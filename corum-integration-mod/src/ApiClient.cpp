#include "ApiClient.hpp"
#include "ClientVersion.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace {

std::unordered_map<std::int64_t, corum::MapInfo> s_mapCache;
std::unordered_set<int> s_notListedCache;
geode::async::TaskHolder<geode::utils::web::WebResponse> s_manifestRequest;
geode::async::TaskHolder<geode::utils::web::WebResponse> s_catalogRequest;
corum::StartupStatus s_startupStatus = corum::StartupStatus::NotStarted;
corum::CatalogResult::ClientPolicy s_clientPolicy;
std::vector<std::string> s_loadedMods;
bool s_loadedModsCaptured = false;
std::string s_evidenceGeneration;

constexpr std::string_view kEndpointManifestURL =
    "https://raw.githubusercontent.com/ybaf100/Corum-v3-beta/main/"
    "public/corum-endpoint.json";

std::string s_baseURL;

std::string trim(std::string value);

std::string clientPlatformKey() {
#if defined(GEODE_IS_WINDOWS)
    return "windows";
#elif defined(GEODE_IS_ANDROID)
    return "android";
#elif defined(GEODE_IS_IOS)
    return "ios";
#else
    return "unsupported";
#endif
}

bool validUpdateURL(std::string const& url) {
    constexpr std::string_view releasePrefix =
        "https://github.com/ybaf100/Corum-integration/releases";
    constexpr std::string_view releasePathPrefix =
        "https://github.com/ybaf100/Corum-integration/releases/";
    return url == releasePrefix || url.starts_with(releasePathPrefix);
}

corum::CatalogResult::ClientPolicy parseClientPolicy(
    matjson::Value const& root
) {
    corum::CatalogResult::ClientPolicy policy {
        .platform = clientPlatformKey(),
        .currentVersion = Mod::get()->getVersion().toVString(),
    };

    if (
        !root.contains("clientPolicy") ||
        !root["clientPolicy"].isObject()
    ) {
        return policy;
    }

    auto const platformNode = root["clientPolicy"][policy.platform];
    if (!platformNode.isObject()) return policy;

    policy.minimumSupportedVersion = trim(
        platformNode["minimumSupportedVersion"].asString().unwrapOr("")
    );
    policy.latestVersion = trim(
        platformNode["latestVersion"].asString().unwrapOr("")
    );
    policy.updateURL = trim(
        platformNode["updateUrl"].asString().unwrapOr("")
    );
    if (!validUpdateURL(policy.updateURL)) policy.updateURL.clear();

    policy.enforcementEnabled =
        platformNode["enforcementEnabled"].asBool().unwrapOr(true);

    auto const current = corum::parseSemanticVersion(policy.currentVersion);
    auto const minimum =
        corum::parseSemanticVersion(policy.minimumSupportedVersion);
    if (!current || !minimum) {
        log::warn(
            "Ignoring invalid Corum client version policy for {}: current={}, minimum={}",
            policy.platform,
            policy.currentVersion,
            policy.minimumSupportedVersion
        );
        return policy;
    }

    policy.present = true;
    policy.supported =
        !policy.enforcementEnabled ||
        corum::compareSemanticVersions(*current, *minimum) >= 0;
    return policy;
}

void showUpdatePopup(
    std::string const& title,
    std::string const& message
) {
    auto const updateURL = s_clientPolicy.updateURL;
    if (updateURL.empty()) {
        FLAlertLayer::create(
            title.c_str(),
            message.c_str(),
            "Close"
        )->show();
        return;
    }

    createQuickPopup(
        title.c_str(),
        message,
        "Close",
        "Update",
        [updateURL](FLAlertLayer*, bool openUpdate) {
            if (!openUpdate) return;
            CCApplication::sharedApplication()->openURL(updateURL.c_str());
        },
        true,
        true
    );
}

void captureLoadedMods() {
    if (s_loadedModsCaptured) return;
    s_loadedModsCaptured = true;
    s_loadedMods.clear();

    auto* ownMod = Mod::get();
    for (auto* mod : Loader::get()->getAllMods()) {
        if (!mod || !mod->isLoaded() || mod->isInternal() || mod == ownMod) {
            continue;
        }

        auto const id = static_cast<std::string>(mod->getID());
        if (id.empty()) continue;

        auto const version = mod->getVersion().toVString();
        s_loadedMods.push_back(
            version.empty() ? id : fmt::format("{}@{}", id, version)
        );
    }

    std::sort(s_loadedMods.begin(), s_loadedMods.end());
    s_loadedMods.erase(
        std::unique(s_loadedMods.begin(), s_loadedMods.end()),
        s_loadedMods.end()
    );
    if (s_loadedMods.size() > 128) {
        s_loadedMods.resize(128);
    }
    log::info("Captured {} loaded mods for Corum record metadata", s_loadedMods.size());
}

std::string trim(std::string value) {
    auto const first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";

    auto const last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::int64_t mapCacheKey(int levelID, int accountID) {
    return
        (static_cast<std::int64_t>(accountID) << 32) |
        static_cast<std::uint32_t>(levelID);
}

std::string responseErrorCode(matjson::Value const& root) {
    if (!root.contains("error") || !root["error"].isObject()) return "";
    return root["error"]["code"].asString().unwrapOr("");
}

std::string responseErrorMessage(matjson::Value const& root) {
    if (!root.contains("error") || !root["error"].isObject()) return "";
    return root["error"]["message"].asString().unwrapOr("");
}

std::string withQuery(std::string url, std::string const& query) {
    std::string separator = "?";
    if (url.ends_with("?") || url.ends_with("&")) {
        separator = "";
    } else if (url.find('?') != std::string::npos) {
        separator = "&";
    }
    return fmt::format("{}{}{}", url, separator, query);
}

bool validAppsScriptURL(std::string const& url) {
    constexpr std::string_view prefix = "https://script.google.com/macros/s/";
    return url.starts_with(prefix) && url.ends_with("/exec");
}

std::optional<corum::MapInfo> parseMapValue(matjson::Value const& value) {
    if (!value.isObject()) return std::nullopt;

    auto levelID = static_cast<int>(value["levelId"].asInt().unwrapOr(0));
    if (levelID <= 0) {
        auto const levelIDText = value["levelId"].asString().unwrapOr("");
        levelID = numFromString<int>(levelIDText).unwrapOr(0);
    }
    auto alternateLevelID = static_cast<int>(
        value["alternateLevelId"].asInt().unwrapOr(0)
    );
    if (alternateLevelID <= 0) {
        auto const alternateLevelIDText =
            value["alternateLevelId"].asString().unwrapOr("");
        alternateLevelID =
            numFromString<int>(alternateLevelIDText).unwrapOr(0);
    }
    if (alternateLevelID == levelID) alternateLevelID = 0;

    corum::MapInfo map {
        .rank = static_cast<int>(value["rank"].asInt().unwrapOr(0)),
        .levelID = levelID,
        .alternateLevelID = alternateLevelID,
        .tier = value["tier"].asString().unwrapOr("Unranked"),
        .title = value["title"].asString().unwrapOr(""),
        .rating = value["rating"].asString().unwrapOr(""),
        .length = value["length"].asString().unwrapOr(""),
        .creator = value["creator"].asString().unwrapOr(""),
        .verifier = value["verifier"].asString().unwrapOr(""),
        .minimumRecord = value["minimumRecord"].asDouble().unwrapOr(100.0),
    };

    if (map.levelID <= 0 || map.title.empty()) return std::nullopt;
    if (
        !std::isfinite(map.minimumRecord) ||
        map.minimumRecord < 1.0 ||
        map.minimumRecord > 100.0
    ) {
        map.minimumRecord = 100.0;
    }
    return map;
}

void showStartupFailure(std::string message) {
    s_startupStatus = corum::StartupStatus::Failed;
    message = trim(std::move(message));
    if (message.empty()) message = "unknown error";
    if (message.size() > 120) message.resize(120);

    auto const notification = fmt::format("C Integration failed: {}", message);
    log::error("{}", notification);
    Notification::create(notification, NotificationIcon::Error, 5.0f)->show();
}

void downloadCatalog() {
    auto request = web::WebRequest();
    request.header("User-Agent", corum::ApiClient::userAgent());
    request.timeout(std::chrono::seconds(30));

    s_catalogRequest.spawn(
        request.get(corum::ApiClient::catalogURL()),
        [](web::WebResponse response) {
            auto const catalog =
                corum::ApiClient::parseCatalogResponse(response);
            if (!catalog.ok) {
                showStartupFailure(
                    catalog.message.empty()
                        ? "invalid map list response"
                        : catalog.message
                );
                return;
            }

            s_mapCache.clear();
            s_notListedCache.clear();
            s_evidenceGeneration = catalog.evidenceGeneration;
            s_clientPolicy = catalog.clientPolicy;
            for (auto const& map : catalog.maps) {
                s_mapCache[mapCacheKey(map.levelID, 0)] = map;
                if (map.alternateLevelID > 0) {
                    s_mapCache[mapCacheKey(map.alternateLevelID, 0)] = map;
                }
            }

            s_startupStatus = corum::StartupStatus::Ready;
            log::info(
                "Corum startup catalog ready with {} maps",
                catalog.maps.size()
            );
            if (corum::ApiClient::isOutdated()) {
                corum::ApiClient::showStartupOutdatedWarning();
            } else {
                Notification::create(
                    "C Integration is ready",
                    NotificationIcon::Success,
                    3.0f
                )->show();
            }
        }
    );
}

cocos2d::ccColor3B colorFromHex(int value) {
    return cocos2d::ccc3(
        static_cast<GLubyte>((value >> 16) & 0xff),
        static_cast<GLubyte>((value >> 8) & 0xff),
        static_cast<GLubyte>(value & 0xff)
    );
}

} // namespace

namespace corum {

bool ApiClient::isConfigured() {
    return true;
}

std::string ApiClient::baseURL() {
    return s_baseURL;
}

std::string ApiClient::catalogURL() {
    return withQuery(baseURL(), "action=list");
}

std::string ApiClient::playerRecordsURL(int accountID) {
    return withQuery(
        baseURL(),
        fmt::format("action=playerRecords&gdAccountId={}", accountID)
    );
}

std::string ApiClient::mapURL(int levelID, int accountID) {
    auto result = withQuery(
        baseURL(),
        fmt::format("action=map&levelId={}", levelID)
    );
    if (accountID > 0) {
        result += fmt::format("&gdAccountId={}", accountID);
    }
    return result;
}

std::string ApiClient::userAgent() {
    return fmt::format(
        "{}/{}; Geometry Dash/{}; Geode/{}; {}",
        Mod::get()->getID(),
        Mod::get()->getVersion().toVString(),
        GEODE_GD_VERSION_STRING,
        Loader::get()->getVersion().toVString(),
        GEODE_PLATFORM_NAME
    );
}

void ApiClient::initializeSession() {
    captureLoadedMods();
    if (s_startupStatus != StartupStatus::NotStarted) return;
    s_startupStatus = StartupStatus::Initializing;

    auto request = web::WebRequest();
    request.header("User-Agent", userAgent());
    request.timeout(std::chrono::seconds(15));

    s_manifestRequest.spawn(
        request.get(std::string(kEndpointManifestURL)),
        [](web::WebResponse response) {
            if (response.code() < 200 || response.code() >= 300) {
                auto const networkError = std::string(response.errorMessage());
                showStartupFailure(networkError.empty()
                    ? fmt::format("endpoint config HTTP {}", response.code())
                    : fmt::format("endpoint config: {}", networkError));
                return;
            }

            auto const root = response.json().unwrapOr(matjson::Value());
            if (!root.isObject()) {
                showStartupFailure("invalid endpoint config");
                return;
            }

            auto const endpoint = trim(
                root["apiUrl"].asString().unwrapOr("")
            );
            if (!validAppsScriptURL(endpoint)) {
                showStartupFailure("invalid API URL in endpoint config");
                return;
            }

            s_baseURL = endpoint;
            downloadCatalog();
        }
    );
}

std::vector<std::string> const& ApiClient::loadedMods() {
    captureLoadedMods();
    return s_loadedMods;
}

StartupStatus ApiClient::startupStatus() {
    return s_startupStatus;
}

bool ApiClient::startupFinished() {
    return
        s_startupStatus == StartupStatus::Ready ||
        s_startupStatus == StartupStatus::Failed;
}

bool ApiClient::startupReady() {
    return s_startupStatus == StartupStatus::Ready;
}

bool ApiClient::isOutdated() {
    return
        s_clientPolicy.present &&
        s_clientPolicy.enforcementEnabled &&
        !s_clientPolicy.supported;
}

bool ApiClient::submissionAllowed() {
    return !isOutdated();
}

std::string ApiClient::minimumSupportedVersion() {
    return s_clientPolicy.minimumSupportedVersion;
}

std::string ApiClient::latestVersion() {
    return s_clientPolicy.latestVersion;
}

std::string ApiClient::updateURL() {
    return s_clientPolicy.updateURL;
}

void ApiClient::showStartupOutdatedWarning() {
    auto const minimum = minimumSupportedVersion();
    auto const requirement = minimum.empty()
        ? std::string("A newer supported version is required.")
        : fmt::format("Version <cy>{}</c> or newer is required.", minimum);
    showUpdatePopup(
        "C Integration is outdated!",
        fmt::format(
            "<cr>This version can no longer submit Corum records.</c>\n"
            "{}\n"
            "Level information will remain available.",
            requirement
        )
    );
}

void ApiClient::showUpdateRequiredWarning() {
    auto const minimum = minimumSupportedVersion();
    auto const requirement = minimum.empty()
        ? std::string("Install the latest C Integration release to continue.")
        : fmt::format(
            "Install C Integration <cy>{}</c> or newer to continue.",
            minimum
        );
    showUpdatePopup(
        "Update Required",
        fmt::format(
            "<cr>Record submission is unavailable on this version.</c>\n{}",
            requirement
        )
    );
}

std::optional<MapInfo> ApiClient::startupMap(int levelID) {
    if (!startupReady()) return std::nullopt;
    return cachedMap(levelID, 0);
}

std::string ApiClient::evidenceGeneration() {
    return s_evidenceGeneration;
}

CatalogResult ApiClient::parseCatalogResponse(web::WebResponse& response) {
    if (response.code() < 200 || response.code() >= 300) {
        auto const networkError = std::string(response.errorMessage());
        return {
            .message = networkError.empty()
                ? fmt::format("map list HTTP {}", response.code())
                : fmt::format("map list: {}", networkError),
        };
    }

    auto const root = response.json().unwrapOr(matjson::Value());
    if (
        !root.isObject() ||
        !root["ok"].asBool().unwrapOr(false) ||
        !root.contains("maps") ||
        !root["maps"].isArray()
    ) {
        return {.message = "invalid map list response"};
    }

    auto const mapsResult = root["maps"].asArray();
    if (mapsResult.isErr()) {
        return {.message = "invalid map list array"};
    }

    CatalogResult result {
        .ok = true,
        .evidenceGeneration = root["evidenceGeneration"].asString().unwrapOr(""),
        .clientPolicy = parseClientPolicy(root),
    };
    for (auto const& value : mapsResult.unwrap()) {
        auto const map = parseMapValue(value);
        if (map) result.maps.push_back(*map);
    }

    if (result.maps.empty()) {
        return {.message = "the Corum map list is empty"};
    }

    std::sort(
        result.maps.begin(),
        result.maps.end(),
        [](MapInfo const& left, MapInfo const& right) {
            return left.rank < right.rank;
        }
    );
    return result;
}

LookupResult ApiClient::parseMapResponse(web::WebResponse& response) {
    if (response.code() < 200 || response.code() >= 300) {
        auto const networkError = std::string(response.errorMessage());
        return {
            .status = LookupStatus::Error,
            .errorCode = "HTTP_ERROR",
            .message = networkError.empty()
                ? fmt::format("HTTP {}", response.code())
                : networkError,
        };
    }

    auto root = response.json().unwrapOr(matjson::Value());
    if (!root.isObject()) {
        return {
            .status = LookupStatus::Error,
            .errorCode = "INVALID_RESPONSE",
            .message = "API response was not valid JSON",
        };
    }

    if (!root["ok"].asBool().unwrapOr(false)) {
        auto const code = responseErrorCode(root);
        auto const message = responseErrorMessage(root);
        return {
            .status = code == "MAP_NOT_FOUND" ? LookupStatus::NotListed : LookupStatus::Error,
            .errorCode = code,
            .message = message,
        };
    }

    if (!root.contains("map") || !root["map"].isObject()) {
        return {
            .status = LookupStatus::Error,
            .errorCode = "INVALID_RESPONSE",
            .message = "API response did not include map data",
        };
    }

    auto map = parseMapValue(root["map"]);
    if (!map) {
        return {
            .status = LookupStatus::Error,
            .errorCode = "INVALID_RESPONSE",
            .message = "API returned incomplete map data",
        };
    }

    if (root.contains("playerRecord") && root["playerRecord"].isObject()) {
        auto const& playerRecord = root["playerRecord"];
        auto const frozenScore = playerRecord["score"].asDouble().unwrapOr(-1.0);
        auto const registeredMinimum =
            playerRecord["registeredMinimumRecord"].asDouble().unwrapOr(100.0);

        if (std::isfinite(frozenScore) && frozenScore >= 0.0) {
            map->hasFrozenScore = true;
            map->frozenScore = frozenScore;
            map->serverPercent = static_cast<int>(
                playerRecord["percent"].asInt().unwrapOr(0)
            );
            map->initialPercent = static_cast<int>(
                playerRecord["scoredPercent"].asInt().unwrapOr(
                    playerRecord["initialPercent"].asInt().unwrapOr(0)
                )
            );
            map->registeredRank = static_cast<int>(
                playerRecord["scoredRank"].asInt().unwrapOr(
                    playerRecord["registeredRank"].asInt().unwrapOr(0)
                )
            );
            auto const scoredMinimum =
                playerRecord["scoredMinimumRecord"].asDouble().unwrapOr(
                    registeredMinimum
                );
            map->registeredMinimumRecord =
                std::isfinite(scoredMinimum) &&
                scoredMinimum >= 1.0 &&
                scoredMinimum <= 100.0
                    ? scoredMinimum
                    : 100.0;
        }
    }

    return {
        .status = LookupStatus::Listed,
        .map = std::move(*map),
    };
}

void ApiClient::cacheMap(MapInfo const& map, int accountID) {
    s_mapCache[mapCacheKey(map.levelID, accountID)] = map;
    if (map.alternateLevelID > 0) {
        s_mapCache[mapCacheKey(map.alternateLevelID, accountID)] = map;
    }
    s_notListedCache.erase(map.levelID);
    if (map.alternateLevelID > 0) {
        s_notListedCache.erase(map.alternateLevelID);
    }
}

void ApiClient::clearCachedMap(int levelID, int accountID) {
    auto const found = s_mapCache.find(mapCacheKey(levelID, accountID));
    if (found == s_mapCache.end()) {
        s_mapCache.erase(mapCacheKey(levelID, accountID));
        return;
    }

    auto const map = found->second;
    s_mapCache.erase(mapCacheKey(map.levelID, accountID));
    if (map.alternateLevelID > 0) {
        s_mapCache.erase(mapCacheKey(map.alternateLevelID, accountID));
    }
}

void ApiClient::cacheNotListed(int levelID) {
    for (auto iterator = s_mapCache.begin(); iterator != s_mapCache.end();) {
        if (
            iterator->second.levelID == levelID ||
            iterator->second.alternateLevelID == levelID
        ) {
            iterator = s_mapCache.erase(iterator);
        } else {
            ++iterator;
        }
    }
    s_notListedCache.insert(levelID);
}

std::optional<MapInfo> ApiClient::cachedMap(int levelID, int accountID) {
    auto const found = s_mapCache.find(mapCacheKey(levelID, accountID));
    if (found == s_mapCache.end()) return std::nullopt;
    return found->second;
}

bool ApiClient::isKnownNotListed(int levelID) {
    return s_notListedCache.contains(levelID);
}

cocos2d::ccColor3B ApiClient::ratingColor(std::string rating) {
    rating = trim(std::move(rating));
    if (rating == "20") rating = "20.0";

    static std::unordered_map<std::string, int> const colors {
        {"Tiny", 0xff6fff},
        {"0", 0xe8eaed},
        {"1", 0x0099ff},
        {"2", 0x00bbff},
        {"3", 0x00ddff},
        {"4", 0x00ffff},
        {"5", 0x00ffaa},
        {"6", 0x00ff00},
        {"7", 0x66ff00},
        {"8", 0x99ff00},
        {"9", 0xccff00},
        {"10", 0xffff00},
        {"11", 0xffdd00},
        {"12", 0xffcc00},
        {"13", 0xffaa00},
        {"14", 0xff8800},
        {"15", 0xff6600},
        {"16", 0xff4400},
        {"17", 0xff0000},
        {"18", 0xcc0000},
        {"18+", 0xa61c00},
        {"19", 0x660000},
        {"19+", 0x460c00},
        {"20.0", 0x360900},
        {"20.1", 0x240600},
        {"20.2", 0x130400},
        {"20.3", 0x000000},
        {"20.4", 0x0a031f},
        {"20.5", 0x11072d},
        {"20.6", 0x180b3b},
        {"20.7", 0x180b3b},
        {"20.8", 0x261358},
        {"20.9", 0x2d1766},
        {"21", 0x351c75},
        {"21+", 0x4511c9},
        {"-1", 0x4c1130},
        {"-2", 0x434343},
        {"UnVF", 0x4f71a3},
    };

    auto const found = colors.find(rating);
    return colorFromHex(found == colors.end() ? 0xffffff : found->second);
}

} // namespace corum
