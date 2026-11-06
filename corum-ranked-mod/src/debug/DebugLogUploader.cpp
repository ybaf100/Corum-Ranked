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
#include "../ui/RankedTranslations.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/random.hpp>
#include <Geode/utils/web.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
geode::async::TaskHolder<web::WebResponse> request;
}

DebugLogUploader& DebugLogUploader::get() { static DebugLogUploader uploader; return uploader; }

void DebugLogUploader::start() {
    if (m_busy) return;
    if (RankedRuntime::get().spectatorLiveSessionToken().empty()) {
        m_status = i18n::text("debug-logs-failed", "LOG UPLOAD FAILED");return;
    }
    // Newest three Geode logs: this session and the two before it.
    std::vector<std::filesystem::path> files;
    std::error_code error;
    auto const directory = dirs::getGeodeDir() / "logs";
    for (auto const& entry : std::filesystem::directory_iterator(directory, error))
        if (entry.is_regular_file(error) && entry.path().extension() == ".log") files.push_back(entry.path());
    std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) {
        std::error_code ea, eb;
        return std::filesystem::last_write_time(a, ea) > std::filesystem::last_write_time(b, eb);
    });
    if (files.size() > 3) files.resize(3);
    m_chunks.clear();
    for (std::size_t index = 0; index < files.size(); ++index) {
        std::ifstream input(files[index], std::ios::binary);
        std::stringstream buffer;buffer << input.rdbuf();
        auto chunks = debugLogChunks(static_cast<int>(index), files[index].filename().string(), buffer.str());
        m_chunks.insert(m_chunks.end(), chunks.begin(), chunks.end());
    }
    if (m_chunks.empty()) { m_status = i18n::text("debug-logs-failed", "LOG UPLOAD FAILED");return; }
    m_uploadId = utils::random::generateUUID();m_next = 0;m_busy = true;
    log::info("[DebugLogs] upload={} files={} chunks={}", m_uploadId, files.size(), m_chunks.size());
    sendNext();
}

void DebugLogUploader::sendNext() {
    if (m_next >= m_chunks.size()) {
        m_busy = false;m_status = i18n::text("debug-logs-done", "LOGS SENT");return;
    }
    m_status = fmt::format(fmt::runtime(i18n::text("debug-logs-sending", "SENDING LOGS... {}/{}")), m_next + 1, m_chunks.size());
    auto const& chunk = m_chunks[m_next];
    matjson::Value body;
    body["uploadId"] = m_uploadId;body["fileIndex"] = chunk.fileIndex;body["fileName"] = chunk.fileName;
    body["part"] = chunk.part;body["totalParts"] = chunk.totalParts;body["content"] = chunk.content;
    web::WebRequest http;
    http.header("Authorization", "Bearer " + RankedRuntime::get().spectatorLiveSessionToken());
    http.timeout(std::chrono::seconds(20));http.bodyJSON(body);
    request.spawn(http.post("https://api.52-74-37-130.sslip.io/api/ranked/debug/logs"), [this](web::WebResponse response) {
        if (response.code() >= 200 && response.code() < 300) { ++m_next;sendNext();return; }
        log::warn("[DebugLogs] upload={} chunk={} status={}", m_uploadId, m_next, response.code());
        m_busy = false;m_status = i18n::text("debug-logs-failed", "LOG UPLOAD FAILED");
    });
}

} // namespace corum::ranked
#endif
