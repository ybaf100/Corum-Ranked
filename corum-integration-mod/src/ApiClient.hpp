#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <optional>
#include <string>
#include <vector>

namespace corum {

struct MapInfo {
    int rank = 0;
    int levelID = 0;
    int alternateLevelID = 0;
    std::string tier;
    std::string title;
    std::string rating;
    std::string length;
    std::string creator;
    std::string verifier;
    double minimumRecord = 100.0;
    bool hasFrozenScore = false;
    double frozenScore = 0.0;
    int serverPercent = 0;
    int initialPercent = 0;
    int registeredRank = 0;
    double registeredMinimumRecord = 100.0;
};

enum class LookupStatus {
    Listed,
    NotListed,
    Error,
};

enum class StartupStatus {
    NotStarted,
    Initializing,
    Ready,
    Failed,
};

struct LookupResult {
    LookupStatus status = LookupStatus::Error;
    std::optional<MapInfo> map;
    std::string errorCode;
    std::string message;
};

struct CatalogResult {
    bool ok = false;
    std::vector<MapInfo> maps;
    std::string evidenceGeneration;
    struct ClientPolicy {
        bool present = false;
        bool enforcementEnabled = false;
        bool supported = true;
        std::string platform;
        std::string currentVersion;
        std::string minimumSupportedVersion;
        std::string latestVersion;
        std::string updateURL;
    } clientPolicy;
    std::string message;
};

class ApiClient {
public:
    static bool isConfigured();
    static std::string baseURL();
    static std::string catalogURL();
    static std::string playerRecordsURL(int accountID);
    static std::string mapURL(int levelID, int accountID = 0);
    static std::string userAgent();

    static void initializeSession();
    static StartupStatus startupStatus();
    static bool startupFinished();
    static bool startupReady();
    static bool isOutdated();
    static bool submissionAllowed();
    static std::string minimumSupportedVersion();
    static std::string latestVersion();
    static std::string updateURL();
    static void showStartupOutdatedWarning();
    static void showUpdateRequiredWarning();
    static std::optional<MapInfo> startupMap(int levelID);
    static std::string evidenceGeneration();
    static std::vector<std::string> const& loadedMods();

    static CatalogResult parseCatalogResponse(
        geode::utils::web::WebResponse& response
    );
    static LookupResult parseMapResponse(geode::utils::web::WebResponse& response);

    static void cacheMap(MapInfo const& map, int accountID = 0);
    static void clearCachedMap(int levelID, int accountID = 0);
    static void cacheNotListed(int levelID);
    static std::optional<MapInfo> cachedMap(int levelID, int accountID = 0);
    static bool isKnownNotListed(int levelID);

    static cocos2d::ccColor3B ratingColor(std::string rating);
};

} // namespace corum
