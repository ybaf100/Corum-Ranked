#pragma once
#include "domain/DeathFeed.hpp"

#include "domain/EnvironmentPolicy.hpp"
#include "domain/ServerClock.hpp"
#include "domain/MatchPauseBudget.hpp"
#include "domain/GameplayConcession.hpp"
#include "domain/RankAssetPolicy.hpp"
#include "domain/ResultScorePresentation.hpp"
#include "domain/MapMetadata.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace corum::ranked {

enum class RuntimeStage {
    Idle,
    NotConfigured,
    Loading,
    AwaitingUpdate,
    AwaitingResources,
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
    MapMetadata metadata {};
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

struct PromotionSeriesView {
    bool active = false;
    std::string sourceTier;
    std::string targetTier;
    int cutoff = 0;
    int gamesRequired = 0;
    int winsRequired = 0;
    int winsRemaining = 0;
    std::vector<std::string> results;
};

struct AchievementTitleView {
    bool active = false;
    std::string achievementId;
    std::optional<int> level;
    std::string displayLabel;
    std::string nameKo;
    std::string nameEn;
    std::string displayColor;
    std::string rarity;
};

struct AchievementUnlockNoticeView {
    std::string achievementId;
    std::string type;
    std::optional<int> level;
    std::string displayLabel;
    bool maxed = false;
    int rewardAp = 0;
    std::string nameKo;
    std::string nameEn;
    std::string rarity;
};

struct AchievementLevelView {
    int level = 0;
    std::string grade;
    std::string displayLabel;
    std::string displayColor;
    int threshold = 0;
    int ap = 0;
};

struct AchievementRewardView {
    std::optional<int> level;
    int rewardAp = 0;
    std::string unlockedAt;
    std::string claimedAt;
};

struct AchievementCardView {
    std::string id;
    std::string type;
    std::string nameKo;
    std::string nameEn;
    std::string descriptionKo;
    std::string descriptionEn;
    std::string rarity;
    bool titleEnabled = false;
    bool unlocked = false;
    bool claimed = false;
    int rewardAp = 0;
    int currentLevel = 0;
    std::string displayLabel;
    int metricValue = 0;
    std::optional<int> nextThreshold;
    double gauge = 0.0;
    bool maxed = false;
    int claimableRewards = 0;
    int claimableAp = 0;
    std::vector<AchievementLevelView> levels;
    std::vector<AchievementRewardView> unlocks;
};

struct AchievementView {
    bool loaded = false;
    bool loading = false;
    std::string error;
    int totalAp = 0;
    int unclaimedRewardCount = 0;
    AchievementTitleView equippedTitle;
    std::vector<AchievementCardView> cards;
};

struct ForumRankView {
    std::string provider, externalName, status = "PENDING", lastSuccessAt;
    std::optional<int> rank;
};
struct ForumRecordsView {
    bool loaded = false, loading = false;
    std::string error;
    std::string refreshAvailableAt;
    std::vector<ForumRankView> records;
};
struct PlayerInfoView {
    int accountId = 0;
    std::string username, side;
    bool botReference = false;
    ForumRecordsView forum;
};

struct RankedProfileView {
    std::string displayedTier = "UNRANKED";
    std::optional<int> visibleRankedScore;
    int placementGames = 0;
    int placementGamesRequired = 0;
    std::vector<std::string> placementResults;
    int resultStreak = 0;
    int wins = 0, losses = 0;
    PromotionSeriesView promotion;
    bool demotionWarning = false;
    AchievementTitleView equippedTitle;
};

struct RatingEventView {
    bool placementCompleted = false;
    bool promotionStarted = false;
    bool promotionSucceeded = false;
    bool promotionFailed = false;
    bool demoted = false;
};

struct PlacementStageResultView {
    int stageIndex = 0;
    int pool = 0;
    std::vector<double> mapScores;
    std::vector<std::string> mapGrades;
    double stageScore = 0.0;
    std::string stageGrade = "D";
    int poolDelta = 0;
    int nextPool = 1;
};

struct PlacementFinalResultView {
    bool placed = false;
    bool skipped = false;
    int visibleRankedScore = 0;
    std::string displayedTier = "UNRANKED";
    std::string finalizedAt;
    std::vector<AchievementUnlockNoticeView> achievementUnlocks;
};

struct PlacementAttemptView {
    std::string attemptId;
    double progressPercent = 0.0;
    bool cleared = false;
    double awardedScore = 0.0;
};

struct PlacementView {
    bool required = false;
    bool active = false;
    std::string runId;
    std::string state;
    std::int64_t stateVersion = 0;
    int startingPool = 0;
    int currentStageIndex = 0;
    int currentMapIndex = 0;
    int totalStages = 5;
    int mapsPerStage = 2;
    double currentMapScore = 0.0;
    double provisionalMapScore = 0.0;
    std::string currentMapGrade = "D";
    std::array<double, 4> mapGradeThresholds {150, 250, 350, 425};
    std::vector<double> currentStageMapScores;
    std::vector<std::string> currentStageMapGrades;
    int currentMapClears = 0;
    std::string currentMapNormalEndAt;
    std::string currentMapFinalWindowEndAt;
    std::string activeAttemptId;
    std::vector<PlacementAttemptView> currentMapTopAttempts;
    std::vector<RankedMapView> referenceMaps;
    std::vector<int> stagePools;
    std::vector<PlacementStageResultView> stageResults;
    PlacementFinalResultView finalResult;
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
    std::optional<int> ownRankedScoreDelta;
    std::optional<int> ownRankedScoreAfter;
    std::vector<RoundSummaryView> rounds;
    std::vector<DeathmatchSummaryView> deathmatches;
};

struct MatchView {
    std::vector<DeathFeedEvent> deathLog;
    bool decisiveRoundEnded = false;
    std::string matchType;
    bool placement = false;
    std::string matchId;
    std::string state;
    std::string side;
    std::string effectiveTier;
    std::string banner;
    std::string deadlineAt;
    // Server-persisted native-scene barrier, independent of resource READY.
    struct Entry {
        int roundNumber = 0, deathmatchSequence = 0, levelId = 0;
        bool enteredA = false, enteredB = false;
        std::string startsAt;
    } entry;
    std::string playerAName;
    std::string playerBName;
    int playerAAccountId = 0;
    int playerBAccountId = 0;
    std::string playerATier;
    std::string playerBTier;
    int playerAScore = 0;
    int playerBScore = 0;
    bool playerAScoreVisible = false;
    bool playerBScoreVisible = false;
    RankedProfileView playerAProfile;
    RankedProfileView playerBProfile;
    std::string opponentName;
    std::string winnerSide;
    std::string perfectWinSide;
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
    std::optional<int> ownRankedScoreDelta;
    std::optional<int> ownRankedScoreAfter;
    std::optional<int> rankedScoreDeltaA;
    std::optional<int> rankedScoreDeltaB;
    std::optional<int> rankedScoreAfterA;
    std::optional<int> rankedScoreAfterB;
    std::string profileBeforeTierA;
    std::string profileBeforeTierB;
    std::string profileAfterTierA;
    std::string profileAfterTierB;
    std::optional<int> profileAfterScoreA;
    std::optional<int> profileAfterScoreB;
    RankedProfileView profileBeforeA;
    RankedProfileView profileBeforeB;
    RankedProfileView profileAfterA;
    RankedProfileView profileAfterB;
    RatingEventView ratingEventA;
    RatingEventView ratingEventB;
    ResultScoreView scorePresentation;
    std::vector<AchievementUnlockNoticeView> achievementUnlocks;
    bool spectatorActive = false;
    bool spectatorLiveEnabled = true;
    std::int64_t spectatorLiveSettingVersion = 0;
    std::string surrenderScope, surrenderSide;
    int surrenderRound = 0, surrenderDeathmatch = 0;
    std::optional<int> opponentProgress;
    bool opponentFinished = false;
    bool opponentPractice = false;
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
    double volume = 1.0;
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
    RankAssetMetadata assets;
};

struct EnvironmentModIssueView {
    std::string id;
    std::string displayName;
    std::string installedVersion;
    std::optional<std::string> minimumVersion;
    std::optional<std::string> maximumVersion;
};

struct EnvironmentWarningView {
    bool active = false;
    std::vector<EnvironmentModIssueView> unauthorizedMods;
    std::vector<EnvironmentModIssueView> missingRequiredMods;
    std::vector<EnvironmentModIssueView> versionViolations;
    std::vector<std::string> cbfIssues;
};

struct RuntimeView {
    RuntimeStage stage = RuntimeStage::Idle;
    std::string status;
    std::string error;
    bool accountActiveElsewhere = false;
    std::string profileTier;
    int profileScore = 0;
    int profileWins = 0, profileLosses = 0;
    StandingView standing;
    bool profileScoreVisible = false;
    int placementGames = 0;
    int placementGamesRequired = 0;
    std::vector<std::string> placementResults;
    int resultStreak = 0;
    PromotionSeriesView promotion;
    bool demotionWarning = false;
    std::uint64_t revision = 0;
    RankedClientPresentationView client;
    EnvironmentWarningView environmentWarning;
    PlacementView placement;
    MatchView match;
    bool historyLoading = false;
    std::string historyError;
    std::vector<HistoryMatchView> history;
    AchievementView achievements;
    ForumRecordsView forumRecords;
    PlayerInfoView playerInfo;
};

class RankedRuntime final {
public:
    static RankedRuntime& get();

    void begin();
    void finishAdmission();
    [[nodiscard]] std::int64_t serverNowMillis() const;
    void retireDecisiveAttempt();
    void takeOverSession();
    void leaveRanked();
    void updateSessionHeartbeat();
    void recheckEnvironment();
    void tick();
    void joinQueue();
    void startPlacement(int startingPool, int stageCount);
    void skipPlacement();
    void continuePlacement();
    void pausePlacement();
    void leaveQueue();
    void submitReady();
    void submitBan(std::optional<std::string> canonicalLevelId);
    void reportMapDownloadFailure();
    enum class SurrenderStatus { Idle, WaitingAttempt, Sending, Retrying, Accepted, Rejected };
    void surrenderCurrentMatch(bool wholeMatch);
    void skipPlacementMap(int levelId, double progressPercent);
    [[nodiscard]] bool placementMapSkipPending() const { return m_pendingPlacementMapSkip.has_value(); }
    [[nodiscard]] SurrenderStatus placementMapSkipStatus() const { return m_placementMapSkipStatus; }
    [[nodiscard]] bool placementMapSkipped(std::string const& runId, int stageIndex, int mapIndex) const;
    [[nodiscard]] std::string placementDisplayGrade(double score) const;
    [[nodiscard]] bool surrenderPending() const { return m_pendingSurrender.has_value(); }
    [[nodiscard]] SurrenderStatus surrenderStatus() const { return m_surrenderStatus; }
    [[nodiscard]] bool gameplayConceded(int round, int deathmatch) const;
    [[nodiscard]] bool opponentConceded(int round, int deathmatch) const;
    std::string beginMatchGameplayPause(int levelId);
    void endMatchGameplayPause(std::string const& leaseId);
    void syncMatchGameplayPause(bool nativePaused);
    [[nodiscard]] double matchPauseRemainingMs() const;
    void dismissMatch();
    void queueAgain();
    void fetchHistory();
    void fetchStanding();
    [[nodiscard]] std::string leaderboardUrl(int offset, int limit) const;
    void fetchAchievements();
    void fetchPlayerInfo(std::string const& side);
    void fetchForumRecords(bool refresh = false);
    void applyAudioVolumes(matjson::Value const& volumes);
    [[nodiscard]] std::int64_t forumRefreshRemainingMs() const;
    void claimAchievement(std::string const& achievementId);
    void equipAchievementTitle(std::string const& achievementId, std::optional<int> level);
    void clearAchievementTitle();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    // Primary API: password travels inside the options object.
    void startDebugBotMatch(DebugBotMatchOptions options);
    // Compatibility overload for alpha.4 callers that still pass password separately.
    // Keep this through alpha.5 so stale DebugBotPopup layouts compile on every target.
    void startDebugBotMatch(std::string const& password, DebugBotMatchOptions options);
#endif

    [[nodiscard]] RuntimeView const& view() const;
    [[nodiscard]] bool isPlacement() const;
    [[nodiscard]] std::optional<std::int64_t> deadlineSeconds() const;
    [[nodiscard]] std::optional<std::int64_t> deadlineMillis() const;
    [[nodiscard]] std::optional<std::int64_t> signedDeadlineMillis() const;
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
    // Separate read-only entry authority for reconnect/re-entry. This preserves
    // the canonical LevelInfo preparation path without arming a new Attempt.
    [[nodiscard]] bool canEnterCurrentLevelAsSpectator() const;
    [[nodiscard]] bool canEnterNativeRoundScene() const;
    [[nodiscard]] bool nativeEntryStartGranted() const;
    void noteNativeRoundSceneEntered(int levelId);
    [[nodiscard]] std::optional<std::int64_t> nativeEntryCountdownMillis() const;
    [[nodiscard]] bool hasLocalAttemptInFlight() const;
    [[nodiscard]] bool hasLocalGameplayAuthority() const;
    void setSpectatorLocalTransportBlocked(void const* owner, bool blocked);
    [[nodiscard]] int localDeathmatchVisualAttemptsUsed() const;
    [[nodiscard]] double localDisplayScore(double progressPercent, std::optional<double> qualifyingPercentOverride = std::nullopt) const;
    [[nodiscard]] int localDisplayClears() const;
    void setSongBypassAllowed(bool allowed);
    [[nodiscard]] bool songBypassAllowed() const;

    // Read-only credentials for the isolated ephemeral spectator relay. They are
    // deliberately not mutable and are never accepted as scoring authority.
    [[nodiscard]] std::string const& spectatorLiveSessionToken() const { return m_sessionToken; }
    [[nodiscard]] std::string const& spectatorLiveMatchToken() const { return m_matchToken; }
    [[nodiscard]] std::string const& spectatorLiveAttemptId() const {
        static std::string const empty;
        // A queued visual attempt has not received its server ID yet. Never
        // label its pose with an older journal entry still awaiting End ACK.
        return !m_pendingStart && m_attemptBacklog.empty() ? m_attemptId : empty;
    }

    [[nodiscard]] bool reportAttemptStart(int levelId);
    void reportPracticePresence(bool practice);
    void reportAttemptProgress(int levelId, double progressPercent);
    [[nodiscard]] bool reportAttemptEnd(int levelId, double progressPercent, bool cleared, std::optional<double> qualifyingPercentOverride = std::nullopt, bool nativeDeath = false);

private:
    void const* m_spectatorLocalTransportOwner = nullptr;
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
        bool nativeDeath = false;
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
    void recordDeath(DeathFeedEvent event, std::string const& contextKey);
    bool handleRequiredUpdate(geode::utils::web::WebResponse const& response);
    void createSession(bool takeOver = false);
    void fetchActivePlacement();
    void invalidatePollRequest();
    void invalidateAttemptRequest();
    void pollQueue();
    void pollMatch();
    void parseMatchState(matjson::Value const& root);
    void parsePlacementState(matjson::Value const& root);
    void parseAchievementState(matjson::Value const& root);
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
    [[nodiscard]] bool hasLocalOpenAttempt() const;
    [[nodiscard]] bool canFinishTrackedLevel(int levelId) const;
    [[nodiscard]] bool attemptStartWindowOpen() const;
    [[nodiscard]] std::string currentAttemptContextKey() const;
    [[nodiscard]] double optimisticScoreForContext(std::string const& contextKey) const;
    [[nodiscard]] int optimisticClearsForContext(std::string const& contextKey) const;
    void setStage(RuntimeStage stage, std::string status, std::string error = {});
    void setTransientError(std::string error);
    void showEnvironmentWarning(EnvironmentDecision const& decision, bool blockRuntime);
    [[nodiscard]] EnvironmentModIssueView environmentModIssue(std::string const& id) const;
    void observeServerNow(matjson::Value const& root);
    [[nodiscard]] std::vector<InstalledModSnapshot> captureInstalledMods() const;
    [[nodiscard]] matjson::Value installedModsJson() const;
    [[nodiscard]] std::string endpoint(std::string const& path) const;
    [[nodiscard]] std::string newEventId(std::string_view kind);

    RuntimeView m_view;
    EnvironmentPolicy m_environmentPolicy;
    std::vector<InstalledModSnapshot> m_installedMods;
    ServerClock m_serverClock;
    std::string m_nativeEntryKey;
    std::uint64_t m_nativeEntryGeneration = 0;
    bool m_nativeEntryBusy = false;
    std::chrono::steady_clock::time_point m_nextNativeEntryAt {};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_nativeEntryRequest;
    std::string m_sessionToken;
    std::string m_clientInstanceId;
    bool m_rankedOpen = false, m_sessionRejected = false, m_sessionHeartbeatBusy = false;
    std::uint64_t m_sessionGeneration = 0;
    std::chrono::steady_clock::time_point m_nextSessionHeartbeatAt {}, m_sessionLeaseDeadline {};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_sessionHeartbeatRequest, m_sessionCloseRequest;
    std::string m_matchToken;
    std::string m_attemptId;
    // Attempt UUIDs acknowledged closed by Placement End. A concurrent /state
    // response may predate that ACK even when stateVersion is unchanged.
    std::unordered_set<std::string> m_acknowledgedPlacementEnds;
    int m_attemptLevelId = 0;
    std::string m_attemptContextKey;
    std::string m_deathFeedContext;
    std::string m_rejectedRequiredVersion;
    std::optional<RankedMapView> m_gameplayMap;
    std::string m_gameplayMatchId;
    std::string m_gameplayStartContextKey;
    std::string m_gameplayStartedAt;
    std::string m_lastAchievementNoticeContext;
    bool m_gameplayStartEligible = false;
    std::optional<PendingStart> m_pendingStart;
    std::optional<PendingEnd> m_pendingEnd;
    std::deque<QueuedAttempt> m_attemptBacklog;
    std::deque<PendingStart> m_attemptIntentBacklog;
    std::optional<int> m_pendingProgress;
    std::chrono::steady_clock::time_point m_nextPollAt {};
    std::chrono::steady_clock::time_point m_nextAttemptRetryAt {};
    std::chrono::steady_clock::time_point m_attemptRequestStartedAt {};
    std::chrono::steady_clock::time_point m_nextIntentRetryAt {};
    std::chrono::steady_clock::time_point m_nextProgressAt {};
    int m_lastSubmittedProgress = -1;
    int m_localDeathmatchSequence = 0;
    int m_localDeathmatchVisualAttempts = 0;
    std::uint64_t m_eventSequence = 0;
    std::uint64_t m_attemptRequestGeneration = 0;
    std::uint64_t m_attemptIntentRequestGeneration = 0;
    std::uint64_t m_progressRequestGeneration = 0;
    std::uint64_t m_pollRequestGeneration = 0;
    bool m_controlBusy = false;
    bool m_recoveringMatch = false;
    MatchPauseBudget m_matchPauseBudget;
    std::string m_pauseMatchId, m_pauseLeaseId;
    bool m_pauseDesired = false, m_pauseNeedsSync = false, m_pauseBusy = false;
    std::chrono::steady_clock::time_point m_nextPauseAt {}, m_nextSurrenderAt {};
    // matjson::Value() is an object, NOT an empty/absent request. Presence must
    // be explicit, otherwise a fresh runtime silently rejects every surrender.
    std::optional<matjson::Value> m_pendingSurrender;
    std::string m_surrenderMatchId;
    std::uint64_t m_surrenderRequestGeneration = 0;
    SurrenderStatus m_surrenderStatus = SurrenderStatus::Idle;
    bool m_surrenderBusy = false;
    std::optional<matjson::Value> m_pendingPlacementMapSkip;
    std::string m_placementMapSkipRunId;
    std::unordered_set<std::string> m_skippedPlacementMaps;
    std::uint64_t m_placementMapSkipGeneration = 0;
    bool m_placementMapSkipBusy = false;
    SurrenderStatus m_placementMapSkipStatus = SurrenderStatus::Idle;
    std::chrono::steady_clock::time_point m_nextPlacementMapSkipAt {};
    void flushPlacementMapSkip();
    void resetPlacementMapSkip();
    GameplayConcessionJournal m_concessions;
    void observeGameplayConcession(std::string const& scope, int round, int deathmatch, std::string const& side);
    void flushGameplayControls();
    void resetGameplayControls();
    void retireConcededAttemptTransport();
    bool m_pollBusy = false;
    bool m_attemptBusy = false;
    bool m_attemptIntentBusy = false;
    bool m_progressBusy = false;
    bool m_songBypassAllowed = false;
    bool m_placementResultAcknowledged = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_controlRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_pollRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptIntentRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_progressRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_presenceRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_pauseRequest, m_surrenderRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_placementMapSkipRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_playerInfoRequest, m_forumRecordsRequest;
    std::uint64_t m_playerInfoGeneration = 0, m_forumRecordsGeneration = 0;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_standingRequest;
    std::uint64_t m_standingGeneration = 0;
    bool m_standingBusy = false;
    std::chrono::steady_clock::time_point m_nextStandingAt {};
};

char const* stageName(RuntimeStage stage);

} // namespace corum::ranked

