#pragma once

#include "domain/EnvironmentPolicy.hpp"
#include "domain/ServerClock.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
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

struct RoundSummaryView {
    int roundNumber = 0;
    std::string mapTitle;
    std::string difficulty;
    double scoreA = 0.0;
    double scoreB = 0.0;
    int clearsA = 0;
    int clearsB = 0;
    std::string result;
};

struct DeathmatchSummaryView {
    int sequence = 0;
    std::string mapTitle;
    std::string difficulty;
    double scoreA = 0.0;
    double scoreB = 0.0;
    std::string winnerSide;
};

struct HistoryMatchView {
    std::string matchId;
    std::string finishedAt;
    std::string side;
    std::string opponentName;
    std::string effectiveTier;
    std::string winnerSide;
    int roundWinsA = 0;
    int roundWinsB = 0;
    std::optional<int> ownMmrDelta;
    std::optional<int> ownRatingAfter;
    std::vector<RoundSummaryView> rounds;
    std::vector<DeathmatchSummaryView> deathmatches;
};

struct MatchView {
    std::string matchId;
    std::string state;
    std::string side;
    std::string effectiveTier;
    std::string banner;
    std::string deadlineAt;
    std::string playerAName;
    std::string playerBName;
    std::string playerATier;
    std::string playerBTier;
    int playerAScore = 0;
    int playerBScore = 0;
    std::string opponentName;
    std::string winnerSide;
    std::string cancellationReason;
    std::int64_t stateVersion = 0;
    int roundNumber = 0;
    double scoreA = 0.0;
    double scoreB = 0.0;
    double committedScoreA = 0.0;
    double committedScoreB = 0.0;
    int clearsA = 0;
    int clearsB = 0;
    int roundWinsA = 0;
    int roundWinsB = 0;
    int deathmatchSequence = 0;
    int deathmatchAttemptsUsedA = 0;
    int deathmatchAttemptsUsedB = 0;
    int deathmatchAttemptsCompletedA = 0;
    int deathmatchAttemptsCompletedB = 0;
    bool readyA = false;
    bool readyB = false;
    bool ownBanConfirmed = false;
    std::string ownBanCanonicalLevelId;
    std::optional<int> ownMmrDelta;
    std::optional<int> ownRatingAfter;
    std::optional<int> mmrDeltaA;
    std::optional<int> mmrDeltaB;
    std::optional<int> ratingAfterA;
    std::optional<int> ratingAfterB;
    std::string profileBeforeTierA;
    std::string profileBeforeTierB;
    std::string profileAfterTierA;
    std::string profileAfterTierB;
    std::optional<int> profileAfterScoreA;
    std::optional<int> profileAfterScoreB;
    bool spectatorActive = false;
    std::optional<int> spectatorCurrentProgress;
    std::string spectatorOpponentName;
    std::vector<RankedMapView> candidateMaps;
    std::vector<RoundSummaryView> rounds;
    std::vector<DeathmatchSummaryView> deathmatches;
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

struct RankedAudioResourceView {
    std::string key;
    std::string label;
    int songId = 0;
    double startSeconds = 0.0;
    bool loop = true;
};

struct RankedAudioConfigView {
    bool enabled = false;
    double fadeInSeconds = 0.8;
    double fadeOutSeconds = 0.6;
    std::vector<RankedAudioResourceView> resources;
};

struct RankedUiTransitionView {
    double fadeInSeconds = 0.24;
    double fadeOutSeconds = 0.18;
};

struct RankedClientPresentationView {
    RankedAudioConfigView audio;
    RankedUiTransitionView ui;
};

struct RuntimeView {
    RuntimeStage stage = RuntimeStage::Idle;
    std::string status;
    std::string error;
    std::string profileTier;
    int profileScore = 0;
    int placementGames = 0;
    int placementGamesRequired = 0;
    std::uint64_t revision = 0;
    RankedClientPresentationView client;
    MatchView match;
    bool historyLoading = false;
    std::string historyError;
    std::vector<HistoryMatchView> history;
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
    void reportMapDownloadFailure();
    void dismissMatch();
    void queueAgain();
    void fetchHistory();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    // Primary API: password travels inside the options object.
    void startDebugBotMatch(DebugBotMatchOptions options);
    // Compatibility overload for alpha.4 callers that still pass password separately.
    // Keep this through alpha.5 so stale DebugBotPopup layouts compile on every target.
    void startDebugBotMatch(std::string const& password, DebugBotMatchOptions options);
#endif

    [[nodiscard]] RuntimeView const& view() const;
    [[nodiscard]] std::optional<std::int64_t> deadlineSeconds() const;
    [[nodiscard]] std::optional<std::int64_t> deadlineMillis() const;
    [[nodiscard]] bool canTrackLevel(int levelId) const;
    // Snapshot the revealed server map immediately before LevelInfoLayer enters
    // PlayLayer. This decouples gameplay attempt capture from later poll/UI
    // refresh timing while the server remains authoritative for the match state.
    [[nodiscard]] bool armCurrentLevelForGameplay();
    [[nodiscard]] std::optional<double> gameplayQualifyingPercent(int levelId) const;
    [[nodiscard]] bool isGameplayLevel(int levelId) const;
    [[nodiscard]] bool isSpectating() const;
    [[nodiscard]] int currentLevelId() const;
    [[nodiscard]] bool canEnterCurrentLevel() const;
    [[nodiscard]] bool hasLocalAttemptInFlight() const;
    // Reserve one *visual* Death Match attempt before Geometry Dash starts it.
    // This client-side budget closes the network-ack race that could otherwise
    // let a fourth visual attempt begin before the server had acknowledged the
    // third attempt end. The server remains authoritative and independently
    // rejects attempt 4+.
    [[nodiscard]] bool reserveDeathmatchVisualAttempt();
    [[nodiscard]] int localDeathmatchVisualAttemptsUsed() const;
    [[nodiscard]] double localDisplayScore(double progressPercent, std::optional<double> qualifyingPercentOverride = std::nullopt) const;
    [[nodiscard]] int localDisplayClears() const;
    void setSongBypassAllowed(bool allowed);
    [[nodiscard]] bool songBypassAllowed() const;

    [[nodiscard]] bool reportAttemptStart(int levelId);
    void reportAttemptProgress(int levelId, double progressPercent);
    [[nodiscard]] bool reportAttemptEnd(int levelId, double progressPercent, bool cleared, std::optional<double> qualifyingPercentOverride = std::nullopt);

private:
    struct PendingStart {
        int levelId = 0;
        std::string eventId;
        std::string clientStartedAt;
        std::string contextKey;
    };

    struct PendingEnd {
        int levelId = 0;
        double progressPercent = 0.0;
        bool cleared = false;
        std::string eventId;
        std::string clientEndedAt;
        std::string contextKey;
        double optimisticScore = 0.0;
        int optimisticClear = 0;
    };

    // Visual attempts can finish faster than HTTP start/end acknowledgements.
    // Keep every observed attempt in order so a fast reset can never overwrite
    // the previous attempt's score/Clear event.
    struct QueuedAttempt {
        PendingStart start;
        std::optional<PendingEnd> end;
    };

    RankedRuntime() = default;

    void fetchConfig();
    void createSession();
    void pollQueue();
    void pollMatch();
    void parseMatchState(matjson::Value const& root);
    void applyAttemptSnapshot(matjson::Value const& root);
    void promoteQueuedAttempt();
    void flushAttemptEvents();
    void sendAttemptStartIntent(PendingStart const& start);
    void flushAttemptStartIntents();
    void sendAttemptStart();
    void sendAttemptEnd();
    void flushProgressTelemetry();
    void sendAttemptProgress();
    void cleanupAttemptTransportIfIdle();
    void abandonFinalizedAttemptTransport();
    [[nodiscard]] bool hasLocalOpenAttempt() const;
    [[nodiscard]] bool canFinishTrackedLevel(int levelId) const;
    [[nodiscard]] std::string currentAttemptContextKey() const;
    [[nodiscard]] double optimisticScoreForContext(std::string const& contextKey) const;
    [[nodiscard]] int optimisticClearsForContext(std::string const& contextKey) const;
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
    int m_attemptLevelId = 0;
    std::string m_attemptContextKey;
    std::optional<RankedMapView> m_gameplayMap;
    std::string m_gameplayMatchId;
    std::string m_gameplayStartContextKey;
    std::string m_gameplayStartedAt;
    bool m_gameplayStartEligible = false;
    bool m_gameplayStartNeedsIntent = false;
    std::optional<PendingStart> m_pendingStart;
    std::optional<PendingEnd> m_pendingEnd;
    std::deque<QueuedAttempt> m_attemptBacklog;
    std::deque<PendingStart> m_attemptIntentBacklog;
    std::optional<int> m_pendingProgress;
    std::chrono::steady_clock::time_point m_nextPollAt {};
    std::chrono::steady_clock::time_point m_nextAttemptRetryAt {};
    std::chrono::steady_clock::time_point m_nextIntentRetryAt {};
    std::chrono::steady_clock::time_point m_nextProgressAt {};
    int m_lastSubmittedProgress = -1;
    int m_localDeathmatchSequence = 0;
    int m_localDeathmatchVisualAttempts = 0;
    std::uint64_t m_eventSequence = 0;
    bool m_controlBusy = false;
    bool m_pollBusy = false;
    bool m_attemptBusy = false;
    bool m_attemptIntentBusy = false;
    bool m_progressBusy = false;
    bool m_abandonAttemptTransportWhenIdle = false;
    bool m_songBypassAllowed = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_controlRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_pollRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptIntentRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_progressRequest;
};

char const* stageName(RuntimeStage stage);

} // namespace corum::ranked
