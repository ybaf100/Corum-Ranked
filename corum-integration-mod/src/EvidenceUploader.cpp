#include "EvidenceUploader.hpp"

#include "ApiClient.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {

struct PendingEvidence {
    int levelID = 0;
    int canonicalLevelID = 0;
    int accountID = 0;
    int width = 0;
    int height = 0;
    double capturedAtMs = 0.0;
    std::string username;
    std::string platform;
    std::string modVersion;
    std::string gameVersion;
    std::string geodeVersion;
    std::vector<std::string> loadedMods;
    std::filesystem::path imagePath;
};

async::TaskHolder<web::WebResponse> s_evidenceRequest;
PendingEvidence s_pendingEvidence;
bool s_uploadingEvidence = false;
corum::EvidencePreparationCallback s_evidenceCallback;
std::unordered_map<std::string, double> s_captureWrites;

std::string evidenceStorageKey(int levelID) {
    return fmt::format("clear-evidence.{}", levelID);
}

std::string evidenceScopePrefix(int accountID, int canonicalLevelID) {
    return fmt::format(
        "clear-evidence-v2.{}.{}",
        accountID,
        canonicalLevelID
    );
}

std::string scopedKey(
    int accountID,
    int canonicalLevelID,
    char const* field
) {
    return fmt::format(
        "{}.{}",
        evidenceScopePrefix(accountID, canonicalLevelID),
        field
    );
}

std::filesystem::path pendingDirectory() {
    return Mod::get()->getSaveDir() / "evidence-pending";
}

std::filesystem::path pendingImagePath(
    int accountID,
    int canonicalLevelID
) {
    auto const filename = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "file"),
        ""
    );
    if (filename.empty()) return {};
    return pendingDirectory() / filename;
}

std::string stagedKey(
    int accountID,
    int canonicalLevelID,
    char const* field
) {
    return fmt::format(
        "{}.staged-{}",
        evidenceScopePrefix(accountID, canonicalLevelID),
        field
    );
}

std::filesystem::path stagedImagePath(
    int accountID,
    int canonicalLevelID
) {
    auto const filename = Mod::get()->getSavedValue<std::string>(
        stagedKey(accountID, canonicalLevelID, "file"),
        ""
    );
    if (filename.empty()) return {};
    return pendingDirectory() / filename;
}

bool evidenceCompleteForScope(int accountID, int canonicalLevelID) {
    if (accountID <= 0 || canonicalLevelID <= 0) return false;
    auto const complete = Mod::get()->getSavedValue<bool>(
        scopedKey(accountID, canonicalLevelID, "complete"),
        false
    );
    if (!complete) return false;

    auto const currentGeneration = corum::ApiClient::evidenceGeneration();
    if (currentGeneration.empty()) return false;

    auto const savedGeneration = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "complete-generation"),
        ""
    );
    return !savedGeneration.empty() && savedGeneration == currentGeneration;
}

bool flushEvidenceState() {
    auto result = Mod::get()->saveData();
    if (result.isErr()) {
        log::warn(
            "Could not persist Corum pending-evidence metadata: {}",
            result.unwrapErr()
        );
        return false;
    }
    return true;
}

std::string serializeLoadedMods(std::vector<std::string> const& loadedMods) {
    std::string result;
    for (auto const& loadedMod : loadedMods) {
        if (loadedMod.empty()) continue;
        if (!result.empty()) result.push_back('\n');
        result += loadedMod;
    }
    return result;
}

std::vector<std::string> deserializeLoadedMods(std::string const& value) {
    std::vector<std::string> result;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        auto const end = value.find('\n', offset);
        auto item = value.substr(
            offset,
            end == std::string::npos ? std::string::npos : end - offset
        );
        if (!item.empty()) result.push_back(std::move(item));
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    return result;
}

bool persistStagedCapture(PendingEvidence const& pending) {
    if (
        pending.accountID <= 0 ||
        pending.canonicalLevelID <= 0 ||
        pending.imagePath.empty()
    ) return false;

    auto const accountID = pending.accountID;
    auto const canonicalLevelID = pending.canonicalLevelID;
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "file"),
        pending.imagePath.filename().string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "source"),
        pending.levelID
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "width"),
        pending.width
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "height"),
        pending.height
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "username"),
        pending.username
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "captured-at"),
        pending.capturedAtMs
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "platform"),
        pending.platform
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "mod-version"),
        pending.modVersion
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "game-version"),
        pending.gameVersion
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "geode-version"),
        pending.geodeVersion
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "loaded-mods"),
        serializeLoadedMods(pending.loadedMods)
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "metadata-version"),
        1
    );
    return flushEvidenceState();
}

void clearMatchingStagedCapture(PendingEvidence const& pending) {
    auto const accountID = pending.accountID;
    auto const canonicalLevelID = pending.canonicalLevelID;
    auto const stagedCapturedAt = Mod::get()->getSavedValue<double>(
        stagedKey(accountID, canonicalLevelID, "captured-at"),
        0.0
    );
    if (stagedCapturedAt != pending.capturedAtMs) return;

    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "file"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "source"),
        0
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "width"),
        0
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "height"),
        0
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "username"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "captured-at"),
        0.0
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "platform"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "mod-version"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "game-version"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "geode-version"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "loaded-mods"),
        std::string()
    );
    Mod::get()->setSavedValue(
        stagedKey(accountID, canonicalLevelID, "metadata-version"),
        0
    );
}

std::string base64Encode(std::vector<std::uint8_t> const& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= input.size()) {
        auto const value =
            (static_cast<std::uint32_t>(input[index]) << 16) |
            (static_cast<std::uint32_t>(input[index + 1]) << 8) |
            static_cast<std::uint32_t>(input[index + 2]);
        output.push_back(alphabet[(value >> 18) & 0x3f]);
        output.push_back(alphabet[(value >> 12) & 0x3f]);
        output.push_back(alphabet[(value >> 6) & 0x3f]);
        output.push_back(alphabet[value & 0x3f]);
        index += 3;
    }

    auto const remaining = input.size() - index;
    if (remaining == 1) {
        auto const value = static_cast<std::uint32_t>(input[index]) << 16;
        output.push_back(alphabet[(value >> 18) & 0x3f]);
        output.push_back(alphabet[(value >> 12) & 0x3f]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        auto const value =
            (static_cast<std::uint32_t>(input[index]) << 16) |
            (static_cast<std::uint32_t>(input[index + 1]) << 8);
        output.push_back(alphabet[(value >> 18) & 0x3f]);
        output.push_back(alphabet[(value >> 12) & 0x3f]);
        output.push_back(alphabet[(value >> 6) & 0x3f]);
        output.push_back('=');
    }

    return output;
}

bool isPNG(std::vector<std::uint8_t> const& bytes) {
    static constexpr std::uint8_t signature[] {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    if (bytes.size() < sizeof(signature)) return false;
    for (std::size_t index = 0; index < sizeof(signature); ++index) {
        if (bytes[index] != signature[index]) return false;
    }
    return true;
}

bool isCompletePNGFile(std::filesystem::path const& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    std::uint8_t signature[8] {};
    stream.read(
        reinterpret_cast<char*>(signature),
        static_cast<std::streamsize>(sizeof(signature))
    );
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(signature))) {
        return false;
    }
    static constexpr std::uint8_t expectedSignature[] {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    for (std::size_t index = 0; index < sizeof(expectedSignature); ++index) {
        if (signature[index] != expectedSignature[index]) return false;
    }

    stream.clear();
    stream.seekg(0, std::ios::end);
    auto const length = stream.tellg();
    if (
        length == std::streampos(-1) ||
        static_cast<std::streamoff>(length) < static_cast<std::streamoff>(20)
    ) return false;
    stream.seekg(-12, std::ios::end);

    std::uint8_t endChunk[12] {};
    stream.read(
        reinterpret_cast<char*>(endChunk),
        static_cast<std::streamsize>(sizeof(endChunk))
    );
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(endChunk))) {
        return false;
    }
    return
        endChunk[0] == 0 && endChunk[1] == 0 &&
        endChunk[2] == 0 && endChunk[3] == 0 &&
        endChunk[4] == 'I' && endChunk[5] == 'E' &&
        endChunk[6] == 'N' && endChunk[7] == 'D';
}

std::optional<PendingEvidence> loadStagedCapture(
    int accountID,
    int canonicalLevelID
) {
    auto const imagePath = stagedImagePath(accountID, canonicalLevelID);
    if (imagePath.empty() || !isCompletePNGFile(imagePath)) {
        return std::nullopt;
    }

    PendingEvidence pending;
    pending.levelID = Mod::get()->getSavedValue<int>(
        stagedKey(accountID, canonicalLevelID, "source"),
        canonicalLevelID
    );
    if (pending.levelID <= 0) pending.levelID = canonicalLevelID;
    pending.canonicalLevelID = canonicalLevelID;
    pending.accountID = accountID;
    pending.width = Mod::get()->getSavedValue<int>(
        stagedKey(accountID, canonicalLevelID, "width"),
        0
    );
    pending.height = Mod::get()->getSavedValue<int>(
        stagedKey(accountID, canonicalLevelID, "height"),
        0
    );
    pending.capturedAtMs = Mod::get()->getSavedValue<double>(
        stagedKey(accountID, canonicalLevelID, "captured-at"),
        0.0
    );
    pending.username = Mod::get()->getSavedValue<std::string>(
        stagedKey(accountID, canonicalLevelID, "username"),
        ""
    );
    pending.platform = Mod::get()->getSavedValue<std::string>(
        stagedKey(accountID, canonicalLevelID, "platform"),
        ""
    );
    pending.modVersion = Mod::get()->getSavedValue<std::string>(
        stagedKey(accountID, canonicalLevelID, "mod-version"),
        ""
    );
    pending.gameVersion = Mod::get()->getSavedValue<std::string>(
        stagedKey(accountID, canonicalLevelID, "game-version"),
        ""
    );
    pending.geodeVersion = Mod::get()->getSavedValue<std::string>(
        stagedKey(accountID, canonicalLevelID, "geode-version"),
        ""
    );
    pending.loadedMods = deserializeLoadedMods(
        Mod::get()->getSavedValue<std::string>(
            stagedKey(accountID, canonicalLevelID, "loaded-mods"),
            ""
        )
    );
    pending.imagePath = imagePath;
    return pending;
}

std::optional<std::pair<std::filesystem::path, double>> findRecoverableOrphan(
    int accountID,
    int canonicalLevelID
) {
    auto const directory = pendingDirectory();
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) {
        return std::nullopt;
    }

    auto const prefix = fmt::format(
        "clear-{}-{}-",
        accountID,
        canonicalLevelID
    );
    std::optional<std::pair<std::filesystem::path, double>> newest;

    for (
        std::filesystem::directory_iterator it(directory, error), end;
        it != end && !error;
        it.increment(error)
    ) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || entryError) continue;

        auto const path = it->path();
        if (path.extension() != ".png") continue;
        auto const stem = path.stem().string();
        if (!stem.starts_with(prefix) || stem.size() <= prefix.size()) continue;

        auto const timestampText = stem.substr(prefix.size());
        std::size_t parsedLength = 0;
        double capturedAtMs = 0.0;
        try {
            capturedAtMs = static_cast<double>(std::stoll(
                timestampText,
                &parsedLength
            ));
        } catch (...) {
            continue;
        }
        if (parsedLength != timestampText.size()) continue;
        if (!isCompletePNGFile(path)) continue;

        if (!newest || capturedAtMs > newest->second) {
            newest = std::make_pair(path, capturedAtMs);
        }
    }

    return newest;
}

void finishEvidenceUpload(
    bool success,
    std::string evidenceID = "",
    std::string error = "Could not prepare this record for upload."
) {
    auto callback = std::move(s_evidenceCallback);
    s_evidenceCallback = {};
    s_pendingEvidence = {};
    s_uploadingEvidence = false;

    if (callback) {
        callback({
            .success = success,
            .evidenceID = std::move(evidenceID),
            .error = success ? "" : std::move(error),
        });
    }
}

void handleEvidenceResponse(web::WebResponse response);

void followEvidenceRedirect(web::WebResponse response) {
    auto location = response.header("location");
    if (!location) location = response.header("Location");
    if (!location || location->empty()) {
        log::error("Corum end-screen upload returned a redirect without Location");
        finishEvidenceUpload(false);
        return;
    }

    auto request = web::WebRequest();
    request.header("User-Agent", corum::ApiClient::userAgent());
    request.followRedirects(true);
    request.timeout(std::chrono::seconds(120));
    s_evidenceRequest.spawn(
        request.get(std::string(*location)),
        [](web::WebResponse redirectedResponse) {
            handleEvidenceResponse(std::move(redirectedResponse));
        }
    );
}

void handleEvidenceResponse(web::WebResponse response) {
    if (response.code() < 200 || response.code() >= 300) {
        log::error(
            "Corum end-screen upload failed with HTTP {}: {}",
            response.code(),
            response.errorMessage()
        );
        finishEvidenceUpload(false);
        return;
    }

    auto root = response.json().unwrapOr(matjson::Value());
    if (!root.isObject() || !root["ok"].asBool().unwrapOr(false)) {
        auto code = root["error"]["code"].asString().unwrapOr("UNKNOWN");
        log::error("Corum end-screen upload API error: {}", code);
        finishEvidenceUpload(false);
        return;
    }

    auto evidenceID = root["evidence"]["id"].asString().unwrapOr("");
    if (evidenceID.empty()) {
        log::error("Corum end-screen upload response did not contain an evidence ID");
        finishEvidenceUpload(false);
        return;
    }

    Mod::get()->setSavedValue(
        scopedKey(
            s_pendingEvidence.accountID,
            s_pendingEvidence.canonicalLevelID,
            "uploaded"
        ),
        evidenceID
    );
    flushEvidenceState();
    finishEvidenceUpload(true, evidenceID);
}

void uploadPNG() {
    std::ifstream stream(s_pendingEvidence.imagePath, std::ios::binary);
    if (!stream) {
        log::warn(
            "Could not read pending Corum end-screen PNG; continuing record submission without local evidence: {}",
            s_pendingEvidence.imagePath.string()
        );
        finishEvidenceUpload(true);
        return;
    }

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );
    if (!isPNG(bytes)) {
        log::warn(
            "Corum end-screen capture was not a usable PNG; continuing record submission without local evidence"
        );
        finishEvidenceUpload(true);
        return;
    }

    matjson::Value body;
    body["action"] = "evidence";
    body["levelId"] = s_pendingEvidence.levelID;
    body["gdAccountId"] = s_pendingEvidence.accountID;
    body["gdUsername"] = s_pendingEvidence.username;
    body["mimeType"] = "image/png";
    body["width"] = s_pendingEvidence.width;
    body["height"] = s_pendingEvidence.height;
    body["imageBase64"] = base64Encode(bytes);
    body["platform"] = s_pendingEvidence.platform;
    body["modVersion"] = s_pendingEvidence.modVersion;
    body["gameVersion"] = s_pendingEvidence.gameVersion;
    body["geodeVersion"] = s_pendingEvidence.geodeVersion;
    body["loadedMods"] = matjson::Value::array();
    for (auto const& loadedMod : s_pendingEvidence.loadedMods) {
        body["loadedMods"].push(loadedMod);
    }
    body["clientTimestamp"] = s_pendingEvidence.capturedAtMs;

    auto request = web::WebRequest();
    request.header("User-Agent", corum::ApiClient::userAgent());
    request.bodyJSON(body);
    request.followRedirects(false);
    request.timeout(std::chrono::seconds(120));
    s_evidenceRequest.spawn(
        request.post(corum::ApiClient::baseURL()),
        [](web::WebResponse response) {
            if (response.redirected()) {
                followEvidenceRedirect(std::move(response));
                return;
            }
            handleEvidenceResponse(std::move(response));
        }
    );
}

void storePendingCapture(PendingEvidence const& pending) {
    auto const accountID = pending.accountID;
    auto const canonicalLevelID = pending.canonicalLevelID;
    if (
        accountID <= 0 ||
        canonicalLevelID <= 0 ||
        pending.imagePath.empty()
    ) return;

    auto const previousCapturedAt = Mod::get()->getSavedValue<double>(
        scopedKey(accountID, canonicalLevelID, "captured-at"),
        0.0
    );
    if (previousCapturedAt > pending.capturedAtMs) {
        // PNG encoding is done off the game thread. If two clears of the same
        // map finish encoding out of order, never let the older capture replace
        // the newer one.
        clearMatchingStagedCapture(pending);
        flushEvidenceState();
        std::error_code error;
        std::filesystem::remove(pending.imagePath, error);
        return;
    }

    auto const oldPath = pendingImagePath(accountID, canonicalLevelID);
    auto const prefix = evidenceScopePrefix(accountID, canonicalLevelID);

    Mod::get()->setSavedValue(
        fmt::format("{}.file", prefix),
        pending.imagePath.filename().string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.source", prefix),
        pending.levelID
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.width", prefix),
        pending.width
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.height", prefix),
        pending.height
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.username", prefix),
        pending.username
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.capture-metadata-version", prefix),
        1
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.captured-at", prefix),
        pending.capturedAtMs
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.platform", prefix),
        pending.platform
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.mod-version", prefix),
        pending.modVersion
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.game-version", prefix),
        pending.gameVersion
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.geode-version", prefix),
        pending.geodeVersion
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.loaded-mods", prefix),
        serializeLoadedMods(pending.loadedMods)
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.uploaded", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.complete", prefix),
        false
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.complete-generation", prefix),
        std::string()
    );

    // If this PNG was staged before asynchronous encoding, promote it to the
    // active pending slot and clear only the matching staging generation in the
    // same persisted save operation.
    clearMatchingStagedCapture(pending);

    auto const persisted = flushEvidenceState();
    if (persisted && !oldPath.empty() && oldPath != pending.imagePath) {
        std::error_code error;
        std::filesystem::remove(oldPath, error);
        if (error) {
            log::warn(
                "Could not remove superseded Corum pending PNG {}: {}",
                oldPath.string(),
                error.message()
            );
        }
    }
}

bool hasRecoverablePendingCapture(
    int accountID,
    int canonicalLevelID
) {
    auto const activePath = pendingImagePath(accountID, canonicalLevelID);
    if (!activePath.empty() && isCompletePNGFile(activePath)) return true;
    if (loadStagedCapture(accountID, canonicalLevelID)) return true;
    return findRecoverableOrphan(accountID, canonicalLevelID).has_value();
}

std::filesystem::path recoverPendingCapture(
    int accountID,
    int canonicalLevelID,
    int fallbackSourceLevelID
) {
    auto activePath = pendingImagePath(accountID, canonicalLevelID);
    auto activeCapturedAt = Mod::get()->getSavedValue<double>(
        scopedKey(accountID, canonicalLevelID, "captured-at"),
        0.0
    );
    auto activeValid = !activePath.empty() && isCompletePNGFile(activePath);

    if (auto staged = loadStagedCapture(accountID, canonicalLevelID)) {
        if (staged->levelID <= 0) staged->levelID = fallbackSourceLevelID;
        if (!activeValid || staged->capturedAtMs >= activeCapturedAt) {
            storePendingCapture(*staged);
            activePath = pendingImagePath(accountID, canonicalLevelID);
            activeCapturedAt = staged->capturedAtMs;
            activeValid = !activePath.empty() && isCompletePNGFile(activePath);
            log::info(
                "Recovered staged Corum End Screen capture after restart for account {} / map {}",
                accountID,
                canonicalLevelID
            );
        } else {
            auto const stalePath = staged->imagePath;
            clearMatchingStagedCapture(*staged);
            flushEvidenceState();
            if (!stalePath.empty() && stalePath != activePath) {
                std::error_code removeError;
                std::filesystem::remove(stalePath, removeError);
            }
        }
    }

    // v0.2.35 could finish writing a PNG just before process shutdown but lose
    // the main-thread metadata callback. Recover those complete orphan PNGs by
    // their account/canonical-map/timestamp filename instead of silently
    // submitting the record without evidence.
    if (auto orphan = findRecoverableOrphan(accountID, canonicalLevelID)) {
        if (!activeValid || orphan->second > activeCapturedAt) {
            auto account = GJAccountManager::get();
            auto const username =
                account && account->m_accountID == accountID
                    ? std::string(account->m_username)
                    : std::string();
            PendingEvidence recovered {
                .levelID = fallbackSourceLevelID > 0
                    ? fallbackSourceLevelID
                    : canonicalLevelID,
                .canonicalLevelID = canonicalLevelID,
                .accountID = accountID,
                .width = 0,
                .height = 0,
                .capturedAtMs = orphan->second,
                .username = username,
                .platform = "",
                .modVersion = "",
                .gameVersion = "",
                .geodeVersion = "",
                .loadedMods = {},
                .imagePath = orphan->first,
            };
            storePendingCapture(recovered);
            activePath = pendingImagePath(accountID, canonicalLevelID);
            activeCapturedAt = recovered.capturedAtMs;
            activeValid = !activePath.empty() && isCompletePNGFile(activePath);
            log::info(
                "Recovered orphaned v0.2.35 Corum End Screen PNG for account {} / map {}",
                accountID,
                canonicalLevelID
            );
        }
    }

    return activeValid ? activePath : std::filesystem::path();
}

} // namespace

namespace corum {

std::string latestEvidenceID(int levelID) {
    if (levelID <= 0) return "";
    return Mod::get()->getSavedValue<std::string>(
        evidenceStorageKey(levelID),
        ""
    );
}

bool hasPendingEvidenceForSubmission(
    int canonicalLevelID,
    int accountID
) {
    if (canonicalLevelID <= 0 || accountID <= 0) return false;
    if (evidenceCompleteForScope(accountID, canonicalLevelID)) return false;
    return hasRecoverablePendingCapture(accountID, canonicalLevelID);
}

void prepareEvidenceForSubmission(
    int canonicalLevelID,
    int sourceLevelID,
    int accountID,
    EvidencePreparationCallback callback
) {
    if (!callback) return;
    if (canonicalLevelID <= 0 || sourceLevelID <= 0 || accountID <= 0) {
        callback({
            .success = false,
            .error = "Could not prepare this record for upload.",
        });
        return;
    }

    if (s_uploadingEvidence) {
        callback({
            .success = false,
            .error = "Another record is still being prepared. Please try again.",
        });
        return;
    }

    if (evidenceCompleteForScope(accountID, canonicalLevelID)) {
        callback({.success = true});
        return;
    }

    if (s_captureWrites.contains(evidenceScopePrefix(accountID, canonicalLevelID))) {
        callback({
            .success = false,
            .error = "This record is still being prepared. Please try again in a moment.",
        });
        return;
    }

    auto const configuredImagePath = pendingImagePath(
        accountID,
        canonicalLevelID
    );
    auto const configuredStagedPath = stagedImagePath(
        accountID,
        canonicalLevelID
    );
    auto continueWithoutLocalEvidence = [&]() {
        // Evidence is optional. Reuse a previously uploaded evidence ID when
        // possible; otherwise let the record request continue without one.
        auto evidenceID = latestEvidenceID(sourceLevelID);
        if (evidenceID.empty() && sourceLevelID != canonicalLevelID) {
            evidenceID = latestEvidenceID(canonicalLevelID);
        }
        callback({
            .success = true,
            .evidenceID = std::move(evidenceID),
        });
    };
    auto const imagePath = recoverPendingCapture(
        accountID,
        canonicalLevelID,
        sourceLevelID
    );
    if (imagePath.empty()) {
        if (!configuredStagedPath.empty()) {
            log::warn(
                "Corum staged-evidence metadata has no usable PNG; continuing record submission without local evidence: {}",
                configuredStagedPath.string()
            );
        }
        if (!configuredImagePath.empty()) {
            log::warn(
                "Corum pending-evidence metadata has no usable PNG; continuing record submission without local evidence: {}",
                configuredImagePath.string()
            );
        }
        continueWithoutLocalEvidence();
        return;
    }

    std::error_code existsError;
    if (!std::filesystem::exists(imagePath, existsError) || existsError) {
        log::warn(
            "Pending Corum evidence disappeared before upload; continuing record submission without local evidence: {}",
            imagePath.string()
        );
        continueWithoutLocalEvidence();
        return;
    }

    auto const uploadedID = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "uploaded"),
        ""
    );
    if (!uploadedID.empty()) {
        callback({
            .success = true,
            .evidenceID = uploadedID,
        });
        return;
    }

    auto account = GJAccountManager::get();
    auto username = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "username"),
        ""
    );
    if (username.empty() && account && account->m_accountID == accountID) {
        username = std::string(account->m_username);
    }

    auto storedSource = Mod::get()->getSavedValue<int>(
        scopedKey(accountID, canonicalLevelID, "source"),
        sourceLevelID
    );
    if (storedSource <= 0) storedSource = sourceLevelID;

    auto const metadataVersion = Mod::get()->getSavedValue<int>(
        scopedKey(accountID, canonicalLevelID, "capture-metadata-version"),
        0
    );
    auto capturedAtMs = Mod::get()->getSavedValue<double>(
        scopedKey(accountID, canonicalLevelID, "captured-at"),
        0.0
    );
    auto platform = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "platform"),
        ""
    );
    auto modVersion = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "mod-version"),
        ""
    );
    auto gameVersion = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "game-version"),
        ""
    );
    auto geodeVersion = Mod::get()->getSavedValue<std::string>(
        scopedKey(accountID, canonicalLevelID, "geode-version"),
        ""
    );
    auto loadedMods = deserializeLoadedMods(
        Mod::get()->getSavedValue<std::string>(
            scopedKey(accountID, canonicalLevelID, "loaded-mods"),
            ""
        )
    );

    // v0.2.32 pending captures did not snapshot their environment. Preserve
    // compatibility by filling only those legacy captures from this session.
    if (metadataVersion < 1) {
        auto const now = std::chrono::system_clock::now().time_since_epoch();
        capturedAtMs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()
        );
        platform = GEODE_PLATFORM_NAME;
        modVersion = Mod::get()->getVersion().toVString();
        gameVersion = GEODE_GD_VERSION_STRING;
        geodeVersion = Loader::get()->getVersion().toVString();
        loadedMods = corum::ApiClient::loadedMods();
    }

    s_pendingEvidence = {
        .levelID = storedSource,
        .canonicalLevelID = canonicalLevelID,
        .accountID = accountID,
        .width = Mod::get()->getSavedValue<int>(
            scopedKey(accountID, canonicalLevelID, "width"),
            0
        ),
        .height = Mod::get()->getSavedValue<int>(
            scopedKey(accountID, canonicalLevelID, "height"),
            0
        ),
        .capturedAtMs = capturedAtMs,
        .username = std::move(username),
        .platform = std::move(platform),
        .modVersion = std::move(modVersion),
        .gameVersion = std::move(gameVersion),
        .geodeVersion = std::move(geodeVersion),
        .loadedMods = std::move(loadedMods),
        .imagePath = imagePath,
    };
    s_evidenceCallback = std::move(callback);
    s_uploadingEvidence = true;
    uploadPNG();
}

void markEvidenceSubmissionComplete(
    int canonicalLevelID,
    int accountID
) {
    if (canonicalLevelID <= 0 || accountID <= 0) return;

    auto const imagePath = pendingImagePath(accountID, canonicalLevelID);
    auto const prefix = evidenceScopePrefix(accountID, canonicalLevelID);
    auto const previousSource = Mod::get()->getSavedValue<int>(
        fmt::format("{}.source", prefix), 0
    );
    auto const previousWidth = Mod::get()->getSavedValue<int>(
        fmt::format("{}.width", prefix), 0
    );
    auto const previousHeight = Mod::get()->getSavedValue<int>(
        fmt::format("{}.height", prefix), 0
    );
    auto const previousUsername = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.username", prefix), ""
    );
    auto const previousMetadataVersion = Mod::get()->getSavedValue<int>(
        fmt::format("{}.capture-metadata-version", prefix), 0
    );
    auto const previousCapturedAt = Mod::get()->getSavedValue<double>(
        fmt::format("{}.captured-at", prefix), 0.0
    );
    auto const previousPlatform = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.platform", prefix), ""
    );
    auto const previousModVersion = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.mod-version", prefix), ""
    );
    auto const previousGameVersion = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.game-version", prefix), ""
    );
    auto const previousGeodeVersion = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.geode-version", prefix), ""
    );
    auto const previousLoadedMods = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.loaded-mods", prefix), ""
    );
    auto const previousUploaded = Mod::get()->getSavedValue<std::string>(
        fmt::format("{}.uploaded", prefix), ""
    );

    Mod::get()->setSavedValue(
        fmt::format("{}.file", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.source", prefix),
        0
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.width", prefix),
        0
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.height", prefix),
        0
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.username", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.capture-metadata-version", prefix),
        0
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.captured-at", prefix),
        0.0
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.platform", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.mod-version", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.game-version", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.geode-version", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.loaded-mods", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.uploaded", prefix),
        std::string()
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.complete", prefix),
        true
    );
    Mod::get()->setSavedValue(
        fmt::format("{}.complete-generation", prefix),
        corum::ApiClient::evidenceGeneration()
    );

    if (!flushEvidenceState()) {
        Mod::get()->setSavedValue(
            fmt::format("{}.file", prefix),
            imagePath.empty() ? std::string() : imagePath.filename().string()
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.source", prefix), previousSource
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.width", prefix), previousWidth
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.height", prefix), previousHeight
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.username", prefix), previousUsername
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.capture-metadata-version", prefix),
            previousMetadataVersion
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.captured-at", prefix), previousCapturedAt
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.platform", prefix), previousPlatform
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.mod-version", prefix), previousModVersion
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.game-version", prefix), previousGameVersion
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.geode-version", prefix), previousGeodeVersion
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.loaded-mods", prefix), previousLoadedMods
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.uploaded", prefix), previousUploaded
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.complete", prefix), false
        );
        Mod::get()->setSavedValue(
            fmt::format("{}.complete-generation", prefix), std::string()
        );
        return;
    }

    if (!imagePath.empty()) {
        std::error_code error;
        std::filesystem::remove(imagePath, error);
        if (error) {
            log::warn(
                "Could not remove submitted Corum pending PNG {}: {}",
                imagePath.string(),
                error.message()
            );
        }
    }
}

} // namespace corum

class $modify(CorumEndLevelEvidenceLayer, EndLevelLayer) {
    struct Fields {
        bool captureScheduled = false;
    };

    void showLayer(bool instant) {
        EndLevelLayer::showLayer(instant);

        if (m_fields->captureScheduled) return;
        if (!Mod::get()->getSettingValue<bool>("enable-record-submit")) return;
        if (!m_playLayer || !m_playLayer->m_level) return;
        if (m_playLayer->m_isPracticeMode || m_playLayer->m_isTestMode) return;
        if (!m_playLayer->m_hasCompletedLevel) return;

        auto const levelID = static_cast<int>(m_playLayer->m_level->m_levelID);
        auto const map = corum::ApiClient::startupMap(levelID);
        if (levelID <= 0 || !map) return;

        auto account = GJAccountManager::get();
        auto const username = account ? std::string(account->m_username) : "";
        if (!account || account->m_accountID <= 0 || username.empty()) return;
        if (evidenceCompleteForScope(account->m_accountID, map->levelID)) {
            log::info(
                "Skipped Corum End Screen capture because evidence is already complete for account {} / map {}",
                account->m_accountID,
                map->levelID
            );
            return;
        }

        m_fields->captureScheduled = true;
        log::info(
            "Scheduled Corum End Screen capture for account {} / map {}",
            account->m_accountID,
            map->levelID
        );
        scheduleOnce(
            schedule_selector(CorumEndLevelEvidenceLayer::captureEndScreen),
            0.80f
        );
    }

    void onReplay(CCObject* sender) {
        // Replay can reuse this EndLevelLayer. Cancel a capture that has not
        // fired yet and release the per-layer guard so the next completion can
        // schedule its own fully-settled End Screen frame.
        unschedule(
            schedule_selector(CorumEndLevelEvidenceLayer::captureEndScreen)
        );
        m_fields->captureScheduled = false;
        EndLevelLayer::onReplay(sender);
    }

    void captureEndScreen(float) {
        // EndLevelLayer can be reused when Replay is pressed. Release the
        // one-shot scheduling guard as soon as this callback starts so the next
        // completed attempt can schedule a fresh capture, even if the previous
        // PNG was moved or deleted outside the mod.
        m_fields->captureScheduled = false;

        if (!m_playLayer || !m_playLayer->m_level) return;

        auto const levelID = static_cast<int>(m_playLayer->m_level->m_levelID);
        auto const map = corum::ApiClient::startupMap(levelID);
        auto account = GJAccountManager::get();
        auto const username = account ? std::string(account->m_username) : "";
        if (
            levelID <= 0 ||
            !map ||
            !account ||
            account->m_accountID <= 0 ||
            username.empty()
        ) return;
        if (evidenceCompleteForScope(account->m_accountID, map->levelID)) {
            log::info(
                "Cancelled scheduled Corum End Screen capture because evidence became complete for account {} / map {}",
                account->m_accountID,
                map->levelID
            );
            return;
        }

        auto director = CCDirector::sharedDirector();
        auto scene = director ? director->getRunningScene() : nullptr;
        if (!director || !scene) return;

        // CCRenderTexture expects Cocos logical points here. Its implementation
        // multiplies these values by CC_CONTENT_SCALE_FACTOR() internally.
        // Passing getWinSizeInPixels() caused that scale to be applied twice,
        // producing a huge black render target with the scene only in the
        // bottom-left corner.
        auto const windowSize = director->getWinSize();
        auto const logicalWidth = static_cast<int>(windowSize.width);
        auto const logicalHeight = static_cast<int>(windowSize.height);
        if (logicalWidth <= 0 || logicalHeight <= 0) return;

        auto renderTexture = CCRenderTexture::create(logicalWidth, logicalHeight);
        if (!renderTexture) {
            log::error("Could not create Corum end-screen render texture");
            return;
        }

        auto captureLabel = CCLabelBMFont::create(
            fmt::format("{} / {}", username, map->title).c_str(),
            "bigFont.fnt"
        );
        captureLabel->setAnchorPoint({0.0f, 1.0f});
        captureLabel->setColor(ccc3(255, 255, 255));
        captureLabel->setOpacity(235);
        captureLabel->setPosition({8.0f, windowSize.height - 8.0f});
        captureLabel->limitLabelWidth(
            windowSize.width * 0.48f,
            0.30f,
            0.16f
        );
        scene->addChild(captureLabel, 100000);

        renderTexture->beginWithClear(0.0f, 0.0f, 0.0f, 1.0f);
        scene->visit();
        renderTexture->end();
        captureLabel->removeFromParentAndCleanup(true);

        auto image = renderTexture->newCCImage(true);
        if (!image) {
            log::error("Could not create Corum end-screen image from render texture");
            return;
        }

        auto sprite = renderTexture->getSprite();
        auto texture = sprite ? sprite->getTexture() : nullptr;
        auto const pixelSize = texture
            ? texture->getContentSizeInPixels()
            : director->getWinSizeInPixels();
        auto const width = static_cast<int>(pixelSize.width);
        auto const height = static_cast<int>(pixelSize.height);
        if (width <= 0 || height <= 0) {
            delete image;
            return;
        }

        auto directory = pendingDirectory();
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        auto const capturedAtMs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        auto imagePath = directory / fmt::format(
            "clear-{}-{}-{}.png",
            account->m_accountID,
            map->levelID,
            static_cast<std::int64_t>(capturedAtMs)
        );
        if (error) {
            delete image;
            log::error(
                "Could not create Corum pending-evidence directory: {}",
                error.message()
            );
            return;
        }

        PendingEvidence pending {
            .levelID = levelID,
            .canonicalLevelID = map->levelID,
            .accountID = account->m_accountID,
            .width = width,
            .height = height,
            .capturedAtMs = capturedAtMs,
            .username = username,
            .platform = GEODE_PLATFORM_NAME,
            .modVersion = Mod::get()->getVersion().toVString(),
            .gameVersion = GEODE_GD_VERSION_STRING,
            .geodeVersion = Loader::get()->getVersion().toVString(),
            .loadedMods = corum::ApiClient::loadedMods(),
            .imagePath = imagePath,
        };

        // Persist the small metadata record before the expensive PNG encoding
        // starts. If the process exits after the PNG reaches disk but before
        // the worker's main-thread completion callback, the next session can
        // promote this staged capture instead of losing it.
        if (!persistStagedCapture(pending)) {
            delete image;
            log::error(
                "Could not persist staged Corum End Screen metadata before PNG encoding"
            );
            return;
        }

        s_captureWrites[evidenceScopePrefix(
            pending.accountID,
            pending.canonicalLevelID
        )] = pending.capturedAtMs;

        // GPU readback has to happen on the game thread, but PNG compression
        // and disk I/O do not. Keeping saveToFile() off the End Screen frame
        // removes the largest avoidable hitch while preserving lossless PNG.
        std::thread([
            capturedImage = std::unique_ptr<CCImage>(image),
            pending = std::move(pending)
        ]() mutable {
            auto temporaryPath = pending.imagePath;
            temporaryPath.replace_extension(".part.png");
            auto saved = capturedImage->saveToFile(
                temporaryPath.string().c_str(),
                false
            );
            capturedImage.reset();

            if (saved) {
                std::error_code renameError;
                std::filesystem::rename(
                    temporaryPath,
                    pending.imagePath,
                    renameError
                );
                saved = !renameError;
                if (renameError) {
                    std::filesystem::remove(temporaryPath, renameError);
                }
            }

            Loader::get()->queueInMainThread([
                saved,
                pending = std::move(pending)
            ]() mutable {
                auto const scope = evidenceScopePrefix(
                    pending.accountID,
                    pending.canonicalLevelID
                );
                auto const write = s_captureWrites.find(scope);
                if (
                    write != s_captureWrites.end() &&
                    write->second == pending.capturedAtMs
                ) {
                    s_captureWrites.erase(write);
                }

                if (!saved) {
                    log::error(
                        "Could not write Corum end-screen PNG: {}",
                        pending.imagePath.string()
                    );
                    return;
                }

                auto const accountID = pending.accountID;
                auto const canonicalLevelID = pending.canonicalLevelID;
                storePendingCapture(pending);
                log::info(
                    "Stored Corum end-screen capture locally for account {} / map {}",
                    accountID,
                    canonicalLevelID
                );
            });
        }).detach();
    }
};
