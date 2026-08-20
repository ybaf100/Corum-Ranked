#pragma once

#include "domain/EnvironmentPolicy.hpp"
#include "domain/ServerClock.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {

enum class RuntimeStage {
    Idle,
    NotConfigured,
    Loading,
    Blocked,
    Ready,
    JoiningQueue,
    Queued,
    Matched,
    Error,
};

struct RankedMapView {
    int levelId = 0;
    std::string canonicalLevelId;
    std::string alternateLevelId;
    std::string playableLevelId;
    std::string title;
    std::string creator;
    std::string difficulty;
    int pool = 0;
    double qualifyingPercent = 100.0;
};

struct MatchView {
    std::string matchId;
    std::string state;
    std::string side;
    std::string effectiveTier;
    std::string banner;
    std::string deadlineAt;
    std::string opponentName;
    std::string winnerSide;
    std::int64_t stateVersion = 0;
    int roundNumber = 0;
    int scoreA = 0;
    int scoreB = 0;
    int clearsA = 0;
    int clearsB = 0;
    int deathmatchSequence = 0;
    std::optional<int> ownMmrDelta;
    std::optional<int> ownRatingAfter;
    bool spectatorActive = false;
    std::optional<int> spectatorCurrentProgress;
    std::string spectatorOpponentName;
    std::vector<RankedMapView> candidateMaps;
    std::optional<RankedMapView> currentMap;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    bool debug = false;
#endif
};

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
struct DebugBotMatchOptions {
    std::string password;
    std::string difficulty;
    std::string scenario;
    std::string botBan;
    bool sendDiscordEvents = false;
};
#endif

struct RuntimeView {
    RuntimeStage stage = RuntimeStage::Idle;
    std::string status;
    std::string error;
    std::string profileTier;
    int placementGames = 0;
    int placementGamesRequired = 0;
    std::uint64_t revision = 0;
    MatchView match;
};

class RankedRuntime final {
public:
    static RankedRuntime& get();

    void begin();
    void tick();
    void joinQueue();
    void leaveQueue();
    void submitReady();
    void submitBan(std::optional<std::string> canonicalLevelId);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void startDebugBotMatch(DebugBotMatchOptions options);
#endif

    [[nodiscard]] RuntimeView const& view() const;
    [[nodiscard]] std::optional<std::int64_t> deadlineSeconds() const;
    [[nodiscard]] std::optional<std::int64_t> deadlineMillis() const;
    [[nodiscard]] bool canTrackLevel(int levelId) const;
    [[nodiscard]] bool isSpectating() const;
    [[nodiscard]] int currentLevelId() const;

    void reportAttemptStart(int levelId);
    void reportAttemptProgress(int levelId, double progressPercent);
    void reportAttemptEnd(int levelId, double progressPercent, bool cleared);

private:
    struct PendingStart {
        int levelId = 0;
        std::string eventId;
    };

    struct PendingEnd {
        int levelId = 0;
        double progressPercent = 0.0;
        bool cleared = false;
        std::string eventId;
    };

    RankedRuntime() = default;

    void fetchConfig();
    void createSession();
    void pollQueue();
    void pollMatch();
    void parseMatchState(matjson::Value const& root);
    void flushAttemptEvents();
    void sendAttemptStart();
    void sendAttemptEnd();
    void flushProgressTelemetry();
    void sendAttemptProgress();
    void setStage(RuntimeStage stage, std::string status, std::string error = {});
    void setTransientError(std::string error);
    void observeServerNow(matjson::Value const& root);
    [[nodiscard]] std::vector<InstalledModSnapshot> captureInstalledMods() const;
    [[nodiscard]] matjson::Value installedModsJson() const;
    [[nodiscard]] std::string endpoint(std::string const& path) const;
    [[nodiscard]] std::string newEventId(std::string_view kind);

    RuntimeView m_view;
    EnvironmentPolicy m_environmentPolicy;
    std::vector<InstalledModSnapshot> m_installedMods;
    ServerClock m_serverClock;
    std::string m_serverURL;
    std::string m_sessionToken;
    std::string m_matchToken;
    std::string m_attemptId;
    std::optional<PendingStart> m_pendingStart;
    std::optional<PendingEnd> m_pendingEnd;
    std::optional<int> m_pendingProgress;
    std::chrono::steady_clock::time_point m_nextPollAt {};
    std::chrono::steady_clock::time_point m_nextAttemptRetryAt {};
    std::chrono::steady_clock::time_point m_nextProgressAt {};
    int m_lastSubmittedProgress = -1;
    std::uint64_t m_eventSequence = 0;
    bool m_controlBusy = false;
    bool m_pollBusy = false;
    bool m_attemptBusy = false;
    bool m_progressBusy = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_controlRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_pollRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_progressRequest;
};

char const* stageName(RuntimeStage stage);

} // namespace corum::ranked
