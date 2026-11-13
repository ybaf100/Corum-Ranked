#include "DebugLogUploader.hpp"
#include <algorithm>

namespace corum::ranked {

std::string debugLogSanitize(std::string_view raw) {
    std::string out;out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        auto const c = static_cast<unsigned char>(raw[i]);
        std::size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool valid = length > 0 && i + length <= raw.size();
        for (std::size_t k = 1; valid && k < length; ++k) valid = (static_cast<unsigned char>(raw[i + k]) & 0xC0) == 0x80;
        if (!valid) { out.push_back('?');++i;continue; }
        if (length == 1 && c == 0) { ++i;continue; }
        out.append(raw.substr(i, length));i += length;
    }
    return out;
}

std::vector<DebugLogChunk> debugLogChunks(int fileIndex, std::string const& fileName, std::string_view raw,
    std::size_t tailBytes, std::size_t chunkBytes) {
    if (raw.size() > tailBytes) {
        raw.remove_prefix(raw.size() - tailBytes);
        if (auto line = raw.find('\n'); line != std::string_view::npos && line + 1 < raw.size()) raw.remove_prefix(line + 1);
    }
    auto const text = debugLogSanitize(raw);
    std::vector<DebugLogChunk> chunks;
    std::size_t at = 0;
    do {
        auto end = std::min(text.size(), at + chunkBytes);
        // Never split a UTF-8 sequence across two chunks.
        while (end < text.size() && end > at && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        chunks.push_back({fileIndex, fileName, static_cast<int>(chunks.size()), 0, text.substr(at, end - at)});
        at = end;
    } while (at < text.size());
    for (auto& chunk : chunks) chunk.totalParts = static_cast<int>(chunks.size());
    return chunks;
}

} // namespace corum::ranked

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "../RankedRuntime.hpp"
#include "../RankedArgon.hpp"
#include "../ui/RankedTranslations.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/random.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
geode::async::TaskHolder<web::WebResponse> request;
}

DebugLogUploader& DebugLogUploader::get() { static DebugLogUploader uploader; return uploader; }

std::vector<DebugLogFile> DebugLogUploader::listFiles() {
    auto collect = [](std::filesystem::path const& directory, bool crash, std::size_t limit) {
        std::vector<DebugLogFile> files;
        std::error_code error;
        for (auto const& entry : std::filesystem::directory_iterator(directory, error)) {
            if (!entry.is_regular_file(error)) continue;
            auto const extension = entry.path().extension().string();
            if (extension != ".log" && extension != ".txt") continue;
            DebugLogFile file;
            file.path = entry.path();file.name = entry.path().filename().string();file.crash = crash;
            file.bytes = entry.file_size(error);file.modified = entry.last_write_time(error);
            files.push_back(std::move(file));
        }
        std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return a.modified > b.modified; });
        if (files.size() > limit) files.resize(limit);
        return files;
    };
    auto logs = collect(dirs::getGeodeDir() / "logs", false, 6);
    if (!logs.empty()) logs.front().current = true;
    auto crashes = collect(dirs::getCrashlogsDir(), true, 6);
    logs.insert(logs.end(), crashes.begin(), crashes.end());
    return logs;
}

void DebugLogUploader::fail(std::string const& reason) {
    log::warn("[DebugLogs] {}", reason);
    m_busy = false;m_status = i18n::text("debug-logs-failed", "LOG UPLOAD FAILED");
}

void DebugLogUploader::start(std::vector<DebugLogFile> const& files) {
    if (m_busy) return;
    // From the main menu there is usually no Ranked session yet (for example
    // right after a crash); the upload then names the GD account instead.
    auto* account = GJAccountManager::get();
    m_accountId = account ? account->m_accountID : 0;
    m_username = account ? std::string(account->m_username) : std::string();
    if (RankedRuntime::get().spectatorLiveSessionToken().empty() && (m_accountId <= 0 || m_username.empty()))
        return fail("no Ranked session and no logged-in GD account");
    if (files.empty()) return fail("no files selected");
    m_chunks.clear();
    auto const count = std::min(files.size(), DebugLogMaxFiles);
    for (std::size_t index = 0; index < count; ++index) {
        std::ifstream input(files[index].path, std::ios::binary);
        std::stringstream buffer;buffer << input.rdbuf();
        // Crash logs are marked in the name so the Discord files say what they are.
        auto name = (files[index].crash ? std::string("crash_") : std::string()) + files[index].name;
        std::string safe;
        for (char c : name) safe.push_back(std::isalnum(static_cast<unsigned char>(c)) || std::string_view("._-+:() ").find(c) != std::string_view::npos ? c : '_');
        if (safe.size() > 120) safe.resize(120);
        auto chunks = debugLogChunks(static_cast<int>(index), safe, buffer.str());
        m_chunks.insert(m_chunks.end(), chunks.begin(), chunks.end());
    }
    if (m_chunks.empty()) return fail("selected files could not be read");
    m_files = static_cast<int>(count);
    m_uploadId = utils::random::generateUUID();m_next = 0;m_busy = true;
    log::info("[DebugLogs] upload={} files={} chunks={}", m_uploadId, count, m_chunks.size());
    m_argonToken.clear();
    if (!RankedRuntime::get().spectatorLiveSessionToken().empty()) { sendNext(); return; }
    // Without a Ranked session the server needs the same GD account proof
    // (Argon) as session creation; a public account ID/name is not enough.
    RankedArgon::get().request([this](ArgonOutcome const& outcome) {
        if (!m_busy) return;
        if (outcome.token.empty()) return fail("GD account verification failed: " + outcome.error);
        m_argonToken = outcome.token;
        sendNext();
    });
}

void DebugLogUploader::sendNext() {
    if (m_next >= m_chunks.size()) {
        m_busy = false;m_status = i18n::text("debug-logs-done", "LOGS SENT");return;
    }
    m_status = fmt::format(fmt::runtime(i18n::text("debug-logs-sending", "SENDING LOGS... {}/{}")), m_next + 1, m_chunks.size());
    auto const& chunk = m_chunks[m_next];
    matjson::Value body;
    body["uploadId"] = m_uploadId;body["fileIndex"] = chunk.fileIndex;body["fileName"] = chunk.fileName;
    body["part"] = chunk.part;body["totalParts"] = chunk.totalParts;body["totalFiles"] = m_files;body["content"] = chunk.content;
    web::WebRequest http;[](auto& q){if constexpr(requires{q.getHttpVersion();})q.version(decltype(q.getHttpVersion())::VERSION_2TLS);}(http); /* HTTP/2: see PREFER_HTTP2 note in RankedRuntime.cpp */
    auto const& token = RankedRuntime::get().spectatorLiveSessionToken();
    std::string url = "https://api.hwanhee1.com/api/ranked/debug/logs";
    if (!token.empty()) http.header("Authorization", "Bearer " + token);
    else {
        url += "/menu";
        body["gdAccountId"] = fmt::format("{}", m_accountId);body["gdUsername"] = m_username;
        body["clientVersion"] = Mod::get()->getVersion().toVString();body["clientBuildChannel"] = "debug";
        body["argonToken"] = m_argonToken;
    }
    http.timeout(std::chrono::seconds(20));http.bodyJSON(body);
    request.spawn(http.post(url), [this](web::WebResponse response) {
        if (response.code() >= 200 && response.code() < 300) {
            // The last chunk reports whether the rebuilt .log files reached Discord.
            auto const reply = response.json().unwrapOr(matjson::Value());
            if (m_next + 1 == m_chunks.size() && !reply["delivered"].asBool().unwrapOr(false)) {
                return fail(fmt::format("upload={} stored but not delivered to Discord", m_uploadId));
            }
            ++m_next;sendNext();return;
        }
        fail(fmt::format("upload={} chunk={} status={} body={}", m_uploadId, m_next, response.code(),
            response.string().unwrapOr("").substr(0, 200)));
    });
}

} // namespace corum::ranked
#endif
