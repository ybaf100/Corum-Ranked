#include "RankedRuntime.hpp"
#include "SpectatorLiveRuntime.hpp"
#include "domain/SpectatorLocalSafety.hpp"
#include "domain/AttemptScoring.hpp"
#include "domain/RankedGameplayPolicy.hpp"
#include "domain/TransitionTrace.hpp"

#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>

using namespace geode::prelude;

namespace {

constexpr std::string_view kCbfId = "syzzi.click_between_frames";
constexpr std::string_view kDefaultRankedServerURL = "https://corum-ranked.onrender.com";
constexpr auto kAttemptRequestWatchdog = std::chrono::seconds(12);
constexpr std::string_view kKnownCbfSettings[] = {
    "soft-toggle",
    "click-on-steps",
    "physics-bypass",
};

std::int64_t localNowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string iso8601FromMillis(std::int64_t millis) {
    auto const seconds = static_cast<std::time_t>(millis / 1000);
    auto fractional = static_cast<int>(millis % 1000);
    if (fractional < 0) fractional += 1000;
    std::tm utc {};
#if defined(_WIN32)
    if (gmtime_s(&utc, &seconds) != 0) return {};
#else
    if (gmtime_r(&seconds, &utc) == nullptr) return {};
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << fractional << 'Z';
    return stream.str();
}

int placementGradeRank(std::string const& grade) {
    if (grade == "S") return 0;
    if (grade == "A") return 1;
    if (grade == "B") return 2;
    if (grade == "C") return 3;
    return 4;
}

bool successful(web::WebResponse const& response) {
    return response.code() >= 200 && response.code() < 300;
}

bool permanentClientError(int status) {
    // Request timeout, Too Early, and rate limiting are explicitly retryable.
    // Other 4xx responses describe a deterministic request/session rejection;
    // retrying the exact same FIFO event forever blocks every later attempt.
    return
        status >= 400 && status < 500 &&
        status != 408 && status != 425 && status != 429;
}

std::string responseError(web::WebResponse& response) {
    auto const root = response.json().unwrapOr(matjson::Value());
    if (root.isObject()) {
        auto const error = root["error"];
        if (error.isObject()) {
            auto message = error["message"].asString().unwrapOr("");
            if (!message.empty()) return message;
            auto code = error["code"].asString().unwrapOr("");
            if (!code.empty()) return code;
        }
        auto const message = root["message"].asString().unwrapOr("");
        if (!message.empty()) return message;
    }
    return fmt::format("HTTP {}", response.code());
}

corum::ranked::SettingValue jsonSetting(matjson::Value const& value) {
    if (value.isBool()) return value.asBool().unwrapOr(false);
    if (value.isNumber()) return value.asDouble().unwrapOr(0.0);
    return value.asString().unwrapOr("");
}

void writeSetting(matjson::Value& target, std::string const& key, corum::ranked::SettingValue const& value) {
    std::visit([&](auto const& item) { target[key] = item; }, value);
}

std::optional<corum::ranked::RankedMapView> parseMap(matjson::Value const& value) {
    if (!value.isObject()) return std::nullopt;
    auto const playableText = value["playableLevelId"].asString().unwrapOr(
        value["levelId"].asString().unwrapOr("")
    );
    auto levelId = numFromString<int>(playableText).unwrapOr(0);
    if (levelId <= 0) {
        levelId = static_cast<int>(value["playableLevelId"].asInt().unwrapOr(
            value["levelId"].asInt().unwrapOr(0)
        ));
    }
    if (levelId <= 0) return std::nullopt;
    return corum::ranked::RankedMapView {
        .levelId = levelId,
        .canonicalLevelId = value["canonicalLevelId"].asString().unwrapOr(""),
        .alternateLevelId = value["alternateLevelId"].asString().unwrapOr(""),
        .playableLevelId = playableText.empty() ? fmt::format("{}", levelId) : playableText,
        .title = value["title"].asString().unwrapOr("Unknown map"),
        .creator = value["creator"].asString().unwrapOr(""),
        .difficulty = value["difficulty"].asString().unwrapOr(""),
        .pool = static_cast<int>(value["pool"].asInt().unwrapOr(0)),
        .qualifyingPercent = value["qualifyingPercent"].asDouble().unwrapOr(100.0),
    };
}

std::vector<std::string> parseRatingMarks(matjson::Value const& value) {
    std::vector<std::string> marks;
    auto const array = value.asArray();
    if (array.isErr()) return marks;
    for (auto const& item : array.unwrap()) {
        auto const mark = item.asString().unwrapOr("");
        if (mark == "WIN" || mark == "LOSS") marks.push_back(mark);
    }
    return marks;
}

corum::ranked::AchievementTitleView parseAchievementTitle(matjson::Value const& value) {
    corum::ranked::AchievementTitleView title;
    if (!value.isObject()) return title;
    title.active = true;
    title.achievementId = value["achievementId"].asString().unwrapOr("");
    auto const level = value["level"].asInt();
    if (level.isOk()) title.level = static_cast<int>(level.unwrap());
    title.displayLabel = value["displayLabel"].asString().unwrapOr("");
    title.nameKo = value["nameKo"].asString().unwrapOr("");
    title.nameEn = value["nameEn"].asString().unwrapOr("");
    title.displayColor = value["displayColor"].asString().unwrapOr("");
    title.rarity = value["rarity"].asString().unwrapOr("");
    title.active = !title.achievementId.empty();
    return title;
}

corum::ranked::AchievementUnlockNoticeView parseAchievementNotice(matjson::Value const& value) {
    corum::ranked::AchievementUnlockNoticeView notice;
    if (!value.isObject()) return notice;
    notice.achievementId = value["achievementId"].asString().unwrapOr("");
    notice.type = value["type"].asString().unwrapOr("");
    auto const level = value["level"].asInt();
    if (level.isOk()) notice.level = static_cast<int>(level.unwrap());
    notice.displayLabel = value["displayLabel"].asString().unwrapOr("");
    notice.maxed = value["maxed"].asBool().unwrapOr(false);
    notice.rewardAp = static_cast<int>(value["rewardAp"].asInt().unwrapOr(0));
    notice.nameKo = value["nameKo"].asString().unwrapOr("");
    notice.nameEn = value["nameEn"].asString().unwrapOr("");
    notice.rarity = value["rarity"].asString().unwrapOr("");
    return notice;
}

corum::ranked::RankedProfileView parseRankedProfile(matjson::Value const& value) {
    corum::ranked::RankedProfileView profile;
    if (!value.isObject()) return profile;
    profile.displayedTier = value["displayedTier"].asString().unwrapOr("UNRANKED");
    auto const score = value["visibleRankedScore"].asInt();
    if (score.isOk()) profile.visibleRankedScore = static_cast<int>(score.unwrap());
    profile.placementGames = static_cast<int>(value["placementGames"].asInt().unwrapOr(0));
    profile.placementGamesRequired = static_cast<int>(
        value["placementGamesRequired"].asInt().unwrapOr(0)
    );
    profile.placementResults = parseRatingMarks(value["placementResults"]);
    profile.resultStreak = static_cast<int>(value["resultStreak"].asInt().unwrapOr(0));
    profile.demotionWarning = value["demotionWarning"].asBool().unwrapOr(false);
    profile.equippedTitle = parseAchievementTitle(value["equippedTitle"]);
    auto const promotion = value["promotion"];
    if (promotion.isObject()) {
        profile.promotion.active = true;
        profile.promotion.sourceTier = promotion["sourceTier"].asString().unwrapOr("");
        profile.promotion.targetTier = promotion["targetTier"].asString().unwrapOr("");
        profile.promotion.cutoff = static_cast<int>(promotion["cutoff"].asInt().unwrapOr(0));
        profile.promotion.gamesRequired = static_cast<int>(promotion["gamesRequired"].asInt().unwrapOr(0));
        profile.promotion.winsRequired = static_cast<int>(promotion["winsRequired"].asInt().unwrapOr(0));
        profile.promotion.winsRemaining = static_cast<int>(promotion["winsRemaining"].asInt().unwrapOr(
            profile.promotion.winsRequired
        ));
        profile.promotion.results = parseRatingMarks(promotion["results"]);
    }
    return profile;
}

corum::ranked::RatingEventView parseRatingEvent(matjson::Value const& value) {
    if (!value.isObject()) return {};
    return {
        .placementCompleted = value["placementCompleted"].asBool().unwrapOr(false),
        .promotionStarted = value["promotionStarted"].asBool().unwrapOr(false),
        .promotionSucceeded = value["promotionSucceeded"].asBool().unwrapOr(false),
        .promotionFailed = value["promotionFailed"].asBool().unwrapOr(false),
        .demoted = value["demoted"].asBool().unwrapOr(false),
    };
}

corum::ranked::RoundSummaryView parseRoundSummary(matjson::Value const& value) {
    return {
        .roundNumber = static_cast<int>(value["roundNumber"].asInt().unwrapOr(0)),
        .mapTitle = value["mapTitle"].asString().unwrapOr(""),
        .difficulty = value["difficulty"].asString().unwrapOr(""),
        .scoreA = value["scoreA"].asDouble().unwrapOr(0.0),
        .scoreB = value["scoreB"].asDouble().unwrapOr(0.0),
        .clearsA = static_cast<int>(value["clearsA"].asInt().unwrapOr(0)),
        .clearsB = static_cast<int>(value["clearsB"].asInt().unwrapOr(0)),
        .result = value["result"].asString().unwrapOr(""),
    };
}

corum::ranked::DeathmatchSummaryView parseDeathmatchSummary(matjson::Value const& value) {
    return {
        .sequence = static_cast<int>(value["sequence"].asInt().unwrapOr(0)),
        .mapTitle = value["mapTitle"].asString().unwrapOr(""),
        .difficulty = value["difficulty"].asString().unwrapOr(""),
        .scoreA = value["scoreA"].asDouble().unwrapOr(0.0),
        .scoreB = value["scoreB"].asDouble().unwrapOr(0.0),
        .winnerSide = value["winnerSide"].asString().unwrapOr(""),
    };
}

bool stateAllowsActiveAttempt(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING" ||
        state == "MAP_PLAYING" ||
        state == "MAP_SETTLING";
}

int pollIntervalMillis(bool spectatorActive = false) {
    auto const configured = std::clamp(
        Mod::get()->getSettingValue<int>("poll-interval-ms"),
        250,
        5000
    );
    return spectatorActive ? std::min(configured, 150) : configured;
}

web::WebRequest baseRequest(std::string const& sessionToken, std::string const& matchToken = {}) {
    web::WebRequest request;
    request.header("User-Agent", fmt::format(
        "{}/{}; Geometry Dash/{}; Geode/{}; {}",
        Mod::get()->getID(),
        Mod::get()->getVersion().toVString(),
        GEODE_GD_VERSION_STRING,
        Loader::get()->getVersion().toVString(),
        GEODE_PLATFORM_NAME
    ));
    if (!sessionToken.empty()) request.header("Authorization", "Bearer " + sessionToken);
    if (!matchToken.empty()) request.header("x-match-token", matchToken);
    request.followRedirects(false);
    request.timeout(std::chrono::seconds(20));
    return request;
}

} // namespace

namespace corum::ranked {

RankedRuntime& RankedRuntime::get() {
    static RankedRuntime runtime;
    return runtime;
}

char const* stageName(RuntimeStage stage) {
    switch (stage) {
        case RuntimeStage::Idle: return "Idle";
        case RuntimeStage::NotConfigured: return "Not configured";
        case RuntimeStage::Loading: return "Connecting";
        case RuntimeStage::Blocked: return "Environment blocked";
        case RuntimeStage::Ready: return "Ready";
        case RuntimeStage::JoiningQueue: return "Joining queue";
        case RuntimeStage::Queued: return "Queued";
        case RuntimeStage::Matched: return "Matched";
        case RuntimeStage::Error: return "Error";
    }
    return "Unknown";
}

RuntimeView const& RankedRuntime::view() const {
    return m_view;
}

bool RankedRuntime::isPlacement() const {
    return m_view.match.placement && m_view.placement.active;
}

void RankedRuntime::setStage(RuntimeStage stage, std::string status, std::string error) {
    m_view.stage = stage;
    m_view.status = std::move(status);
    m_view.error = std::move(error);
    ++m_view.revision;
}

void RankedRuntime::setTransientError(std::string error) {
    m_view.error = std::move(error);
    ++m_view.revision;
}

EnvironmentModIssueView RankedRuntime::environmentModIssue(std::string const& id) const {
    EnvironmentModIssueView issue;
    issue.id = id;

    auto const installed = std::find_if(
        m_installedMods.begin(),
        m_installedMods.end(),
        [&id](InstalledModSnapshot const& mod) { return mod.id == id; }
    );
    if (installed != m_installedMods.end()) {
        issue.displayName = installed->displayName;
        issue.installedVersion = installed->version;
    }

    auto const rule = std::find_if(
        m_environmentPolicy.allowedMods.begin(),
        m_environmentPolicy.allowedMods.end(),
        [&id](AllowedModRule const& candidate) { return candidate.id == id; }
    );
    if (rule != m_environmentPolicy.allowedMods.end()) {
        if (issue.displayName.empty()) issue.displayName = rule->displayName;
        issue.minimumVersion = rule->minVersion;
        issue.maximumVersion = rule->maxVersion;
    }

    if (issue.displayName.empty()) issue.displayName = id;
    return issue;
}

void RankedRuntime::showEnvironmentWarning(
    EnvironmentDecision const& decision,
    bool blockRuntime
) {
    EnvironmentWarningView warning;
    warning.active = true;
    for (auto const& id : decision.unauthorizedModIds) {
        warning.unauthorizedMods.push_back(environmentModIssue(id));
    }
    for (auto const& id : decision.missingRequiredModIds) {
        warning.missingRequiredMods.push_back(environmentModIssue(id));
    }
    for (auto const& id : decision.versionViolationModIds) {
        warning.versionViolations.push_back(environmentModIssue(id));
    }
    warning.cbfIssues = decision.cbfIssues;
    m_view.environmentWarning = std::move(warning);

    if (blockRuntime) {
        setStage(
            RuntimeStage::Blocked,
            "Ranked cannot start until the mod environment is corrected."
        );
    } else {
        m_view.error = "Ranked Ready was not sent because the mod environment changed.";
        ++m_view.revision;
    }
}

void RankedRuntime::recheckEnvironment() {
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, m_view.stage == RuntimeStage::Blocked);
        return;
    }

    auto const restartSession = m_view.stage == RuntimeStage::Blocked;
    m_view.environmentWarning = {};
    m_view.error.clear();
    ++m_view.revision;
    if (restartSession) begin();
}

void RankedRuntime::invalidatePollRequest() {
    ++m_pollRequestGeneration;
    m_pollRequest.cancel();
    m_pollBusy = false;
    m_nextPollAt = {};
}

void RankedRuntime::invalidateAttemptRequest() {
    ++m_attemptRequestGeneration;
    m_attemptRequest.cancel();
    m_attemptBusy = false;
    m_attemptRequestStartedAt = {};
}

std::string RankedRuntime::endpoint(std::string const& path) const {
    return std::string(kDefaultRankedServerURL) + path;
}

void RankedRuntime::begin() {
    if (
        m_view.stage == RuntimeStage::Loading ||
        m_view.stage == RuntimeStage::JoiningQueue ||
        m_view.stage == RuntimeStage::Queued ||
        m_view.stage == RuntimeStage::Matched
    ) return;

    invalidatePollRequest();
    invalidateAttemptRequest();
    ++m_progressRequestGeneration;
    m_progressRequest.cancel();
    m_progressBusy = false;

    m_sessionToken.clear();
    m_matchToken.clear();
    m_attemptId.clear();
    m_attemptLevelId = 0;
    m_attemptContextKey.clear();
    m_gameplayMap.reset();
    m_gameplayMatchId.clear();
    m_gameplayStartContextKey.clear();
    m_gameplayStartedAt.clear();
    m_gameplayStartEligible = false;
    m_pendingStart.reset();
    m_pendingEnd.reset();
    m_attemptBacklog.clear();
    ++m_attemptIntentRequestGeneration;
    m_attemptIntentRequest.cancel();
    m_attemptIntentBusy = false;
    m_attemptIntentBacklog.clear();
    m_pendingProgress.reset();
    m_nextAttemptRetryAt = {};
    m_nextProgressAt = {};
    m_lastSubmittedProgress = -1;
    m_localDeathmatchSequence = 0;
    m_localDeathmatchVisualAttempts = 0;
    m_songBypassAllowed = false;
    m_placementResultAcknowledged = false;
    m_view.client = {};
    m_view.environmentWarning = {};
    m_view.placement = {};
    m_view.match = {};
    fetchConfig();
}

void RankedRuntime::fetchConfig() {
    if (m_controlBusy) return;
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Loading authoritative Ranked configuration...");
    auto request = baseRequest({});
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/config")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Error, "Could not load Ranked configuration.", responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            if (!root.isObject()) {
                setStage(RuntimeStage::Error, "Could not load Ranked configuration.", "Invalid JSON response");
                return;
            }
            observeServerNow(root);

            RankedClientPresentationView clientPresentation;
            auto const clientNode = root["client"];
            if (clientNode.isObject()) {
                auto const audioNode = clientNode["audio"];
                if (audioNode.isObject()) {
                    clientPresentation.audio.enabled = audioNode["enabled"].asBool().unwrapOr(false);
                    clientPresentation.audio.fadeInSeconds = std::clamp(
                        audioNode["fadeInSeconds"].asDouble().unwrapOr(0.8), 0.0, 10.0
                    );
                    clientPresentation.audio.fadeOutSeconds = std::clamp(
                        audioNode["fadeOutSeconds"].asDouble().unwrapOr(0.6), 0.0, 10.0
                    );
                    auto const resources = audioNode["resources"].asArray();
                    if (resources.isOk()) {
                        for (auto const& item : resources.unwrap()) {
                            if (!item.isObject()) continue;
                            RankedAudioResourceView resource;
                            resource.key = item["key"].asString().unwrapOr("");
                            resource.label = item["label"].asString().unwrapOr("Resource");
                            resource.songId = item["songId"].asInt().unwrapOr(0);
                            resource.startSeconds = std::max(0.0, item["startSeconds"].asDouble().unwrapOr(0.0));
                            resource.loop = item["loop"].asBool().unwrapOr(true);
                            if (!resource.key.empty() && resource.songId > 0) {
                                clientPresentation.audio.resources.push_back(std::move(resource));
                            }
                        }
                    }
                }
                auto const uiNode = clientNode["ui"];
                if (uiNode.isObject()) {
                    clientPresentation.ui.fadeInSeconds = std::clamp(
                        uiNode["fadeInSeconds"].asDouble().unwrapOr(0.24), 0.0, 3.0
                    );
                    clientPresentation.ui.fadeOutSeconds = std::clamp(
                        uiNode["fadeOutSeconds"].asDouble().unwrapOr(0.18), 0.0, 3.0
                    );
                }
            }
            m_view.client = std::move(clientPresentation);

            if (!root["queueEnabled"].asBool().unwrapOr(false)) {
                setStage(RuntimeStage::Error, "The Ranked queue is disabled by operations.");
                return;
            }

            EnvironmentPolicy policy;
            auto const allowedResult = root["allowedMods"].asArray();
            if (allowedResult.isErr()) {
                setStage(RuntimeStage::Error, "Ranked configuration is invalid.", "allowedMods is missing");
                return;
            }
            for (auto const& item : allowedResult.unwrap()) {
                if (!item.isObject()) continue;
                auto const id = item["id"].asString().unwrapOr("");
                if (id.empty()) continue;
                auto const minVersionText = item["minVersion"].asString().unwrapOr("");
                auto const maxVersionText = item["maxVersion"].asString().unwrapOr("");
                policy.allowedMods.push_back({
                    .id = id,
                    .displayName = item["displayName"].asString().unwrapOr(id),
                    .minVersion = minVersionText.empty() ? std::nullopt : std::optional(minVersionText),
                    .maxVersion = maxVersionText.empty() ? std::nullopt : std::optional(maxVersionText),
                    .required = item["required"].asBool().unwrapOr(false),
                    .enabled = item["enabled"].asBool().unwrapOr(false),
                });
            }

            auto const cbf = root["cbf"];
            policy.cbfModId = cbf["modId"].asString().unwrapOr("");
            auto const requiredSettings = cbf["requiredSettings"];
            if (policy.cbfModId.empty() || !requiredSettings.isObject()) {
                setStage(RuntimeStage::Error, "Ranked configuration is invalid.", "CBF policy is missing");
                return;
            }
            if (requiredSettings.size() > std::size(kKnownCbfSettings)) {
                setStage(
                    RuntimeStage::Error,
                    "This client does not understand the current CBF policy.",
                    "Update Corum Ranked before joining."
                );
                return;
            }
            for (auto const key : kKnownCbfSettings) {
                if (requiredSettings.contains(key)) {
                    policy.cbfRequiredSettings[std::string(key)] = jsonSetting(requiredSettings[key]);
                }
            }
            m_environmentPolicy = std::move(policy);
            m_installedMods = captureInstalledMods();
            auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
            if (!decision.allowed) {
                showEnvironmentWarning(decision, true);
                return;
            }
            createSession();
        }
    );
}

std::vector<InstalledModSnapshot> RankedRuntime::captureInstalledMods() const {
    std::vector<InstalledModSnapshot> result;
    for (auto* mod : Loader::get()->getAllMods()) {
        if (!mod) continue;
        InstalledModSnapshot snapshot {
            .id = std::string(mod->getID()),
            .displayName = std::string(mod->getName()),
            .version = mod->getVersion().toVString(),
            .enabled = mod->isOrWillBeEnabled(),
            .loaded = mod->isLoaded(),
            .internal = mod->isInternal(),
            .system = mod->isInternal(),
            .settings = {},
        };
        auto const isActive = snapshot.enabled && snapshot.loaded;
        auto const isCbf = snapshot.id == m_environmentPolicy.cbfModId;
        // Ranked only reports active mods to the allowlist gate. CBF is the one
        // exception: keep an inactive CBF snapshot so we can report that the
        // mandatory dependency is installed but not active.
        if (!isActive && !isCbf) continue;

        if (isCbf) {
            for (auto const& [key, required] : m_environmentPolicy.cbfRequiredSettings) {
                if (!mod->getSetting(key)) {
#if !defined(GEODE_IS_WINDOWS)
                    if (key == "physics-bypass" && required == SettingValue(false)) {
                        snapshot.settings[key] = false;
                    }
#endif
                    continue;
                }
                if (std::holds_alternative<bool>(required)) {
                    snapshot.settings[key] = mod->getSettingValue<bool>(key);
                } else if (std::holds_alternative<double>(required)) {
                    snapshot.settings[key] = mod->getSettingValue<double>(key);
                } else {
                    snapshot.settings[key] = mod->getSettingValue<std::string>(key);
                }
            }
        }
        result.push_back(std::move(snapshot));
    }
    std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
        return left.id < right.id;
    });
    return result;
}

matjson::Value RankedRuntime::installedModsJson() const {
    auto result = matjson::Value::array();
    for (auto const& mod : m_installedMods) {
        matjson::Value item;
        item["id"] = mod.id;
        item["version"] = mod.version.empty() ? "v0.0.0" : mod.version;
        item["enabled"] = mod.enabled;
        item["loaded"] = mod.loaded;
        item["internal"] = mod.internal;
        item["system"] = mod.system;
        if (!mod.settings.empty()) {
            matjson::Value settings;
            for (auto const& [key, value] : mod.settings) writeSetting(settings, key, value);
            item["settings"] = std::move(settings);
        }
        result.push(std::move(item));
    }
    return result;
}

void RankedRuntime::createSession() {
    if (m_controlBusy) return;
    auto* account = GJAccountManager::get();
    auto const accountId = account ? account->m_accountID : 0;
    auto const username = account ? std::string(account->m_username) : std::string();
    if (accountId <= 0 || username.empty()) {
        setStage(RuntimeStage::Error, "A logged-in Geometry Dash account is required.");
        return;
    }

    matjson::Value body;
    body["gdAccountId"] = fmt::format("{}", accountId);
    body["gdUsername"] = username;
    body["clientVersion"] = Mod::get()->getVersion().toVString();
    body["installedMods"] = installedModsJson();
    auto request = baseRequest({});
    // Initial profile creation can require a cold Apps Script CSMP lookup plus one retry.
    request.timeout(std::chrono::seconds(70));
    request.bodyJSON(body);
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Creating a server session...");
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/session")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Error, "Could not create a Ranked session.", responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            m_sessionToken = root["sessionToken"].asString().unwrapOr("");
            if (m_sessionToken.empty()) {
                setStage(RuntimeStage::Error, "Could not create a Ranked session.", "Session token is missing");
                return;
            }
            observeServerNow(root);
            auto const player = root["player"];
            auto const profile = parseRankedProfile(player);
            m_view.profileTier = profile.displayedTier;
            m_view.profileScoreVisible = profile.visibleRankedScore.has_value();
            m_view.profileScore = profile.visibleRankedScore.value_or(0);
            m_view.placementGames = profile.placementGames;
            m_view.placementGamesRequired = profile.placementGamesRequired;
            m_view.placementResults = profile.placementResults;
            m_view.resultStreak = profile.resultStreak;
            m_view.promotion = profile.promotion;
            m_view.demotionWarning = profile.demotionWarning;
            m_view.placement.required =
                profile.displayedTier == "UNRANKED" ||
                profile.placementGames < profile.placementGamesRequired;
            // Completed runs can still have an unacknowledged Reveal after a lost
            // finalization response, so every session checks the recovery endpoint.
            fetchActivePlacement();
        }
    );
}

void RankedRuntime::fetchActivePlacement() {
    if (m_controlBusy || m_sessionToken.empty()) return;
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Checking for an active Placement run...");
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/placement/active")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Error, "Could not recover Placement.", responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            if (!root["active"].asBool().unwrapOr(false)) {
                m_view.placement.active = false;
                m_view.placement.stageResults.clear();
                if (auto const results = root["stageResults"].asArray(); results.isOk()) {
                    for (auto const& item : results.unwrap()) {
                        if (!item.isObject()) continue;
                        PlacementStageResultView result;
                        result.stageIndex = static_cast<int>(item["stageIndex"].asInt().unwrapOr(0));
                        result.pool = static_cast<int>(item["pool"].asInt().unwrapOr(0));
                        if (auto const scores = item["mapScores"].asArray(); scores.isOk()) {
                            for (auto const& score : scores.unwrap()) result.mapScores.push_back(score.asDouble().unwrapOr(0.0));
                        }
                        if (auto const grades = item["mapGrades"].asArray(); grades.isOk()) {
                            for (auto const& grade : grades.unwrap()) result.mapGrades.push_back(grade.asString().unwrapOr("D"));
                        }
                        result.stageScore = item["stageScore"].asDouble().unwrapOr(0.0);
                        result.stageGrade = item["stageGrade"].asString().unwrapOr("D");
                        result.poolDelta = static_cast<int>(item["poolDelta"].asInt().unwrapOr(0));
                        result.nextPool = static_cast<int>(item["nextPool"].asInt().unwrapOr(1));
                        m_view.placement.stageResults.push_back(std::move(result));
                    }
                }
                m_view.placement.referenceMaps.clear();
                if (auto const references = root["referenceMaps"].asArray(); references.isOk()) {
                    for (auto const& item : references.unwrap()) {
                        if (auto map = parseMap(item)) m_view.placement.referenceMaps.push_back(*map);
                    }
                }
                setStage(
                    RuntimeStage::Ready,
                    m_view.placement.required
                        ? "Choose the Pool that best matches your current skill."
                        : "Ranked session ready. Join the queue when prepared."
                );
                fetchAchievements();
                return;
            }
            parsePlacementState(root);
            if (m_view.placement.state == "STAGE_RESULT") {
                setStage(RuntimeStage::Ready, "Placement Stage saved. Continue whenever you're ready.");
            } else {
                setStage(RuntimeStage::Matched, "Placement recovered. Continue from the saved Stage and Map.");
            }
            m_nextPollAt = std::chrono::steady_clock::now();
            fetchAchievements();
        }
    );
}

void RankedRuntime::skipPlacement() {
    if (
        m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready ||
        !m_view.placement.required || m_view.placement.active
    ) return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, true);
        return;
    }
    matjson::Value body;
    body["installedMods"] = installedModsJson();
    auto request = baseRequest(m_sessionToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Finalizing skipped Placement...");
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/placement/skip")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Ready, "Placement skip was not applied.", responseError(response));
                return;
            }
            parsePlacementState(response.json().unwrapOr(matjson::Value()));
            setStage(RuntimeStage::Matched, "Placement skipped. Showing the server result.");
        }
    );
}

void RankedRuntime::startPlacement(int startingPool, int stageCount) {
    if (
        m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready ||
        !m_view.placement.required || m_view.placement.active || startingPool < 1 || startingPool > 6 ||
        (stageCount != 3 && stageCount != 5)
    ) return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, true);
        return;
    }
    matjson::Value body;
    body["startingPool"] = startingPool;
    body["stageCount"] = stageCount;
    body["installedMods"] = installedModsJson();
    auto request = baseRequest(m_sessionToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Starting server-authoritative Placement...");
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/placement/start")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Ready, "Choose a starting Pool.", responseError(response));
                return;
            }
            m_matchToken.clear();
            parsePlacementState(response.json().unwrapOr(matjson::Value()));
            setStage(RuntimeStage::Matched, "Placement started. Preparing Stage 1 Map 1.");
            m_nextPollAt = std::chrono::steady_clock::now();
        }
    );
}

void RankedRuntime::continuePlacement() {
    if (
        m_controlBusy || !isPlacement() || m_view.match.matchId.empty() ||
        (m_view.match.state != "MAP_RESULT" && m_view.match.state != "STAGE_RESULT")
    ) return;
    auto const runId = m_view.match.matchId;
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/placement/" + runId + "/continue")),
        [this, runId](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setTransientError(responseError(response));
                return;
            }
            if (!isPlacement() || m_view.match.matchId != runId) return;
            parsePlacementState(response.json().unwrapOr(matjson::Value()));
            setStage(RuntimeStage::Matched, "Placement resumed. Preparing the next Map.");
            m_nextPollAt = std::chrono::steady_clock::now();
        }
    );
}

void RankedRuntime::pausePlacement() {
    if (!isPlacement() || m_view.placement.state != "STAGE_RESULT") return;
    invalidatePollRequest();
    setStage(RuntimeStage::Ready, "Placement Stage saved. Continue whenever you're ready.");
}

void RankedRuntime::joinQueue() {
    if (m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready) return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, true);
        return;
    }
    invalidatePollRequest();
    invalidateAttemptRequest();
    ++m_progressRequestGeneration;
    m_progressRequest.cancel();
    m_progressBusy = false;
    matjson::Value body;
    body["installedMods"] = installedModsJson();
    auto request = baseRequest(m_sessionToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    setStage(RuntimeStage::JoiningQueue, "Joining the Ranked queue...");
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/queue/join")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Ready, "Ranked session ready.", responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            setStage(RuntimeStage::Queued, "Searching for an opponent...");
            m_nextPollAt = std::chrono::steady_clock::now();
            pollQueue();
        }
    );
}

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
void RankedRuntime::startDebugBotMatch(std::string const& password, DebugBotMatchOptions options) {
    options.password = password;
    startDebugBotMatch(options);
}

void RankedRuntime::startDebugBotMatch(DebugBotMatchOptions options) {
    if (m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready) return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, true);
        return;
    }
    invalidatePollRequest();
    matjson::Value body;
    body["password"] = options.password;
    body["difficulty"] = options.difficulty;
    body["scenario"] = options.scenario;
    body["botBan"] = options.botBan;
    body["sendDiscordEvents"] = options.sendDiscordEvents;
    body["installedMods"] = installedModsJson();
    auto request = baseRequest(m_sessionToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    setStage(RuntimeStage::JoiningQueue, "Creating DEBUG BOT MATCH...");
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/debug/bot-match")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Ready, "Ranked session ready.", responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            m_view.match = {};
            m_view.match.matchId = root["matchId"].asString().unwrapOr("");
            m_view.match.side = root["side"].asString().unwrapOr("A");
            m_view.match.debug = root["debug"].asBool().unwrapOr(true);
            m_matchToken = root["matchToken"].asString().unwrapOr("");
            if (m_view.match.matchId.empty() || m_matchToken.empty()) {
                setStage(RuntimeStage::Error, "The debug server returned an incomplete match assignment.");
                return;
            }
            setStage(RuntimeStage::Matched, "DEBUG BOT MATCH created. Confirm Ready.");
            m_nextPollAt = std::chrono::steady_clock::now();
            pollMatch();
        }
    );
}
#endif

void RankedRuntime::leaveQueue() {
    if (m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Queued) return;
    invalidatePollRequest();
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    setStage(RuntimeStage::JoiningQueue, "Leaving the Ranked queue...");
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/queue/leave")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setStage(RuntimeStage::Queued, "Searching for an opponent...", responseError(response));
                m_nextPollAt = std::chrono::steady_clock::now();
                return;
            }
            setStage(RuntimeStage::Ready, "Left the queue.");
        }
    );
}

void RankedRuntime::pollQueue() {
    if (m_pollBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Queued) return;
    auto request = baseRequest(m_sessionToken);
    auto const generation = ++m_pollRequestGeneration;
    m_pollBusy = true;
    m_pollRequest.spawn(
        request.get(endpoint("/api/ranked/queue/status")),
        [this, generation](web::WebResponse response) {
            if (generation != m_pollRequestGeneration) {
                log::debug("Ignoring stale Ranked queue poll response");
                return;
            }
            m_pollBusy = false;
            m_nextPollAt = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(pollIntervalMillis());
            if (!successful(response)) {
                setTransientError(responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            auto const status = root["status"].asString().unwrapOr("");
            if (status != "MATCHED") {
                auto const range = root["searchRange"].asInt().unwrapOr(0);
                m_view.status = range > 0
                    ? fmt::format("Searching for an opponent (rating range +/-{})...", range)
                    : "Searching for an opponent...";
                m_view.error.clear();
                ++m_view.revision;
                return;
            }
            m_view.match.matchId = root["matchId"].asString().unwrapOr("");
            m_matchToken = root["matchToken"].asString().unwrapOr("");
            if (m_view.match.matchId.empty() || m_matchToken.empty()) {
                setStage(RuntimeStage::Error, "The server returned an incomplete match assignment.");
                return;
            }
            setStage(RuntimeStage::Matched, "Match found. Preparing ban phase...");
            m_nextPollAt = std::chrono::steady_clock::now();
            pollMatch();
        }
    );
}

void RankedRuntime::pollMatch() {
    if (
        m_pollBusy || m_sessionToken.empty() || (!isPlacement() && m_matchToken.empty()) ||
        m_view.stage != RuntimeStage::Matched || m_view.match.matchId.empty()
    ) return;
    auto const matchId = m_view.match.matchId;
    auto const placement = isPlacement();
    auto request = baseRequest(m_sessionToken, placement ? std::string() : m_matchToken);
    auto const generation = ++m_pollRequestGeneration;
    m_pollBusy = true;
    m_pollRequest.spawn(
        request.get(endpoint(placement
            ? "/api/ranked/placement/" + matchId + "/state"
            : "/api/ranked/matches/" + matchId + "/state")),
        [this, generation, matchId, placement](web::WebResponse response) {
            if (
                generation != m_pollRequestGeneration ||
                m_view.stage != RuntimeStage::Matched ||
                m_view.match.matchId != matchId
            ) {
                log::debug("Ignoring stale Ranked match poll response: match={}", matchId);
                return;
            }
            m_pollBusy = false;
            if (!successful(response)) {
                m_nextPollAt = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(pollIntervalMillis());
                setTransientError(responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            if (placement) parsePlacementState(root);
            else parseMatchState(root);
            m_nextPollAt = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(pollIntervalMillis(m_view.match.spectatorActive));
        }
    );
}

void RankedRuntime::parsePlacementState(matjson::Value const& root) {
    if (!root.isObject()) {
        setTransientError("Invalid Placement state response");
        return;
    }
    observeServerNow(root);
    auto const previousState = m_view.placement.state;
    auto const runId = root["runId"].asString().unwrapOr(m_view.placement.runId);
    if (runId.empty()) {
        setTransientError("Placement Run ID is missing");
        return;
    }

    m_view.placement.required = root["state"].asString().unwrapOr("") != "PLACEMENT_RESULT";
    m_view.placement.active = true;
    m_view.placement.runId = runId;
    m_view.placement.state = root["state"].asString().unwrapOr("");
    m_view.placement.stateVersion = root["stateVersion"].asInt().unwrapOr(0);
    m_view.placement.startingPool = static_cast<int>(root["startingPool"].asInt().unwrapOr(0));
    m_view.placement.currentStageIndex = static_cast<int>(root["currentStageIndex"].asInt().unwrapOr(0));
    m_view.placement.currentMapIndex = static_cast<int>(root["currentMapIndex"].asInt().unwrapOr(0));
    m_view.placement.totalStages = static_cast<int>(root["totalStages"].asInt().unwrapOr(5));
    m_view.placement.mapsPerStage = static_cast<int>(root["mapsPerStage"].asInt().unwrapOr(2));
    m_view.placement.stagePools.clear();
    if (auto const pools = root["stagePools"].asArray(); pools.isOk()) {
        for (auto const& pool : pools.unwrap()) {
            m_view.placement.stagePools.push_back(static_cast<int>(pool.asInt().unwrapOr(0)));
        }
    }
    m_view.placement.stageResults.clear();
    if (auto const results = root["stageResults"].asArray(); results.isOk()) {
        for (auto const& item : results.unwrap()) {
            if (!item.isObject()) continue;
            PlacementStageResultView result;
            result.stageIndex = static_cast<int>(item["stageIndex"].asInt().unwrapOr(0));
            result.pool = static_cast<int>(item["pool"].asInt().unwrapOr(0));
            if (auto const scores = item["mapScores"].asArray(); scores.isOk()) {
                for (auto const& score : scores.unwrap()) {
                    result.mapScores.push_back(score.asDouble().unwrapOr(0.0));
                }
            }
            if (auto const grades = item["mapGrades"].asArray(); grades.isOk()) {
                for (auto const& grade : grades.unwrap()) {
                    result.mapGrades.push_back(grade.asString().unwrapOr("D"));
                }
            }
            result.stageScore = item["stageScore"].asDouble().unwrapOr(0.0);
            result.stageGrade = item["stageGrade"].asString().unwrapOr("D");
            result.poolDelta = static_cast<int>(item["poolDelta"].asInt().unwrapOr(0));
            result.nextPool = static_cast<int>(item["nextPool"].asInt().unwrapOr(1));
            m_view.placement.stageResults.push_back(std::move(result));
        }
    }

    m_view.match = {};
    m_view.match.placement = true;
    m_view.match.matchId = runId;
    m_view.match.state = m_view.placement.state;
    m_view.match.stateVersion = m_view.placement.stateVersion;
    m_view.match.deadlineAt = root["deadlineAt"].asString().unwrapOr("");
    m_view.match.side = "A";
    m_view.match.playerAName = "PLACEMENT";
    m_view.match.playerATier = "UNRANKED";
    m_view.match.roundNumber = m_view.placement.currentStageIndex;
    m_view.match.spectatorActive = false;

    m_view.placement.currentStageMapScores.clear();
    m_view.placement.currentStageMapGrades.clear();
    if (auto const stageMaps = root["currentStageMaps"].asArray(); stageMaps.isOk()) {
        for (auto const& stageMap : stageMaps.unwrap()) {
            m_view.placement.currentStageMapScores.push_back(stageMap["mapScore"].asDouble().unwrapOr(0.0));
            m_view.placement.currentStageMapGrades.push_back(stageMap["mapGrade"].asString().unwrapOr("?"));
        }
    }

    auto const currentMap = root["currentMap"];
    if (auto map = parseMap(currentMap)) {
        m_view.match.currentMap = *map;
        m_view.placement.currentMapScore = currentMap["mapScore"].asDouble().unwrapOr(0.0);
        m_view.placement.currentMapTopAttempts.clear();
        if (auto const topAttempts = currentMap["topAttempts"].asArray(); topAttempts.isOk()) {
            for (auto const& item : topAttempts.unwrap()) {
                if (!item.isObject()) continue;
                PlacementAttemptView attempt;
                attempt.attemptId = item["attemptId"].asString().unwrapOr("");
                attempt.progressPercent = item["progressPercent"].asDouble().unwrapOr(0.0);
                attempt.cleared = item["cleared"].asBool().unwrapOr(false);
                attempt.awardedScore = item["awardedScore"].asDouble().unwrapOr(0.0);
                if (!attempt.attemptId.empty()) m_view.placement.currentMapTopAttempts.push_back(std::move(attempt));
            }
        }
        auto const provisional = currentMap["provisionalMapScore"].asDouble().unwrapOr(m_view.placement.currentMapScore);
        if (hasLocalOpenAttempt()) {
            m_view.placement.provisionalMapScore = std::max(
                m_view.placement.provisionalMapScore,
                provisional
            );
        } else {
            m_view.placement.provisionalMapScore = provisional;
        }
        auto const incomingGrade = currentMap["provisionalMapGrade"].asString().unwrapOr(
            currentMap["mapGrade"].asString().unwrapOr("D")
        );
        if (
            !hasLocalOpenAttempt() ||
            placementGradeRank(incomingGrade) <= placementGradeRank(m_view.placement.currentMapGrade)
        ) {
            m_view.placement.currentMapGrade = incomingGrade;
        }
        m_view.placement.currentMapClears = static_cast<int>(currentMap["clearCount"].asInt().unwrapOr(0));
        m_view.placement.currentMapNormalEndAt = currentMap["normalEndAt"].asString().unwrapOr("");
        m_view.placement.currentMapFinalWindowEndAt = currentMap["finalWindowEndAt"].asString().unwrapOr("");
        m_view.match.readyA = !currentMap["readyAt"].asString().unwrapOr("").empty();
        m_view.match.readyB = true;
        if (
            m_view.match.state == "MAP_PLAYING" &&
            !m_view.placement.currentMapNormalEndAt.empty()
        ) {
            // The ordinary HUD deadline is Placement's three-minute Main Time. Its
            // signed overrun drives the separate ten-second Final Attempt window.
            m_view.match.deadlineAt = m_view.placement.currentMapNormalEndAt;
        }
        m_view.match.scoreA = m_view.placement.currentMapScore;
        m_view.match.committedScoreA = m_view.placement.currentMapScore;
        m_view.match.clearsA = m_view.placement.currentMapClears;
    } else {
        m_view.match.currentMap.reset();
    }

    auto const activeAttempt = root["activeAttempt"];
    m_view.placement.activeAttemptId = activeAttempt["attemptId"].asString().unwrapOr("");
    // A process restart cannot restore Geometry Dash's frame state, but it can
    // retain the exact authoritative Attempt identity. The next PlayLayer scene
    // continues that same persisted Attempt and ends it idempotently.
    if (
        !m_view.placement.activeAttemptId.empty() && !hasLocalAttemptInFlight() &&
        m_view.match.currentMap
    ) {
        m_attemptId = m_view.placement.activeAttemptId;
        m_attemptLevelId = m_view.match.currentMap->levelId;
        m_attemptContextKey = currentAttemptContextKey();
    }

    m_view.placement.finalResult = {};
    auto const finalResult = root["finalResult"];
    if (finalResult.isObject()) {
        m_view.placement.finalResult.placed = finalResult["placed"].asBool().unwrapOr(false);
        m_view.placement.finalResult.skipped = finalResult["skipped"].asBool().unwrapOr(false);
        m_view.placement.finalResult.visibleRankedScore = static_cast<int>(
            finalResult["visibleRankedScore"].asInt().unwrapOr(0)
        );
        m_view.placement.finalResult.displayedTier = finalResult["displayedTier"].asString().unwrapOr("UNRANKED");
        m_view.placement.finalResult.finalizedAt = finalResult["finalizedAt"].asString().unwrapOr("");
        if (auto const unlocks = finalResult["achievementUnlocks"].asArray(); unlocks.isOk()) {
            for (auto const& item : unlocks.unwrap()) {
                auto notice = parseAchievementNotice(item);
                if (!notice.achievementId.empty()) {
                    m_view.placement.finalResult.achievementUnlocks.push_back(std::move(notice));
                }
            }
        }
        auto const noticeContext = "placement:" + m_view.placement.runId;
        if (
            !m_view.placement.finalResult.achievementUnlocks.empty() &&
            m_lastAchievementNoticeContext != noticeContext
        ) {
            m_lastAchievementNoticeContext = noticeContext;
            m_view.achievements.unclaimedRewardCount += static_cast<int>(
                m_view.placement.finalResult.achievementUnlocks.size()
            );
        }
        if (m_view.placement.finalResult.placed) {
            m_view.match.profileBeforeA.displayedTier = "UNRANKED";
            m_view.match.profileAfterA.displayedTier = m_view.placement.finalResult.displayedTier;
            m_view.match.profileAfterA.visibleRankedScore = m_view.placement.finalResult.visibleRankedScore;
            m_view.match.profileAfterA.placementGames = m_view.placement.totalStages;
            m_view.match.profileAfterA.placementGamesRequired = m_view.placement.totalStages;
            m_view.match.profileBeforeTierA = "UNRANKED";
            m_view.match.profileAfterTierA = m_view.placement.finalResult.displayedTier;
            m_view.match.profileAfterScoreA = m_view.placement.finalResult.visibleRankedScore;
            m_view.match.ownRankedScoreAfter = m_view.placement.finalResult.visibleRankedScore;
            m_view.match.ratingEventA.placementCompleted = true;
        }
    }

    if (m_view.match.state == "PLACEMENT_RESULT") {
        m_view.status = "Placement complete. Your initial Tier is ready.";
    } else if (m_view.match.state == "STAGE_RESULT") {
        m_view.status = "Stage complete. Review the combined Grade.";
    } else if (m_view.match.state == "MAP_RESULT") {
        m_view.status = "Map Grade locked by the server.";
    } else if (m_view.match.state == "MAP_SETTLING") {
        m_view.status = "Final accepted Attempt is still in progress.";
    } else if (m_view.match.state == "MAP_PLAYING") {
        m_view.status = "Placement Map live. Score and Grade update from the active Top 2 evidence.";
    } else if (m_view.match.state == "MAP_PREPARE") {
        m_view.status = "Placement Map revealed. Preparing the exact GD level.";
    } else if (m_view.match.state == "CANCELLED") {
        m_view.status = "Placement cancelled by the server.";
    }
    m_view.error.clear();
    ++m_view.revision;
    if (previousState != m_view.placement.state) {
        log::info("Corum Ranked Placement state: {} -> {}", previousState, m_view.placement.state);
        TransitionTrace::get().begin(
            fmt::format("{}:{}->{}", m_view.match.matchId, previousState, m_view.placement.state),
            "server-state-received"
        );
    }
}

void RankedRuntime::parseMatchState(matjson::Value const& root) {
    if (!root.isObject()) {
        setTransientError("Invalid match state response");
        return;
    }
    observeServerNow(root);
    auto const previousState = m_view.match.state;
    m_view.match.state = root["state"].asString().unwrapOr("");
    m_view.match.stateVersion = root["stateVersion"].asInt().unwrapOr(0);
    m_view.match.deadlineAt = root["deadlineAt"].asString().unwrapOr("");
    m_view.match.side = root["side"].asString().unwrapOr("");
    m_view.match.effectiveTier = root["effectiveTier"].asString().unwrapOr("");
    m_view.match.playerAName = root["players"]["A"]["gdUsername"].asString().unwrapOr("Player A");
    m_view.match.playerBName = root["players"]["B"]["gdUsername"].asString().unwrapOr("Player B");
    auto const playerAAccountText = root["players"]["A"]["gdAccountId"].asString().unwrapOr("");
    auto const playerBAccountText = root["players"]["B"]["gdAccountId"].asString().unwrapOr("");
    m_view.match.playerAAccountId = numFromString<int>(playerAAccountText).unwrapOr(
        static_cast<int>(root["players"]["A"]["gdAccountId"].asInt().unwrapOr(0))
    );
    m_view.match.playerBAccountId = numFromString<int>(playerBAccountText).unwrapOr(
        static_cast<int>(root["players"]["B"]["gdAccountId"].asInt().unwrapOr(0))
    );
    m_view.match.playerATier = root["players"]["A"]["displayedTier"].asString().unwrapOr("UNRANKED");
    m_view.match.playerBTier = root["players"]["B"]["displayedTier"].asString().unwrapOr("UNRANKED");
    m_view.match.playerAProfile = parseRankedProfile(root["players"]["A"]);
    m_view.match.playerBProfile = parseRankedProfile(root["players"]["B"]);
    m_view.match.playerAScoreVisible = m_view.match.playerAProfile.visibleRankedScore.has_value();
    m_view.match.playerBScoreVisible = m_view.match.playerBProfile.visibleRankedScore.has_value();
    m_view.match.playerAScore = m_view.match.playerAProfile.visibleRankedScore.value_or(0);
    m_view.match.playerBScore = m_view.match.playerBProfile.visibleRankedScore.value_or(0);
    m_view.match.readyA = root["ready"]["A"].asBool().unwrapOr(false);
    m_view.match.readyB = root["ready"]["B"].asBool().unwrapOr(false);
    m_view.match.ownBanConfirmed = root["banStatus"]["confirmed"].asBool().unwrapOr(false);
    m_view.match.ownBanCanonicalLevelId = root["banStatus"]["canonicalLevelId"].asString().unwrapOr("");
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    m_view.match.debug = root["debug"].asBool().unwrapOr(false);
#endif
    m_view.match.candidateMaps.clear();
    m_view.match.currentMap.reset();
    m_view.match.banner.clear();
    m_view.match.roundNumber = 0;
    m_view.match.deathmatchSequence = 0;
    m_view.match.deathmatchAttemptsUsedA = 0;
    m_view.match.deathmatchAttemptsUsedB = 0;
    m_view.match.deathmatchAttemptsCompletedA = 0;
    m_view.match.deathmatchAttemptsCompletedB = 0;
    m_view.match.scoreA = 0.0;
    m_view.match.scoreB = 0.0;
    m_view.match.committedScoreA = 0.0;
    m_view.match.committedScoreB = 0.0;
    m_view.match.clearsA = 0;
    m_view.match.clearsB = 0;
    m_view.match.roundWinsA = static_cast<int>(root["series"]["roundWins"]["A"].asInt().unwrapOr(0));
    m_view.match.roundWinsB = static_cast<int>(root["series"]["roundWins"]["B"].asInt().unwrapOr(0));
    m_view.match.rounds.clear();
    m_view.match.deathmatches.clear();
    m_view.match.cancellationReason = root["cancellation"]["reason"].asString().unwrapOr("");
    m_view.match.spectatorActive = false;
    m_view.match.opponentProgress.reset();
    m_view.match.opponentFinished = root["opponentFinished"].asBool().unwrapOr(false);
    m_view.match.opponentPractice = root["opponentPractice"].asBool().unwrapOr(false);
    m_view.match.spectatorCurrentProgress.reset();
    m_view.match.spectatorOpponentName.clear();

    auto const opponentSide = m_view.match.side == "A" ? "B" : "A";
    m_view.match.opponentName = root["players"][opponentSide]["gdUsername"].asString().unwrapOr("Opponent");
    auto const opponentProgress = root["opponentProgress"].asInt();
    if (opponentProgress.isOk()) {
        m_view.match.opponentProgress = std::clamp(
            static_cast<int>(opponentProgress.unwrap()),
            0,
            100
        );
    }

    auto const candidates = root["candidateMaps"].asArray();
    if (candidates.isOk()) {
        for (auto const& item : candidates.unwrap()) {
            if (auto map = parseMap(item)) m_view.match.candidateMaps.push_back(*map);
        }
    }

    auto const round = root["currentRound"];
    if (round.isObject()) {
        m_view.match.roundNumber = static_cast<int>(round["roundNumber"].asInt().unwrapOr(0));
        m_view.match.banner = round["banner"].asString().unwrapOr("");
        auto const committedScores = round["scores"];
        auto const displayScores = round["displayScores"].isObject() ? round["displayScores"] : committedScores;
        m_view.match.committedScoreA = committedScores["A"].asDouble().unwrapOr(0.0);
        m_view.match.committedScoreB = committedScores["B"].asDouble().unwrapOr(0.0);
        m_view.match.scoreA = displayScores["A"].asDouble().unwrapOr(m_view.match.committedScoreA);
        m_view.match.scoreB = displayScores["B"].asDouble().unwrapOr(m_view.match.committedScoreB);
        m_view.match.clearsA = static_cast<int>(round["clears"]["A"].asInt().unwrapOr(0));
        m_view.match.clearsB = static_cast<int>(round["clears"]["B"].asInt().unwrapOr(0));
        m_view.match.currentMap = parseMap(round["map"]);
    }
    auto const deathmatch = root["deathmatch"];
    if (deathmatch.isObject()) {
        m_view.match.deathmatchSequence = static_cast<int>(deathmatch["sequence"].asInt().unwrapOr(0));
        m_view.match.currentMap = parseMap(deathmatch["map"]);
        auto const committedScores = deathmatch["scores"];
        auto const displayScores = deathmatch["displayScores"].isObject() ? deathmatch["displayScores"] : committedScores;
        m_view.match.committedScoreA = committedScores["A"].asDouble().unwrapOr(0.0);
        m_view.match.committedScoreB = committedScores["B"].asDouble().unwrapOr(0.0);
        m_view.match.scoreA = displayScores["A"].asDouble().unwrapOr(m_view.match.committedScoreA);
        m_view.match.scoreB = displayScores["B"].asDouble().unwrapOr(m_view.match.committedScoreB);
        m_view.match.clearsA = static_cast<int>(deathmatch["clears"]["A"].asInt().unwrapOr(0));
        m_view.match.clearsB = static_cast<int>(deathmatch["clears"]["B"].asInt().unwrapOr(0));
        m_view.match.deathmatchAttemptsUsedA = static_cast<int>(deathmatch["attemptsUsed"]["A"].asInt().unwrapOr(0));
        m_view.match.deathmatchAttemptsUsedB = static_cast<int>(deathmatch["attemptsUsed"]["B"].asInt().unwrapOr(0));
        m_view.match.deathmatchAttemptsCompletedA = static_cast<int>(deathmatch["attemptsCompleted"]["A"].asInt().unwrapOr(0));
        m_view.match.deathmatchAttemptsCompletedB = static_cast<int>(deathmatch["attemptsCompleted"]["B"].asInt().unwrapOr(0));

        auto const ownServerUsed = m_view.match.side == "A"
            ? m_view.match.deathmatchAttemptsUsedA
            : m_view.match.deathmatchAttemptsUsedB;
        if (m_localDeathmatchSequence != m_view.match.deathmatchSequence) {
            m_localDeathmatchSequence = m_view.match.deathmatchSequence;
            m_localDeathmatchVisualAttempts = ownServerUsed;
        } else {
            m_localDeathmatchVisualAttempts = std::max(
                m_localDeathmatchVisualAttempts,
                ownServerUsed
            );
        }
    }

    auto const roundSummaries = root["rounds"].asArray();
    if (roundSummaries.isOk()) {
        for (auto const& item : roundSummaries.unwrap()) {
            if (item.isObject()) m_view.match.rounds.push_back(parseRoundSummary(item));
        }
    }
    auto const deathmatchSummaries = root["deathmatches"].asArray();
    if (deathmatchSummaries.isOk()) {
        for (auto const& item : deathmatchSummaries.unwrap()) {
            if (item.isObject()) m_view.match.deathmatches.push_back(parseDeathmatchSummary(item));
        }
    }

    auto const spectator = root["spectator"];
    if (spectator.isObject() && spectator["active"].asBool().unwrapOr(false)) {
        m_view.match.spectatorActive = true;
        m_view.match.spectatorOpponentName = spectator["opponentName"].asString().unwrapOr(
            m_view.match.opponentName
        );
        auto const progress = spectator["currentProgress"].asInt();
        if (progress.isOk()) {
            m_view.match.spectatorCurrentProgress = std::clamp(
                static_cast<int>(progress.unwrap()),
                0,
                100
            );
        }
    }

    // The Last Attempt window itself is authoritative enough to identify its
    // spectator: the trigger side has finished three Clears and only the target
    // side may start. Recover that state locally if a state response races with
    // the attempt-end acknowledgement or temporarily omits the convenience
    // `spectator` projection. Without this fallback the popup can remain on its
    // ordinary OPENING LEVEL / WAITING FOR MAP presentation indefinitely.
    auto const lastAttempt = round["lastAttemptWindow"];
    auto const inLastAttemptFlow =
        m_view.match.state == "LAST_ATTEMPT_WINDOW" ||
        m_view.match.state == "ROUND_SETTLING";
    if (!m_view.match.spectatorActive && inLastAttemptFlow && lastAttempt.isObject()) {
        auto const trigger = lastAttempt["triggerSide"].asString().unwrapOr("");
        auto const target = lastAttempt["targetSide"].asString().unwrapOr("");
        if (!trigger.empty() && trigger == m_view.match.side && target != m_view.match.side) {
            m_view.match.spectatorActive = true;
            m_view.match.spectatorOpponentName = m_view.match.opponentName;
            m_view.match.spectatorCurrentProgress.reset();
        }
    }

    auto const result = root["result"];
    m_view.match.winnerSide = result["winnerSide"].asString().unwrapOr("");
    m_view.match.perfectWinSide = result["perfectWinSide"].asString().unwrapOr("");
    m_view.match.ownRankedScoreDelta.reset();
    m_view.match.ownRankedScoreAfter.reset();
    m_view.match.rankedScoreDeltaA.reset();
    m_view.match.rankedScoreDeltaB.reset();
    m_view.match.rankedScoreAfterA.reset();
    m_view.match.rankedScoreAfterB.reset();
    m_view.match.profileBeforeTierA.clear();
    m_view.match.profileBeforeTierB.clear();
    m_view.match.profileAfterTierA.clear();
    m_view.match.profileAfterTierB.clear();
    m_view.match.profileAfterScoreA.reset();
    m_view.match.profileAfterScoreB.reset();
    m_view.match.profileBeforeA = {};
    m_view.match.profileBeforeB = {};
    m_view.match.profileAfterA = {};
    m_view.match.profileAfterB = {};
    m_view.match.ratingEventA = {};
    m_view.match.ratingEventB = {};
    m_view.match.achievementUnlocks.clear();
    if (result.isObject() && !m_view.match.side.empty()) {
        auto const deltaA = result["rankedScoreDelta"]["A"].asInt();
        auto const deltaB = result["rankedScoreDelta"]["B"].asInt();
        auto const afterA = result["profileAfter"]["A"]["visibleRankedScore"].asInt();
        auto const afterB = result["profileAfter"]["B"]["visibleRankedScore"].asInt();
        if (deltaA.isOk()) m_view.match.rankedScoreDeltaA = static_cast<int>(deltaA.unwrap());
        if (deltaB.isOk()) m_view.match.rankedScoreDeltaB = static_cast<int>(deltaB.unwrap());
        if (afterA.isOk()) m_view.match.rankedScoreAfterA = static_cast<int>(afterA.unwrap());
        if (afterB.isOk()) m_view.match.rankedScoreAfterB = static_cast<int>(afterB.unwrap());
        m_view.match.ownRankedScoreDelta = m_view.match.side == "A"
            ? m_view.match.rankedScoreDeltaA
            : m_view.match.rankedScoreDeltaB;
        m_view.match.ownRankedScoreAfter = m_view.match.side == "A"
            ? m_view.match.rankedScoreAfterA
            : m_view.match.rankedScoreAfterB;
        auto const profileBefore = result["profileBefore"];
        if (profileBefore.isObject()) {
            m_view.match.profileBeforeA = parseRankedProfile(profileBefore["A"]);
            m_view.match.profileBeforeB = parseRankedProfile(profileBefore["B"]);
            m_view.match.profileBeforeTierA = profileBefore["A"]["displayedTier"].asString().unwrapOr("");
            m_view.match.profileBeforeTierB = profileBefore["B"]["displayedTier"].asString().unwrapOr("");
        }
        auto const profileAfter = result["profileAfter"];
        if (profileAfter.isObject()) {
            m_view.match.profileAfterA = parseRankedProfile(profileAfter["A"]);
            m_view.match.profileAfterB = parseRankedProfile(profileAfter["B"]);
            m_view.match.profileAfterTierA = profileAfter["A"]["displayedTier"].asString().unwrapOr("");
            m_view.match.profileAfterTierB = profileAfter["B"]["displayedTier"].asString().unwrapOr("");
            auto const scoreA = profileAfter["A"]["visibleRankedScore"].asInt();
            auto const scoreB = profileAfter["B"]["visibleRankedScore"].asInt();
            if (scoreA.isOk()) m_view.match.profileAfterScoreA = static_cast<int>(scoreA.unwrap());
            if (scoreB.isOk()) m_view.match.profileAfterScoreB = static_cast<int>(scoreB.unwrap());
        }
        m_view.match.ratingEventA = parseRatingEvent(result["events"]["A"]);
        m_view.match.ratingEventB = parseRatingEvent(result["events"]["B"]);
        if (auto const unlocks = result["achievementUnlocks"][m_view.match.side].asArray(); unlocks.isOk()) {
            for (auto const& item : unlocks.unwrap()) {
                auto notice = parseAchievementNotice(item);
                if (!notice.achievementId.empty()) {
                    m_view.match.achievementUnlocks.push_back(std::move(notice));
                }
            }
        }
        auto const noticeContext = "match:" + m_view.match.matchId;
        if (
            !m_view.match.achievementUnlocks.empty() &&
            m_lastAchievementNoticeContext != noticeContext
        ) {
            m_lastAchievementNoticeContext = noticeContext;
            m_view.achievements.unclaimedRewardCount += static_cast<int>(
                m_view.match.achievementUnlocks.size()
            );
        }
    }

    if (!stateAllowsActiveAttempt(m_view.match.state)) {
        m_pendingProgress.reset();
        if (hasLocalAttemptInFlight()) {
            if (hasLocalOpenAttempt()) {
                // Defensive client rule: if the server ever publishes a result
                // while the visual attempt is still alive, do not destroy the
                // local attempt. The server-side lease fix should make this path
                // exceptional, but natural completion is still preferred.
                log::error(
                    "Authoritative result arrived while a local visual attempt is still open: state={}",
                    m_view.match.state
                );
            } else {
                // Never discard a completed Start/End merely because polling raced
                // ahead to a result. The server-side attempt lease/idempotency path
                // can still reconcile an event that was visually completed first.
                // Keep delivering until the authoritative endpoint ACKs or returns
                // a permanent rejection for that exact event.
                log::warn(
                    "Authoritative result arrived while completed Ranked attempt transport is still pending: state={}",
                    m_view.match.state
                );
                flushAttemptEvents();
            }
        } else {
            cleanupAttemptTransportIfIdle();
        }
    }
    if (m_view.match.spectatorActive) {
        // Spectator mode prevents *new* visual attempts. Do not destroy starts or
        // ends that were already observed before the server switched this viewer
        // into spectator mode; those events still need authoritative ACKs.
        m_pendingProgress.reset();
    }
    if (m_view.match.state == "MATCH_RESULT") {
        auto const won = m_view.match.winnerSide == m_view.match.side;
        m_view.status = won ? "Match complete: Victory" : "Match complete: Defeat";
    } else if (m_view.match.state == "CANCELLED") {
        m_view.status = "Match cancelled by the server.";
    } else if (m_view.match.state == "LAST_ATTEMPT_WINDOW") {
        m_view.status = "LAST ATTEMPT - starts must be accepted before the deadline.";
    } else if (m_view.match.state == "ROUND_SETTLING") {
        m_view.status = "LAST ATTEMPT - an accepted attempt is still in progress.";
    } else if (m_view.match.state == "FINAL_ATTEMPT_WINDOW") {
        m_view.status = "Final start window - active attempts remain valid.";
    } else if (m_view.match.state.starts_with("DEATHMATCH")) {
        m_view.status = "Deathmatch: exactly three attempts per player.";
    } else if (m_view.match.state == "BAN_PHASE") {
        m_view.status = "Choose one private ban. The opponent's choice stays hidden.";
    } else if (m_view.match.state == "MATCHED") {
        m_view.status = "Match found. Preparing ban phase...";
    } else if (m_view.match.state == "ROUND_PREPARE") {
        m_view.status = "Current map revealed. Preparing downloads...";
    } else if (m_view.match.state == "ROUND_PLAYING") {
        m_view.status = "Round live. Only attempts on the revealed map are reported.";
    } else if (m_view.match.state == "ROUND_RESULT") {
        m_view.status = "Round result locked by the server.";
    }
    m_view.error.clear();
    ++m_view.revision;
    if (previousState != m_view.match.state) {
        log::info("Corum Ranked state: {} -> {}", previousState, m_view.match.state);
        TransitionTrace::get().begin(
            fmt::format("{}:{}->{}", m_view.match.matchId, previousState, m_view.match.state),
            "server-state-received"
        );
    }
}

void RankedRuntime::applyAttemptSnapshot(matjson::Value const& root) {
    auto const deadline = root["deadlineAt"].asString();
    if (deadline.isOk()) m_view.match.deadlineAt = deadline.unwrap();

    auto const round = root["roundSnapshot"];
    if (round.isObject()) {
        auto const roundNumber = static_cast<int>(round["roundNumber"].asInt().unwrapOr(0));
        if (roundNumber == 0 || m_view.match.roundNumber == 0 || roundNumber == m_view.match.roundNumber) {
            auto const phase = round["phase"].asString().unwrapOr("");
            if (!phase.empty()) m_view.match.state = phase;

            auto const committed = round["scores"];
            auto const display = round["displayScores"].isObject() ? round["displayScores"] : committed;
            m_view.match.committedScoreA = committed["A"].asDouble().unwrapOr(m_view.match.committedScoreA);
            m_view.match.committedScoreB = committed["B"].asDouble().unwrapOr(m_view.match.committedScoreB);
            m_view.match.scoreA = display["A"].asDouble().unwrapOr(m_view.match.committedScoreA);
            m_view.match.scoreB = display["B"].asDouble().unwrapOr(m_view.match.committedScoreB);
            m_view.match.clearsA = static_cast<int>(round["clears"]["A"].asInt().unwrapOr(m_view.match.clearsA));
            m_view.match.clearsB = static_cast<int>(round["clears"]["B"].asInt().unwrapOr(m_view.match.clearsB));

            // Attempt-end acknowledgements can move a Round directly into
            // LAST_ATTEMPT_WINDOW / ROUND_SETTLING before the next state poll.
            // Apply that transition immediately so a player who just reached
            // three Clears cannot be auto-entered into an illegal extra attempt.
            auto const lastAttempt = round["lastAttemptWindow"];
            auto const inLastAttemptFlow =
                phase == "LAST_ATTEMPT_WINDOW" || phase == "ROUND_SETTLING";
            if (inLastAttemptFlow && lastAttempt.isObject()) {
                auto const trigger = lastAttempt["triggerSide"].asString().unwrapOr("");
                auto const target = lastAttempt["targetSide"].asString().unwrapOr("");
                m_view.match.spectatorActive =
                    !trigger.empty() && trigger == m_view.match.side && target != m_view.match.side;
                if (m_view.match.spectatorActive) {
                    m_view.match.spectatorOpponentName = m_view.match.opponentName;
                    m_view.match.spectatorCurrentProgress.reset();
                }
            } else if (phase == "ROUND_RESULT") {
                // A non-Last-Attempt ACK can be older than a state poll that has
                // already enabled spectator mode. Do not let such an ACK clear a
                // newer authoritative projection. Round result is the only phase
                // in this response where spectator mode is certainly finished.
                m_view.match.spectatorActive = false;
                m_view.match.spectatorCurrentProgress.reset();
                m_view.match.spectatorOpponentName.clear();
            }
        }
    }

    auto const deathmatch = root["deathmatchSnapshot"];
    if (deathmatch.isObject()) {
        auto const sequence = static_cast<int>(deathmatch["sequence"].asInt().unwrapOr(0));
        if (sequence == 0 || m_view.match.deathmatchSequence == 0 || sequence == m_view.match.deathmatchSequence) {
            if (sequence > 0) m_view.match.deathmatchSequence = sequence;
            auto const committed = deathmatch["scores"];
            auto const display = deathmatch["displayScores"].isObject() ? deathmatch["displayScores"] : committed;
            m_view.match.committedScoreA = committed["A"].asDouble().unwrapOr(m_view.match.committedScoreA);
            m_view.match.committedScoreB = committed["B"].asDouble().unwrapOr(m_view.match.committedScoreB);
            m_view.match.scoreA = display["A"].asDouble().unwrapOr(m_view.match.committedScoreA);
            m_view.match.scoreB = display["B"].asDouble().unwrapOr(m_view.match.committedScoreB);
            m_view.match.clearsA = static_cast<int>(deathmatch["clears"]["A"].asInt().unwrapOr(m_view.match.clearsA));
            m_view.match.clearsB = static_cast<int>(deathmatch["clears"]["B"].asInt().unwrapOr(m_view.match.clearsB));
            m_view.match.deathmatchAttemptsUsedA = static_cast<int>(deathmatch["attemptsUsed"]["A"].asInt().unwrapOr(m_view.match.deathmatchAttemptsUsedA));
            m_view.match.deathmatchAttemptsUsedB = static_cast<int>(deathmatch["attemptsUsed"]["B"].asInt().unwrapOr(m_view.match.deathmatchAttemptsUsedB));
            m_view.match.deathmatchAttemptsCompletedA = static_cast<int>(deathmatch["attemptsCompleted"]["A"].asInt().unwrapOr(m_view.match.deathmatchAttemptsCompletedA));
            m_view.match.deathmatchAttemptsCompletedB = static_cast<int>(deathmatch["attemptsCompleted"]["B"].asInt().unwrapOr(m_view.match.deathmatchAttemptsCompletedB));

            auto const ownServerUsed = m_view.match.side == "A"
                ? m_view.match.deathmatchAttemptsUsedA
                : m_view.match.deathmatchAttemptsUsedB;
            if (m_localDeathmatchSequence != m_view.match.deathmatchSequence) {
                m_localDeathmatchSequence = m_view.match.deathmatchSequence;
                m_localDeathmatchVisualAttempts = ownServerUsed;
            } else {
                m_localDeathmatchVisualAttempts = std::max(
                    m_localDeathmatchVisualAttempts,
                    ownServerUsed
                );
            }
        }
    }
}

void RankedRuntime::submitReady() {
    if (m_controlBusy || m_view.stage != RuntimeStage::Matched) return;
    auto const& state = m_view.match.state;
    auto const placement = isPlacement();
    if (placement) {
        if (state != "MAP_PREPARE") return;
    } else if (state != "MATCHED" && state != "ROUND_PREPARE" && state != "DEATHMATCH_PREPARE") {
        return;
    }
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, false);
        return;
    }
    matjson::Value body;
    body["installedMods"] = installedModsJson();
    auto request = baseRequest(m_sessionToken, placement ? std::string() : m_matchToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.post(endpoint(placement
            ? "/api/ranked/placement/" + m_view.match.matchId + "/ready"
            : "/api/ranked/matches/" + m_view.match.matchId + "/ready")),
        [this, placement](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setTransientError(responseError(response));
                return;
            }
            if (placement) parsePlacementState(response.json().unwrapOr(matjson::Value()));
            else parseMatchState(response.json().unwrapOr(matjson::Value()));
        }
    );
}

void RankedRuntime::submitBan(std::optional<std::string> canonicalLevelId) {
    if (m_controlBusy || m_view.stage != RuntimeStage::Matched || m_view.match.state != "BAN_PHASE") return;
    // Clear stale transport errors before this acknowledgement cycle. If this
    // request fails, setTransientError() will repopulate the error and the UI can
    // release its BANNING... state for a retry.
    m_view.error.clear();
    ++m_view.revision;
    matjson::Value body;
    if (canonicalLevelId && !canonicalLevelId->empty()) body["canonicalLevelId"] = *canonicalLevelId;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/ban")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setTransientError(responseError(response));
                return;
            }
            parseMatchState(response.json().unwrapOr(matjson::Value()));
        }
    );
}

void RankedRuntime::reportMapDownloadFailure() {
    if (m_controlBusy || m_view.stage != RuntimeStage::Matched || m_view.match.matchId.empty()) return;
    if (isPlacement()) {
        setTransientError("The exact Placement level could not be loaded. Retry the download to continue this saved Map.");
        return;
    }
    matjson::Value body;
    body["resource"] = "MAP";
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/resource-failure")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setTransientError(responseError(response));
                return;
            }
            parseMatchState(response.json().unwrapOr(matjson::Value()));
        }
    );
}

void RankedRuntime::dismissMatch() {
    if (m_view.stage != RuntimeStage::Matched) return;
    auto const placement = m_view.match.placement;
    if (placement) {
        if (m_view.match.state != "PLACEMENT_RESULT" && m_view.match.state != "CANCELLED") return;
    } else if (m_view.match.state != "MATCH_RESULT" && m_view.match.state != "CANCELLED") {
        return;
    }
    if (
        placement && m_view.match.state == "PLACEMENT_RESULT" &&
        !m_placementResultAcknowledged
    ) {
        if (m_controlBusy || m_sessionToken.empty() || m_view.match.matchId.empty()) return;
        auto const runId = m_view.match.matchId;
        auto request = baseRequest(m_sessionToken);
        m_controlBusy = true;
        m_controlRequest.spawn(
            request.post(endpoint("/api/ranked/placement/" + runId + "/result/acknowledge")),
            [this, runId](web::WebResponse response) {
                m_controlBusy = false;
                if (!successful(response)) {
                    setTransientError("Placement result acknowledgement failed: " + responseError(response));
                    return;
                }
                if (m_view.match.matchId != runId || m_view.match.state != "PLACEMENT_RESULT") return;
                m_placementResultAcknowledged = true;
                dismissMatch();
            }
        );
        return;
    }
    if (hasLocalAttemptInFlight()) {
        if (hasLocalOpenAttempt()) {
            setTransientError("A final attempt is still active; finish it before leaving the result.");
        } else {
            setTransientError("Final attempt result is still syncing; wait for server acknowledgement.");
            flushAttemptEvents();
        }
        return;
    }
    // A terminal-state poll may still be in flight when Queue Again or Close is
    // pressed. Cancel it and invalidate its callback before clearing matchToken /
    // match view, otherwise the old response can resurrect the previous match.
    invalidatePollRequest();
    invalidateAttemptRequest();
    ++m_progressRequestGeneration;
    m_progressRequest.cancel();
    m_progressBusy = false;
    m_presenceRequest.cancel();
    if (placement && m_view.match.state == "PLACEMENT_RESULT") {
        m_view.profileTier = m_view.placement.finalResult.displayedTier;
        m_view.profileScoreVisible = true;
        m_view.profileScore = m_view.placement.finalResult.visibleRankedScore;
        m_view.placement.required = false;
        m_view.placement.active = false;
        m_view.placementGames = std::max(m_view.placementGamesRequired, m_view.placement.totalStages);
    } else if (m_view.match.state == "MATCH_RESULT") {
        auto const ownSide = m_view.match.side;
        auto const& profile = ownSide == "A" ? m_view.match.profileAfterA : m_view.match.profileAfterB;
        m_view.profileTier = profile.displayedTier;
        m_view.profileScoreVisible = profile.visibleRankedScore.has_value();
        m_view.profileScore = profile.visibleRankedScore.value_or(0);
        m_view.placementGames = profile.placementGames;
        m_view.placementGamesRequired = profile.placementGamesRequired;
        m_view.placementResults = profile.placementResults;
        m_view.resultStreak = profile.resultStreak;
        m_view.promotion = profile.promotion;
        m_view.demotionWarning = profile.demotionWarning;
    }
    m_matchToken.clear();
    m_attemptId.clear();
    m_attemptLevelId = 0;
    m_attemptContextKey.clear();
    m_gameplayMap.reset();
    m_gameplayMatchId.clear();
    m_gameplayStartContextKey.clear();
    m_gameplayStartedAt.clear();
    m_gameplayStartEligible = false;
    m_pendingStart.reset();
    m_pendingEnd.reset();
    m_attemptBacklog.clear();
    ++m_attemptIntentRequestGeneration;
    m_attemptIntentRequest.cancel();
    m_attemptIntentBusy = false;
    m_attemptIntentBacklog.clear();
    m_nextIntentRetryAt = {};
    m_pendingProgress.reset();
    m_nextAttemptRetryAt = {};
    m_nextProgressAt = {};
    m_lastSubmittedProgress = -1;
    m_localDeathmatchSequence = 0;
    m_localDeathmatchVisualAttempts = 0;
    m_songBypassAllowed = false;
    m_placementResultAcknowledged = false;
    m_view.match = {};
    setStage(
        RuntimeStage::Ready,
        m_view.placement.required
            ? "Resume Placement before entering the Ranked queue."
            : "Ranked session ready."
    );
}

void RankedRuntime::queueAgain() {
    if (m_view.stage != RuntimeStage::Matched) return;
    if (isPlacement()) return;
    if (m_view.match.state != "MATCH_RESULT" && m_view.match.state != "CANCELLED") return;
    if (hasLocalAttemptInFlight()) {
        if (hasLocalOpenAttempt()) {
            setTransientError("A final attempt is still active; finish it before queueing again.");
        } else {
            setTransientError("Final attempt result is still syncing; wait for server acknowledgement.");
            flushAttemptEvents();
        }
        return;
    }
    dismissMatch();
    joinQueue();
}

void RankedRuntime::fetchHistory() {
    if (m_controlBusy || m_sessionToken.empty()) return;
    m_view.historyLoading = true;
    m_view.historyError.clear();
    ++m_view.revision;
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/matches")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            m_view.historyLoading = false;
            if (!successful(response)) {
                m_view.historyError = responseError(response);
                ++m_view.revision;
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            m_view.history.clear();
            auto const matches = root["matches"].asArray();
            if (matches.isOk()) {
                for (auto const& item : matches.unwrap()) {
                    if (!item.isObject()) continue;
                    HistoryMatchView history;
                    history.matchId = item["matchId"].asString().unwrapOr("");
                    history.finishedAt = item["finishedAt"].asString().unwrapOr("");
                    history.side = item["side"].asString().unwrapOr("");
                    history.opponentName = item["opponentName"].asString().unwrapOr("Opponent");
                    history.effectiveTier = item["effectiveTier"].asString().unwrapOr("");
                    history.winnerSide = item["winnerSide"].asString().unwrapOr("");
                    history.roundWinsA = static_cast<int>(item["series"]["roundWins"]["A"].asInt().unwrapOr(0));
                    history.roundWinsB = static_cast<int>(item["series"]["roundWins"]["B"].asInt().unwrapOr(0));
                    auto const delta = item["rankedScoreDelta"][history.side].asInt();
                    auto const rating = item["profileAfter"][history.side]["visibleRankedScore"].asInt();
                    if (delta.isOk()) history.ownRankedScoreDelta = static_cast<int>(delta.unwrap());
                    if (rating.isOk()) history.ownRankedScoreAfter = static_cast<int>(rating.unwrap());
                    auto const rounds = item["rounds"].asArray();
                    if (rounds.isOk()) {
                        for (auto const& round : rounds.unwrap()) {
                            if (round.isObject()) history.rounds.push_back(parseRoundSummary(round));
                        }
                    }
                    auto const deathmatches = item["deathmatches"].asArray();
                    if (deathmatches.isOk()) {
                        for (auto const& deathmatch : deathmatches.unwrap()) {
                            if (deathmatch.isObject()) history.deathmatches.push_back(parseDeathmatchSummary(deathmatch));
                        }
                    }
                    m_view.history.push_back(std::move(history));
                }
            }
            ++m_view.revision;
        }
    );
}

void RankedRuntime::fetchAchievements() {
    if (m_controlBusy || m_sessionToken.empty()) return;
    m_view.achievements.loading = true;
    m_view.achievements.error.clear();
    ++m_view.revision;
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/achievements")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            m_view.achievements.loading = false;
            if (!successful(response)) {
                m_view.achievements.error = responseError(response);
                ++m_view.revision;
                return;
            }
            parseAchievementState(response.json().unwrapOr(matjson::Value()));
        }
    );
}

void RankedRuntime::claimAchievement(std::string const& achievementId) {
    if (m_controlBusy || m_sessionToken.empty() || achievementId.empty()) return;
    matjson::Value body;
    body["scope"] = "ALL_PENDING";
    auto request = baseRequest(m_sessionToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    m_view.achievements.loading = true;
    m_view.achievements.error.clear();
    ++m_view.revision;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/achievements/" + achievementId + "/claim")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            m_view.achievements.loading = false;
            if (!successful(response)) {
                m_view.achievements.error = responseError(response);
                ++m_view.revision;
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            m_view.achievements.totalAp = static_cast<int>(root["totalAp"].asInt().unwrapOr(
                m_view.achievements.totalAp
            ));
            m_view.achievements.unclaimedRewardCount = static_cast<int>(
                root["unclaimedRewardCount"].asInt().unwrapOr(
                    m_view.achievements.unclaimedRewardCount
                )
            );
            fetchAchievements();
        }
    );
}

void RankedRuntime::equipAchievementTitle(
    std::string const& achievementId,
    std::optional<int> level
) {
    if (m_controlBusy || m_sessionToken.empty() || achievementId.empty()) return;
    matjson::Value body;
    body["achievementId"] = achievementId;
    if (level) body["level"] = *level;
    auto request = baseRequest(m_sessionToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    m_view.achievements.loading = true;
    m_view.achievements.error.clear();
    ++m_view.revision;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/achievements/title/equip")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            m_view.achievements.loading = false;
            if (!successful(response)) {
                m_view.achievements.error = responseError(response);
                ++m_view.revision;
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            m_view.achievements.equippedTitle = parseAchievementTitle(root["equippedTitle"]);
            ++m_view.revision;
        }
    );
}

void RankedRuntime::clearAchievementTitle() {
    if (m_controlBusy || m_sessionToken.empty()) return;
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    m_view.achievements.loading = true;
    m_view.achievements.error.clear();
    ++m_view.revision;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/achievements/title/clear")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            m_view.achievements.loading = false;
            if (!successful(response)) {
                m_view.achievements.error = responseError(response);
                ++m_view.revision;
                return;
            }
            observeServerNow(response.json().unwrapOr(matjson::Value()));
            m_view.achievements.equippedTitle = {};
            ++m_view.revision;
        }
    );
}

void RankedRuntime::parseAchievementState(matjson::Value const& root) {
    if (!root.isObject()) {
        m_view.achievements.error = "Invalid Achievement response";
        ++m_view.revision;
        return;
    }
    observeServerNow(root);
    m_view.achievements.loaded = true;
    m_view.achievements.loading = false;
    m_view.achievements.error.clear();
    m_view.achievements.totalAp = static_cast<int>(root["totalAp"].asInt().unwrapOr(0));
    m_view.achievements.unclaimedRewardCount = static_cast<int>(
        root["unclaimedRewardCount"].asInt().unwrapOr(0)
    );
    m_view.achievements.equippedTitle = parseAchievementTitle(root["equippedTitle"]);
    m_view.achievements.cards.clear();
    auto const achievements = root["achievements"].asArray();
    if (achievements.isOk()) {
        for (auto const& value : achievements.unwrap()) {
            if (!value.isObject()) continue;
            AchievementCardView card;
            card.id = value["id"].asString().unwrapOr("");
            card.type = value["type"].asString().unwrapOr("");
            card.nameKo = value["nameKo"].asString().unwrapOr("");
            card.nameEn = value["nameEn"].asString().unwrapOr("");
            card.descriptionKo = value["descriptionKo"].asString().unwrapOr("");
            card.descriptionEn = value["descriptionEn"].asString().unwrapOr("");
            card.rarity = value["rarity"].asString().unwrapOr("");
            card.titleEnabled = value["titleEnabled"].asBool().unwrapOr(false);
            card.unlocked = value["unlocked"].asBool().unwrapOr(false);
            card.claimed = value["claimed"].asBool().unwrapOr(false);
            card.rewardAp = static_cast<int>(value["rewardAp"].asInt().unwrapOr(0));
            card.claimableRewards = static_cast<int>(value["claimableRewards"].asInt().unwrapOr(0));
            card.claimableAp = static_cast<int>(value["claimableAp"].asInt().unwrapOr(0));
            auto const progress = value["progress"];
            if (progress.isObject()) {
                card.currentLevel = static_cast<int>(progress["level"].asInt().unwrapOr(0));
                card.displayLabel = progress["displayLabel"].asString().unwrapOr("");
                card.metricValue = static_cast<int>(progress["metricValue"].asInt().unwrapOr(0));
                auto const next = progress["nextThreshold"].asInt();
                if (next.isOk()) card.nextThreshold = static_cast<int>(next.unwrap());
                card.gauge = progress["gauge"].asDouble().unwrapOr(0.0);
                card.maxed = progress["maxed"].asBool().unwrapOr(false);
            }
            auto const levels = value["levels"].asArray();
            if (levels.isOk()) {
                for (auto const& item : levels.unwrap()) {
                    if (!item.isObject()) continue;
                    card.levels.push_back({
                        .level = static_cast<int>(item["level"].asInt().unwrapOr(0)),
                        .grade = item["grade"].asString().unwrapOr(""),
                        .displayLabel = item["displayLabel"].asString().unwrapOr(""),
                        .displayColor = item["displayColor"].asString().unwrapOr(""),
                        .threshold = static_cast<int>(item["threshold"].asInt().unwrapOr(0)),
                        .ap = static_cast<int>(item["ap"].asInt().unwrapOr(0)),
                    });
                }
            }
            auto const unlocks = value["unlocks"].asArray();
            if (unlocks.isOk()) {
                for (auto const& item : unlocks.unwrap()) {
                    if (!item.isObject()) continue;
                    AchievementRewardView reward;
                    auto const level = item["level"].asInt();
                    if (level.isOk()) reward.level = static_cast<int>(level.unwrap());
                    reward.rewardAp = static_cast<int>(item["rewardAp"].asInt().unwrapOr(0));
                    reward.unlockedAt = item["unlockedAt"].asString().unwrapOr("");
                    reward.claimedAt = item["claimedAt"].asString().unwrapOr("");
                    card.unlocks.push_back(std::move(reward));
                }
            }
            if (!card.id.empty()) m_view.achievements.cards.push_back(std::move(card));
        }
    }
    ++m_view.revision;
}

void RankedRuntime::observeServerNow(matjson::Value const& root) {
    auto const serverNow = root["serverNow"].asString().unwrapOr("");
    if (!serverNow.empty()) m_serverClock.observe(serverNow, localNowMillis());
}

std::optional<std::int64_t> RankedRuntime::deadlineSeconds() const {
    if (m_view.match.deadlineAt.empty()) return std::nullopt;
    return m_serverClock.remainingSeconds(m_view.match.deadlineAt, localNowMillis());
}

std::optional<std::int64_t> RankedRuntime::deadlineMillis() const {
    if (m_view.match.deadlineAt.empty()) return std::nullopt;
    return m_serverClock.remainingMillis(m_view.match.deadlineAt, localNowMillis());
}

std::optional<std::int64_t> RankedRuntime::signedDeadlineMillis() const {
    if (m_view.match.deadlineAt.empty()) return std::nullopt;
    return m_serverClock.signedRemainingMillis(m_view.match.deadlineAt, localNowMillis());
}

int RankedRuntime::currentLevelId() const {
    return m_view.match.currentMap ? m_view.match.currentMap->levelId : 0;
}

bool RankedRuntime::armCurrentLevelForGameplay() {
    if (
        m_view.stage != RuntimeStage::Matched || !m_view.match.currentMap ||
        m_view.match.spectatorActive
    ) return false;
    auto const contextKey = currentAttemptContextKey();
    if (
        m_gameplayStartEligible && m_gameplayMap &&
        m_gameplayMatchId == m_view.match.matchId &&
        m_gameplayMap->levelId == m_view.match.currentMap->levelId &&
        m_gameplayStartContextKey == contextKey
    ) {
        // A LevelInfo watchdog retry is still the same visual attempt launch.
        // Preserve its original authoritative start timestamp instead of
        // repeatedly re-arming and moving the accepted start time forward.
        return true;
    }
    if (!attemptStartWindowOpen()) return false;
    m_gameplayMap = *m_view.match.currentMap;
    m_gameplayMatchId = m_view.match.matchId;
    m_gameplayStartContextKey = contextKey;
    m_gameplayStartedAt = iso8601FromMillis(m_serverClock.serverNowMillis(localNowMillis()));
    m_gameplayStartEligible = true;
    log::debug(
        "Ranked gameplay armed: match={} level={} qualifying={} context={} startedAt={}",
        m_gameplayMatchId,
        m_gameplayMap->levelId,
        m_gameplayMap->qualifyingPercent,
        m_gameplayStartContextKey,
        m_gameplayStartedAt
    );
    return true;
}

std::optional<double> RankedRuntime::gameplayQualifyingPercent(int levelId) const {
    if (
        m_gameplayMap && m_gameplayMatchId == m_view.match.matchId &&
        m_gameplayMap->levelId == levelId
    ) {
        return m_gameplayMap->qualifyingPercent;
    }
    if (m_view.match.currentMap && m_view.match.currentMap->levelId == levelId) {
        return m_view.match.currentMap->qualifyingPercent;
    }
    return std::nullopt;
}

bool RankedRuntime::isGameplayLevel(int levelId) const {
    if (m_view.stage != RuntimeStage::Matched || m_view.match.matchId.empty()) return false;
    if (
        m_gameplayMap && m_gameplayMatchId == m_view.match.matchId &&
        m_gameplayMap->levelId == levelId
    ) return true;
    return m_view.match.currentMap && m_view.match.currentMap->levelId == levelId;
}

bool RankedRuntime::hasLocalAttemptInFlight() const {
    return
        !m_attemptId.empty() ||
        m_pendingStart.has_value() ||
        m_pendingEnd.has_value() ||
        !m_attemptBacklog.empty() ||
        m_attemptBusy;
}

bool RankedRuntime::hasLocalOpenAttempt() const {
    if (!m_attemptBacklog.empty() && !m_attemptBacklog.back().end.has_value()) return true;
    if (m_pendingEnd.has_value()) return false;
    return !m_attemptId.empty() || m_pendingStart.has_value();
}

bool RankedRuntime::hasLocalGameplayAuthority() const {
    return hasLocalOpenAttempt() || m_gameplayStartEligible;
}

void RankedRuntime::setSpectatorLocalTransportBlocked(void const* owner, bool blocked) {
    if (blocked) {
        if (m_spectatorLocalTransportOwner != owner) {
            log::info("Spectator attempt transport suppressed");
            ++m_progressRequestGeneration;
            m_progressRequest.cancel();
            m_progressBusy = false;
        }
        m_spectatorLocalTransportOwner = owner;
        m_pendingProgress.reset();
    } else if (m_spectatorLocalTransportOwner == owner) {
        m_spectatorLocalTransportOwner = nullptr;
    }
}

std::string RankedRuntime::currentAttemptContextKey() const {
    if (m_view.match.matchId.empty()) return {};
    if (isPlacement()) {
        return fmt::format(
            "{}:P:{}:{}",
            m_view.match.matchId,
            m_view.placement.currentStageIndex,
            m_view.placement.currentMapIndex
        );
    }
    if (m_view.match.deathmatchSequence > 0) {
        return fmt::format("{}:D:{}", m_view.match.matchId, m_view.match.deathmatchSequence);
    }
    if (m_view.match.roundNumber > 0) {
        return fmt::format("{}:R:{}", m_view.match.matchId, m_view.match.roundNumber);
    }
    return fmt::format("{}:NONE", m_view.match.matchId);
}

double RankedRuntime::optimisticScoreForContext(std::string const& contextKey) const {
    if (contextKey.empty()) return 0.0;
    double total = 0.0;
    if (m_pendingEnd && m_pendingEnd->contextKey == contextKey) {
        total += m_pendingEnd->optimisticScore;
    }
    for (auto const& queued : m_attemptBacklog) {
        if (queued.end && queued.end->contextKey == contextKey) {
            total += queued.end->optimisticScore;
        }
    }
    return total;
}

int RankedRuntime::optimisticClearsForContext(std::string const& contextKey) const {
    if (contextKey.empty()) return 0;
    int total = 0;
    if (m_pendingEnd && m_pendingEnd->contextKey == contextKey) {
        total += m_pendingEnd->optimisticClear;
    }
    for (auto const& queued : m_attemptBacklog) {
        if (queued.end && queued.end->contextKey == contextKey) {
            total += queued.end->optimisticClear;
        }
    }
    return total;
}

bool RankedRuntime::canEnterCurrentLevel() const {
    if (m_view.stage != RuntimeStage::Matched || !m_view.match.currentMap || m_view.match.spectatorActive) return false;
    // Scene entry creates a NEW visual attempt. ROUND_SETTLING allows an already
    // accepted attempt to finish, but must never be used to enter/re-enter PlayLayer.
    if (!attemptStartWindowOpen()) return false;

    if (isPlacement()) {
        return m_view.placement.currentMapClears < 2;
    }

    if (m_view.match.state == "DEATHMATCH_PLAYING") {
        auto const serverUsed = m_view.match.side == "A"
            ? m_view.match.deathmatchAttemptsUsedA
            : m_view.match.deathmatchAttemptsUsedB;
        auto const localUsed = m_localDeathmatchSequence == m_view.match.deathmatchSequence
            ? m_localDeathmatchVisualAttempts
            : 0;
        // This is evaluated from LevelInfoLayer before creating a new PlayLayer.
        // The currently-running third attempt is already inside PlayLayer, so a
        // local/server visual count of 3 means a fourth scene must never begin.
        if (std::max(serverUsed, localUsed) >= 3) return false;
        return true;
    }

    // A Clear is shown optimistically before its /attempt/end acknowledgement.
    // If that pending Clear would make this side reach three Clears, do not let
    // LevelInfoLayer auto-enter another PlayLayer while waiting for the server
    // to transition into LAST_ATTEMPT_WINDOW / ROUND_SETTLING. This closes the
    // clear->LevelInfo->re-enter race that could create an illegal fourth Clear.
    auto const committedClears = m_view.match.side == "A" ? m_view.match.clearsA : m_view.match.clearsB;
    auto const contextKey = currentAttemptContextKey();
    if (
        attemptStartWindowOpen() &&
        committedClears + optimisticClearsForContext(contextKey) >= 3 &&
        hasLocalAttemptInFlight()
    ) return false;

    return true;
}

bool RankedRuntime::canEnterCurrentLevelAsSpectator() const {
    if (!isSpectating()) return false;
    // A viewer may reconnect before the opponent starts their next legal
    // Attempt. Keep the exact canonical map open in that waiting state; the
    // live relay will attach only after authority publishes an active Attempt.
    return spectatorLevelEntryAllowed({
        .matched = m_view.stage == RuntimeStage::Matched,
        .hasCurrentMap = m_view.match.currentMap.has_value(),
        .hasMatchId = !m_view.match.matchId.empty(),
        .placement = m_view.match.placement,
        .spectatorActive = m_view.match.spectatorActive,
        .state = m_view.match.state,
    });
}

int RankedRuntime::localDeathmatchVisualAttemptsUsed() const {
    if (m_localDeathmatchSequence != m_view.match.deathmatchSequence) return 0;
    return std::clamp(m_localDeathmatchVisualAttempts, 0, 3);
}

double RankedRuntime::localDisplayScore(double progressPercent, std::optional<double> qualifyingPercentOverride) const {
    if (isSpectating() || m_spectatorLocalTransportOwner != nullptr) progressPercent = -1.0;
    if (isPlacement()) {
        // The score may preview a server-calculated Top 2 replacement while the
        // authoritative Grade remains frozen until Attempt End.
        return static_cast<double>(m_view.placement.provisionalMapScore);
    }
    auto const ownIsA = m_view.match.side == "A";
    if (m_view.match.state == "DEATHMATCH_PLAYING") {
        auto const serverBest = ownIsA ? m_view.match.scoreA : m_view.match.scoreB;
        if (progressPercent < 0.0) return serverBest;
        return std::max(serverBest, std::clamp(progressPercent, 0.0, 100.0));
    }
    auto const committed = ownIsA ? m_view.match.committedScoreA : m_view.match.committedScoreB;
    auto const serverDisplay = ownIsA ? m_view.match.scoreA : m_view.match.scoreB;
    auto const contextKey = currentAttemptContextKey();
    auto const optimisticBase = committed + optimisticScoreForContext(contextKey);

    // Every Clear is worth exactly 250 under the current rules. Keep that
    // invariant visible even while attempt-end/state acknowledgements are queued.
    auto const clearFloor = static_cast<double>(localDisplayClears()) * 250.0;

    if (progressPercent < 0.0 || m_view.match.spectatorActive) {
        return std::max({serverDisplay, optimisticBase, clearFloor});
    }

    RankedMapView const* scoringMap = nullptr;
    if (m_gameplayMap && m_gameplayMatchId == m_view.match.matchId) {
        scoringMap = &*m_gameplayMap;
    } else if (m_view.match.currentMap) {
        scoringMap = &*m_view.match.currentMap;
    }

    auto const overrideValid =
        qualifyingPercentOverride && std::isfinite(*qualifyingPercentOverride) &&
        *qualifyingPercentOverride >= 0.0 && *qualifyingPercentOverride <= 100.0;
    auto const qualifying = overrideValid
        ? *qualifyingPercentOverride
        : (scoringMap ? scoringMap->qualifyingPercent : -1.0);
    if (qualifying < 0.0) return std::max({serverDisplay, optimisticBase, clearFloor});

    auto const live = calculateAttemptScore(progressPercent, false, qualifying);
    return std::max({serverDisplay, optimisticBase + live, clearFloor});
}

int RankedRuntime::localDisplayClears() const {
    if (isPlacement()) return m_view.placement.currentMapClears;
    auto const committed = m_view.match.side == "A" ? m_view.match.clearsA : m_view.match.clearsB;
    return std::max(0, committed + optimisticClearsForContext(currentAttemptContextKey()));
}

bool RankedRuntime::canTrackLevel(int levelId) const {
    return
        m_view.stage == RuntimeStage::Matched &&
        stateAllowsActiveAttempt(m_view.match.state) &&
        !isSpectating() &&
        isGameplayLevel(levelId);
}

bool RankedRuntime::canFinishTrackedLevel(int levelId) const {
    if (m_view.stage != RuntimeStage::Matched || m_view.match.matchId.empty()) return false;
    auto const sameGameplayMap =
        m_gameplayMap && m_gameplayMatchId == m_view.match.matchId &&
        m_gameplayMap->levelId == levelId;
    auto const currentMapMatches =
        m_view.match.currentMap && m_view.match.currentMap->levelId == levelId;
    if (!sameGameplayMap && !currentMapMatches) return false;

    // A visual attempt that already owns a local transport slot must be allowed
    // to queue its End even if polling has just advanced to a result/spectator
    // state. The server remains authoritative and may accept/reject it using the
    // original event timestamp.
    return
        !m_attemptId.empty() || m_pendingStart.has_value() ||
        m_pendingEnd.has_value() || !m_attemptBacklog.empty() || m_attemptBusy;
}

bool RankedRuntime::attemptStartWindowOpen() const {
    auto const& state = m_view.match.state;
    if (isPlacement()) {
        if (state != "MAP_PLAYING") return false;
        auto const signedRemaining = signedDeadlineMillis();
        return !signedRemaining || *signedRemaining > -10'000;
    }
    if (state == "DEATHMATCH_PLAYING") return true;

    if (state == "ROUND_PLAYING") {
        // deadlineAt is the five-minute deadline while authority still reports
        // ROUND_PLAYING. The rules permit new starts for exactly ten more seconds.
        // A missing clock remains server-authoritative instead of locally locking
        // the player out; the Start endpoint can still reject a late event.
        auto const signedRemaining = signedDeadlineMillis();
        return !signedRemaining || *signedRemaining > -10'000;
    }

    if (state == "FINAL_ATTEMPT_WINDOW" || state == "LAST_ATTEMPT_WINDOW") {
        auto const remaining = deadlineMillis();
        return !remaining || *remaining > 0;
    }
    return false;
}

void RankedRuntime::cleanupAttemptTransportIfIdle() {
    if (hasLocalAttemptInFlight()) return;
    m_attemptId.clear();
    m_attemptLevelId = 0;
    m_attemptContextKey.clear();
    m_pendingStart.reset();
    m_pendingEnd.reset();
    m_attemptBacklog.clear();
    ++m_attemptIntentRequestGeneration;
    m_attemptIntentRequest.cancel();
    m_attemptIntentBusy = false;
    m_attemptIntentBacklog.clear();
    m_pendingProgress.reset();
    m_lastSubmittedProgress = -1;
    if (
        m_view.match.state == "MATCH_RESULT" || m_view.match.state == "CANCELLED" ||
        m_view.match.state == "ROUND_RESULT" || m_view.match.state == "DEATHMATCH_RESULT" ||
        m_view.match.state == "MAP_RESULT" || m_view.match.state == "STAGE_RESULT" ||
        m_view.match.state == "PLACEMENT_RESULT"
    ) {
        m_gameplayMap.reset();
        m_gameplayMatchId.clear();
        m_gameplayStartContextKey.clear();
        m_gameplayStartedAt.clear();
        m_gameplayStartEligible = false;
    }
}

bool RankedRuntime::isSpectating() const {
    // A poll/ACK must not cut off a live FINAL/LAST/DM or armed scene Attempt.
    // Completed journal entries may still await ACK; those are not live gameplay.
    return spectatorAuthorityReady(m_view.match.spectatorActive, isPlacement(), hasLocalGameplayAuthority());
}

void RankedRuntime::setSongBypassAllowed(bool allowed) {
    m_songBypassAllowed = allowed;
}

bool RankedRuntime::songBypassAllowed() const {
    return m_songBypassAllowed;
}

std::string RankedRuntime::newEventId(std::string_view kind) {
    ++m_eventSequence;
    return fmt::format("{}-{}-{}", kind, localNowMillis(), m_eventSequence);
}

bool RankedRuntime::reportAttemptStart(int levelId) {
    if (spectatorAttemptTransportBlocked(isSpectating(), m_spectatorLocalTransportOwner != nullptr)) return false;
    auto const armedSceneStart =
        m_gameplayStartEligible && m_gameplayMap &&
        m_gameplayMatchId == m_view.match.matchId &&
        m_gameplayMap->levelId == levelId &&
        !m_gameplayStartContextKey.empty();
    if ((!canTrackLevel(levelId) || !attemptStartWindowOpen()) && !armedSceneStart) return false;

    // A PlayLayer can retry this call while waiting for the transport. Treat an
    // already registered local start as success instead of creating duplicates.
    if (
        !m_attemptId.empty() && m_attemptLevelId == levelId &&
        !m_pendingEnd && m_attemptBacklog.empty()
    ) {
        if (armedSceneStart) m_gameplayStartEligible = false;
        return true;
    }
    if (
        m_pendingStart && m_pendingStart->levelId == levelId &&
        !m_pendingEnd && m_attemptBacklog.empty()
    ) {
        if (armedSceneStart) m_gameplayStartEligible = false;
        return true;
    }

    if (m_view.match.state == "DEATHMATCH_PLAYING") {
        auto const sequence = m_view.match.deathmatchSequence;
        if (sequence <= 0) return false;
        auto const serverUsed = m_view.match.side == "A"
            ? m_view.match.deathmatchAttemptsUsedA
            : m_view.match.deathmatchAttemptsUsedB;
        if (m_localDeathmatchSequence != sequence) {
            m_localDeathmatchSequence = sequence;
            m_localDeathmatchVisualAttempts = serverUsed;
        } else {
            m_localDeathmatchVisualAttempts = std::max(m_localDeathmatchVisualAttempts, serverUsed);
        }
        // Count an attempt exactly when a new visual Start journal entry is
        // created. The old pre-reservation path counted the next reset before the
        // visual attempt actually existed, which could make attempt 3 look spent
        // while the server correctly still showed 2/3.
        if (m_localDeathmatchVisualAttempts >= 3) return false;
        ++m_localDeathmatchVisualAttempts;
        ++m_view.revision;
    }

    auto const observedStartMillis = m_serverClock.serverNowMillis(localNowMillis());
    auto const startContext = armedSceneStart ? m_gameplayStartContextKey : currentAttemptContextKey();
    auto const clientStartedAt = armedSceneStart && !m_gameplayStartedAt.empty()
        ? m_gameplayStartedAt
        : iso8601FromMillis(observedStartMillis);
    PendingStart start {
        .levelId = levelId,
        .eventId = newEventId("start"),
        .clientStartedAt = clientStartedAt,
        .contextKey = startContext,
    };
    if (armedSceneStart) m_gameplayStartEligible = false;

    // Start/End delivery remains FIFO, but every standard-round visual Start now
    // publishes an out-of-band lease immediately. That lease is not a score event;
    // it only proves that a visual attempt began before the authoritative deadline
    // while an earlier End/ACK may still be ahead of it in the FIFO. This closes
    // the Round-2 fast-clear race where polling could finalize the round and the
    // client would otherwise lose the queued Clear entirely.
    if (!isPlacement() && m_view.match.state != "DEATHMATCH_PLAYING") {
        sendAttemptStartIntent(start);
    }

    // Geometry Dash may visually reset into the next attempt before the previous
    // HTTP end acknowledgement arrives. Never overwrite that older transport.
    // Store every later visual attempt in FIFO order instead.
    if (
        !m_attemptId.empty() || m_pendingStart || m_pendingEnd ||
        m_attemptBusy || !m_attemptBacklog.empty()
    ) {
        m_attemptBacklog.push_back(QueuedAttempt {
            .start = std::move(start),
            .end = std::nullopt,
        });
    } else {
        m_pendingStart = std::move(start);
    }
    log::debug("Ranked attempt start queued: level={} backlog={}", levelId, m_attemptBacklog.size());
    flushAttemptEvents();
    return true;
}

void RankedRuntime::reportPracticePresence(bool practice) {
    if (
        isSpectating() || m_spectatorLocalTransportOwner != nullptr ||
        isPlacement() || m_view.stage != RuntimeStage::Matched ||
        m_view.match.matchId.empty() || m_matchToken.empty()
    ) return;
    matjson::Value body;
    body["practice"] = practice;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_presenceRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/presence")),
        [this](web::WebResponse response) {
            if (!successful(response)) {
                log::warn("Ranked Practice presence sync failed: {}", responseError(response));
                return;
            }
            observeServerNow(response.json().unwrapOr(matjson::Value()));
        }
    );
}

void RankedRuntime::reportAttemptProgress(int levelId, double progressPercent) {
    if (spectatorAttemptTransportBlocked(isSpectating(), m_spectatorLocalTransportOwner != nullptr)) return;
    // Keep the latest progress even while /attempt/start is waiting for its ACK.
    // alpha.17 discarded every progress update before m_attemptId existed and
    // then erased the buffered value on start ACK, which could leave fast levels
    // at Score 0 for their entire attempt.
    if (!canTrackLevel(levelId) || m_pendingEnd || !m_attemptBacklog.empty()) return;
    auto const ownsTransport =
        (!m_attemptId.empty() && m_attemptLevelId == levelId) ||
        (m_pendingStart && m_pendingStart->levelId == levelId);
    if (!ownsTransport) return;
    auto const progress = std::clamp(static_cast<int>(std::floor(progressPercent)), 0, 100);
    if (progress == m_lastSubmittedProgress && !m_pendingProgress) return;
    m_pendingProgress = progress;
    flushProgressTelemetry();
}

bool RankedRuntime::reportAttemptEnd(int levelId, double progressPercent, bool cleared, std::optional<double> qualifyingPercentOverride) {
    if (spectatorAttemptTransportBlocked(isSpectating(), m_spectatorLocalTransportOwner != nullptr)) return false;
    if (!canTrackLevel(levelId) && !canFinishTrackedLevel(levelId)) return false;

    // Self-heal a missed init-time start. PlayLayer can be created on the same
    // frame as a state refresh; if the first reportAttemptStart() was skipped,
    // never silently discard the eventual death/Clear. Queue a start first while
    // starts are still legal and attach this end to it.
    if (
        m_attemptId.empty() && !m_pendingStart && !m_attemptBusy &&
        m_attemptBacklog.empty()
    ) {
        if (!reportAttemptStart(levelId)) {
            log::warn("Ranked attempt end could not recover a missing start: level={}", levelId);
            return false;
        }
    }

    auto const finalProgress = normalizeAttemptProgress(progressPercent);
    RankedMapView const* scoringMap = nullptr;
    if (m_gameplayMap && m_gameplayMatchId == m_view.match.matchId && m_gameplayMap->levelId == levelId) {
        scoringMap = &*m_gameplayMap;
    } else if (m_view.match.currentMap && m_view.match.currentMap->levelId == levelId) {
        scoringMap = &*m_view.match.currentMap;
    }
    auto const overrideValid =
        qualifyingPercentOverride && std::isfinite(*qualifyingPercentOverride) &&
        *qualifyingPercentOverride >= 0.0 && *qualifyingPercentOverride <= 100.0;
    auto const qualifying = overrideValid
        ? *qualifyingPercentOverride
        : (scoringMap ? scoringMap->qualifyingPercent : -1.0);
    auto optimisticScore = 0.0;
    if (m_view.match.state == "DEATHMATCH_PLAYING") {
        // Death Match is raw best percentage; Qualifying is intentionally absent.
        optimisticScore = cleared ? 100.0 : finalProgress;
    } else if (qualifying >= 0.0) {
        optimisticScore = calculateAttemptScore(
            finalProgress,
            cleared,
            qualifying
        );
    }
    auto const observedEndMillis = m_serverClock.serverNowMillis(localNowMillis());
    std::string contextKey = currentAttemptContextKey();
    if (!m_attemptBacklog.empty()) contextKey = m_attemptBacklog.back().start.contextKey;
    else if (m_pendingStart) contextKey = m_pendingStart->contextKey;
    else if (!m_attemptContextKey.empty()) contextKey = m_attemptContextKey;

    PendingEnd end {
        .levelId = levelId,
        .progressPercent = finalProgress,
        .cleared = cleared,
        .eventId = newEventId("end"),
        .clientEndedAt = iso8601FromMillis(observedEndMillis),
        .contextKey = contextKey,
        .optimisticScore = optimisticScore,
        .optimisticClear = cleared ? 1 : 0,
    };

    if (!m_attemptBacklog.empty()) {
        // The newest queued start is the visual attempt that just ended. Starts
        // and ends are observed in gameplay order (destroy/complete before reset),
        // so attaching to the FIFO tail preserves exact attempt identity.
        if (m_attemptBacklog.back().end) return true;
        m_attemptBacklog.back().end = end;
    } else {
        if (m_pendingEnd) return true; // duplicate completion hook for current attempt
        if (m_attemptId.empty() && !m_pendingStart && !m_attemptBusy) return false;
        m_pendingEnd = end;
    }

    ++m_view.revision;
    // Do not leave a best-effort progress POST competing with the authoritative
    // End on iOS. Cancelling it is safe: End carries the exact final progress,
    // and its clientEventId is the idempotent score journal entry.
    ++m_progressRequestGeneration;
    m_progressRequest.cancel();
    m_progressBusy = false;
    m_pendingProgress.reset();
    log::debug(
        "Ranked attempt end queued: level={} progress={} cleared={} optimisticScore={}",
        levelId,
        finalProgress,
        cleared,
        optimisticScore
    );
    flushAttemptEvents();
    return true;
}

void RankedRuntime::promoteQueuedAttempt() {
    if (
        m_attemptBusy || !m_attemptId.empty() || m_pendingStart || m_pendingEnd ||
        m_attemptBacklog.empty()
    ) return;

    auto next = std::move(m_attemptBacklog.front());
    m_attemptBacklog.pop_front();
    m_pendingStart = std::move(next.start);
    m_pendingEnd = std::move(next.end);
}

void RankedRuntime::flushAttemptEvents() {
    if (m_attemptBusy || std::chrono::steady_clock::now() < m_nextAttemptRetryAt) return;
    promoteQueuedAttempt();
    if (!m_attemptId.empty() && m_pendingEnd) {
        sendAttemptEnd();
        return;
    }
    if (m_attemptId.empty() && m_pendingStart) {
        // Once a visual start has been journaled, always deliver it. Polling may
        // already show ROUND_RESULT by the time the HTTP request gets a turn.
        sendAttemptStart();
    }
}

void RankedRuntime::sendAttemptStartIntent(PendingStart const& start) {
    if (m_view.match.matchId.empty() || start.clientStartedAt.empty()) return;
    auto const exists = std::any_of(
        m_attemptIntentBacklog.begin(),
        m_attemptIntentBacklog.end(),
        [&start](PendingStart const& queued) { return queued.eventId == start.eventId; }
    );
    if (exists) return;

    // The lease exists specifically to bypass the serialized Start/End FIFO. A
    // slow older intent must therefore never head-of-line block the NEW visual
    // attempt that is currently at risk of crossing the Round deadline. Preempt
    // the old HTTP wait, put the newest lease first, then reconcile older leases
    // after it. Server-side idempotency makes a request that raced cancellation
    // harmless.
    if (m_attemptIntentBusy) {
        ++m_attemptIntentRequestGeneration;
        m_attemptIntentRequest.cancel();
        m_attemptIntentBusy = false;
        m_nextIntentRetryAt = {};
        m_attemptIntentBacklog.push_front(start);
    } else {
        m_attemptIntentBacklog.push_back(start);
    }
    flushAttemptStartIntents();
}

void RankedRuntime::flushAttemptStartIntents() {
    if (
        m_attemptIntentBusy || m_attemptIntentBacklog.empty() ||
        std::chrono::steady_clock::now() < m_nextIntentRetryAt
    ) return;
    auto const start = m_attemptIntentBacklog.front();
    auto const requestGeneration = ++m_attemptIntentRequestGeneration;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", start.levelId);
    body["clientEventId"] = start.eventId;
    body["clientStartedAt"] = start.clientStartedAt;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_attemptIntentBusy = true;
    m_attemptIntentRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/attempt/intent")),
        [this, start, requestGeneration](web::WebResponse response) {
            if (requestGeneration != m_attemptIntentRequestGeneration) return;
            m_attemptIntentBusy = false;
            auto const sameFront =
                !m_attemptIntentBacklog.empty() &&
                m_attemptIntentBacklog.front().eventId == start.eventId;
            if (!successful(response)) {
                auto const code = response.code();
                auto const permanent = code >= 400 && code < 500 && code != 429;
                if (permanent) {
                    log::error(
                        "Ranked attempt intent permanently rejected: level={} event={} status={} reason={}",
                        start.levelId, start.eventId, code, responseError(response)
                    );
                    if (sameFront) m_attemptIntentBacklog.pop_front();
                    m_nextIntentRetryAt = {};
                } else {
                    log::warn(
                        "Ranked attempt intent retrying: level={} event={} status={} reason={}",
                        start.levelId, start.eventId, code, responseError(response)
                    );
                    m_nextIntentRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                }
                flushAttemptStartIntents();
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            if (!root["accepted"].asBool().unwrapOr(false)) {
                log::warn(
                    "Ranked attempt start intent rejected: level={} event={} reason={}",
                    start.levelId,
                    start.eventId,
                    root["reason"].asString().unwrapOr("unknown")
                );
            }
            if (sameFront) m_attemptIntentBacklog.pop_front();
            m_nextIntentRetryAt = {};
            flushAttemptStartIntents();
        }
    );
}

void RankedRuntime::sendAttemptStart() {
    if (!m_pendingStart) return;
    auto const starting = *m_pendingStart;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", starting.levelId);
    body["clientEventId"] = starting.eventId;
    if (!starting.clientStartedAt.empty()) body["clientStartedAt"] = starting.clientStartedAt;
    auto const placement = isPlacement();
    auto request = baseRequest(m_sessionToken, placement ? std::string() : m_matchToken);
    request.bodyJSON(body);
    auto const requestGeneration = ++m_attemptRequestGeneration;
    m_attemptBusy = true;
    m_attemptRequestStartedAt = std::chrono::steady_clock::now();
    log::debug(
        "Sending Ranked attempt start: level={} event={}",
        starting.levelId,
        starting.eventId
    );
    m_attemptRequest.spawn(
        request.post(endpoint(placement
            ? "/api/ranked/placement/" + m_view.match.matchId + "/attempt/start"
            : "/api/ranked/matches/" + m_view.match.matchId + "/attempt/start")),
        [this, starting, requestGeneration, placement](web::WebResponse response) {
            if (requestGeneration != m_attemptRequestGeneration) return;
            m_attemptBusy = false;
            m_attemptRequestStartedAt = {};
            if (!successful(response)) {
                auto const reason = responseError(response);
                if (permanentClientError(response.code())) {
                    log::error(
                        "Ranked attempt start permanently rejected: status={} level={} event={} reason={}",
                        response.code(),
                        starting.levelId,
                        starting.eventId,
                        reason
                    );
                    setTransientError("Attempt start rejected: " + reason);
                    m_pendingStart.reset();
                    m_pendingEnd.reset();
                    m_attemptId.clear();
                    m_attemptLevelId = 0;
                    m_attemptContextKey.clear();
                    m_pendingProgress.reset();
                    m_lastSubmittedProgress = -1;
                    m_nextAttemptRetryAt = {};
                    promoteQueuedAttempt();
                    cleanupAttemptTransportIfIdle();
                    ++m_view.revision;
                    flushAttemptEvents();
                    return;
                }
                log::warn(
                    "Ranked attempt start transient failure: status={} level={} event={} reason={}",
                    response.code(),
                    starting.levelId,
                    starting.eventId,
                    reason
                );
                setTransientError("Attempt start retrying: " + reason);
                m_nextAttemptRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            if (!root["accepted"].asBool().unwrapOr(false)) {
                setTransientError(root["reason"].asString().unwrapOr("Attempt start rejected"));
                applyAttemptSnapshot(root);
                m_pendingStart.reset();
                m_pendingEnd.reset();
                m_attemptLevelId = 0;
                m_pendingProgress.reset();
                m_lastSubmittedProgress = -1;
                m_nextAttemptRetryAt = {};
                promoteQueuedAttempt();
                cleanupAttemptTransportIfIdle();
                ++m_view.revision;
                flushAttemptEvents();
                return;
            }
            m_attemptId = root["attemptId"].asString().unwrapOr("");
            if (placement) m_view.placement.activeAttemptId = m_attemptId;
            m_attemptLevelId = starting.levelId;
            m_attemptContextKey = starting.contextKey;
            log::info(
                "Ranked attempt start ACK: accepted=true attempt={} level={}",
                m_attemptId,
                starting.levelId
            );
            applyAttemptSnapshot(root);
            if (m_view.match.state == "DEATHMATCH_PLAYING") {
                auto const attemptNumber = static_cast<int>(root["attemptNumber"].asInt().unwrapOr(0));
                if (attemptNumber > 0) {
                    if (m_view.match.side == "A") m_view.match.deathmatchAttemptsUsedA = std::max(m_view.match.deathmatchAttemptsUsedA, attemptNumber);
                    else m_view.match.deathmatchAttemptsUsedB = std::max(m_view.match.deathmatchAttemptsUsedB, attemptNumber);
                }
            }
            m_pendingStart.reset();
            // Preserve m_pendingProgress collected while start ACK was in flight.
            // It belongs to this exact visual attempt and can now be transmitted.
            m_lastSubmittedProgress = -1;
            m_nextAttemptRetryAt = {};
            ++m_view.revision;
            // m_pendingEnd may already contain a fast visual death/Clear that
            // happened before this start acknowledgement; send it immediately.
            SpectatorLiveRuntime::get().tickTerminal();
            flushAttemptEvents();
            flushProgressTelemetry();
        }
    );
}

void RankedRuntime::sendAttemptEnd() {
    if (!m_pendingEnd || m_attemptId.empty()) return;
    auto const ending = *m_pendingEnd;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", ending.levelId);
    body["attemptId"] = m_attemptId;
    body["clientEventId"] = ending.eventId;
    body["progressPercent"] = ending.progressPercent;
    body["cleared"] = ending.cleared;
    if (!ending.clientEndedAt.empty()) body["clientEndedAt"] = ending.clientEndedAt;
    auto const placement = isPlacement();
    auto request = baseRequest(m_sessionToken, placement ? std::string() : m_matchToken);
    request.bodyJSON(body);
    auto const requestGeneration = ++m_attemptRequestGeneration;
    m_attemptBusy = true;
    m_attemptRequestStartedAt = std::chrono::steady_clock::now();
    log::debug(
        "Sending Ranked attempt end: attempt={} level={} event={}",
        m_attemptId,
        ending.levelId,
        ending.eventId
    );
    m_attemptRequest.spawn(
        request.post(endpoint(placement
            ? "/api/ranked/placement/" + m_view.match.matchId + "/attempt/end"
            : "/api/ranked/matches/" + m_view.match.matchId + "/attempt/end")),
        [this, ending, requestGeneration, placement](web::WebResponse response) {
            if (requestGeneration != m_attemptRequestGeneration) return;
            m_attemptBusy = false;
            m_attemptRequestStartedAt = {};
            if (!successful(response)) {
                auto const reason = responseError(response);
                if (permanentClientError(response.code())) {
                    log::error(
                        "Ranked attempt end permanently rejected: status={} attempt={} level={} event={} reason={}",
                        response.code(),
                        m_attemptId,
                        ending.levelId,
                        ending.eventId,
                        reason
                    );
                    setTransientError("Attempt end rejected: " + reason);
                    m_attemptId.clear();
                    m_attemptLevelId = 0;
                    m_attemptContextKey.clear();
                    m_pendingEnd.reset();
                    m_pendingProgress.reset();
                    m_lastSubmittedProgress = -1;
                    m_nextAttemptRetryAt = {};
                    promoteQueuedAttempt();
                    cleanupAttemptTransportIfIdle();
                    ++m_view.revision;
                    flushAttemptEvents();
                    return;
                }
                log::warn(
                    "Ranked attempt end transient failure: status={} attempt={} level={} event={} reason={}",
                    response.code(),
                    m_attemptId,
                    ending.levelId,
                    ending.eventId,
                    reason
                );
                setTransientError("Attempt end retrying: " + reason);
                m_nextAttemptRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            auto const accepted = root["accepted"].asBool().unwrapOr(false);
            auto const reason = root["reason"].asString().unwrapOr("");
            auto const awardedScore = root["awardedScore"].asDouble().unwrapOr(0.0);
            log::info(
                "Ranked attempt end ACK: accepted={} level={} progress={} clear={} awarded={} reason={}",
                accepted,
                ending.levelId,
                ending.progressPercent,
                ending.cleared,
                awardedScore,
                reason
            );
            if (!accepted && reason != "ATTEMPT_ALREADY_ENDED") {
                setTransientError(reason.empty() ? "Attempt end rejected" : reason);
            }
            if (placement) {
                auto const mapScore = root["mapScore"].asDouble();
                if (mapScore.isOk()) {
                    m_view.placement.currentMapScore = mapScore.unwrap();
                    m_view.placement.provisionalMapScore = m_view.placement.currentMapScore;
                    m_view.match.scoreA = m_view.placement.currentMapScore;
                    m_view.match.committedScoreA = m_view.placement.currentMapScore;
                }
                auto const mapGrade = root["mapGrade"].asString();
                if (mapGrade.isOk()) m_view.placement.currentMapGrade = mapGrade.unwrap();
                auto const clears = root["clearCount"].asInt();
                if (clears.isOk()) {
                    m_view.placement.currentMapClears = static_cast<int>(clears.unwrap());
                    m_view.match.clearsA = m_view.placement.currentMapClears;
                }
                m_view.placement.activeAttemptId.clear();
            } else {
                applyAttemptSnapshot(root);
            }
            m_attemptId.clear();
            m_attemptLevelId = 0;
            m_attemptContextKey.clear();
            m_pendingEnd.reset();
            m_pendingProgress.reset();
            m_lastSubmittedProgress = -1;
            m_nextAttemptRetryAt = {};
            m_nextPollAt = std::chrono::steady_clock::now();
            promoteQueuedAttempt();
            cleanupAttemptTransportIfIdle();
            ++m_view.revision;
            flushAttemptEvents();
        }
    );
}

void RankedRuntime::flushProgressTelemetry() {
    if (spectatorAttemptTransportBlocked(isSpectating(), m_spectatorLocalTransportOwner != nullptr)) return;
    if (
        m_progressBusy || !m_pendingProgress || m_attemptId.empty() || m_pendingEnd ||
        std::chrono::steady_clock::now() < m_nextProgressAt
    ) return;
    sendAttemptProgress();
}

void RankedRuntime::sendAttemptProgress() {
    if (spectatorAttemptTransportBlocked(isSpectating(), m_spectatorLocalTransportOwner != nullptr)) return;
    if (!m_pendingProgress || m_attemptId.empty()) return;
    auto const progress = *m_pendingProgress;
    auto const attemptId = m_attemptId;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", m_attemptLevelId);
    body["attemptId"] = attemptId;
    body["progressPercent"] = progress;
    auto const placement = isPlacement();
    auto request = baseRequest(m_sessionToken, placement ? std::string() : m_matchToken);
    request.bodyJSON(body);
    auto const requestGeneration = ++m_progressRequestGeneration;
    m_progressBusy = true;
    m_nextProgressAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    m_progressRequest.spawn(
        request.post(endpoint(placement
            ? "/api/ranked/placement/" + m_view.match.matchId + "/attempt/progress"
            : "/api/ranked/matches/" + m_view.match.matchId + "/attempt/progress")),
        [this, attemptId, progress, requestGeneration, placement](web::WebResponse response) {
            if (requestGeneration != m_progressRequestGeneration) return;
            m_progressBusy = false;
            if (successful(response)) {
                auto const root = response.json().unwrapOr(matjson::Value());
                observeServerNow(root);
                // Progress is intentionally best-effort telemetry. If this response
                // arrives after the visual attempt has already ended (or after the
                // transport advanced to another attempt), its live snapshot is stale
                // and must never overwrite a newer Clear/end snapshot.
                if (m_attemptId == attemptId && !m_pendingEnd) {
                    if (placement) {
                        auto const provisional = root["provisionalMapScore"].asDouble();
                        if (provisional.isOk()) {
                            m_view.placement.provisionalMapScore = provisional.unwrap();
                            m_view.match.scoreA = m_view.placement.provisionalMapScore;
                        }
                        auto const provisionalGrade = root["provisionalMapGrade"].asString();
                        if (provisionalGrade.isOk()) m_view.placement.currentMapGrade = provisionalGrade.unwrap();
                        if (provisional.isOk() || provisionalGrade.isOk()) ++m_view.revision;
                    } else {
                        applyAttemptSnapshot(root);
                    }
                    m_lastSubmittedProgress = progress;
                } else {
                    log::debug(
                        "Ignoring stale Ranked progress snapshot: responseAttempt={} activeAttempt={} pendingEnd={}",
                        attemptId,
                        m_attemptId,
                        m_pendingEnd.has_value()
                    );
                }
            } else {
                log::debug("Ranked spectator telemetry dropped: {}", response.code());
            }
            if (m_attemptId == attemptId && m_pendingProgress && *m_pendingProgress == progress) {
                m_pendingProgress.reset();
            }
            flushProgressTelemetry();
        }
    );
}

void RankedRuntime::tick() {
    SpectatorLiveRuntime::get().tickTerminal();
    auto const now = std::chrono::steady_clock::now();
    if (now >= m_nextPollAt) {
        if (m_view.stage == RuntimeStage::Queued) pollQueue();
        if (m_view.stage == RuntimeStage::Matched) pollMatch();
    }
    if (
        m_attemptBusy &&
        m_attemptRequestStartedAt != std::chrono::steady_clock::time_point{} &&
        now - m_attemptRequestStartedAt >= kAttemptRequestWatchdog
    ) {
        auto const requestKind = !m_attemptId.empty() && m_pendingEnd ? "end" : "start";
        log::warn(
            "Ranked attempt {} request stalled for {}s; cancelling and retrying the same event",
            requestKind,
            std::chrono::duration_cast<std::chrono::seconds>(kAttemptRequestWatchdog).count()
        );
        invalidateAttemptRequest();
        m_nextAttemptRetryAt = now + std::chrono::milliseconds(250);
        setTransientError(fmt::format("Attempt {} sync timed out; retrying.", requestKind));
    }
    flushAttemptEvents();
    flushAttemptStartIntents();
    flushProgressTelemetry();
}

} // namespace corum::ranked
