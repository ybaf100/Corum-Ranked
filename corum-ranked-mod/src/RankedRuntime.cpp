#include "RankedRuntime.hpp"

#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <string_view>
#include <utility>

using namespace geode::prelude;

namespace {

constexpr std::string_view kCbfId = "syzzi.click_between_frames";
constexpr std::string_view kDefaultRankedServerURL = "https://corum-ranked.onrender.com";
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

std::string trim(std::string value) {
    auto const first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    auto const last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string joinReasons(std::vector<std::string> const& reasons) {
    std::string result;
    for (auto const& reason : reasons) {
        if (!result.empty()) result += ", ";
        result += reason;
    }
    return result;
}

bool successful(web::WebResponse const& response) {
    return response.code() >= 200 && response.code() < 300;
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
        state == "DEATHMATCH_PLAYING";
}

bool stateAllowsAttemptStart(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "DEATHMATCH_PLAYING";
}

int pollIntervalMillis(bool spectatorActive = false) {
    auto const configured = std::clamp(
        Mod::get()->getSettingValue<int>("poll-interval-ms"),
        250,
        5000
    );
    return spectatorActive ? std::min(configured, 250) : configured;
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

std::string RankedRuntime::endpoint(std::string const& path) const {
    return m_serverURL + path;
}

void RankedRuntime::begin() {
    if (
        m_view.stage == RuntimeStage::Loading ||
        m_view.stage == RuntimeStage::JoiningQueue ||
        m_view.stage == RuntimeStage::Queued ||
        m_view.stage == RuntimeStage::Matched
    ) return;

    m_serverURL = trim(Mod::get()->getSettingValue<std::string>("ranked-server-url"));
    if (m_serverURL.empty()) {
        m_serverURL = std::string(kDefaultRankedServerURL);
    }
    if (!isAcceptableServerURL(m_serverURL)) {
        setStage(
            RuntimeStage::NotConfigured,
            "Set a valid Ranked server URL in this mod's settings.",
            "HTTPS is required except for localhost development. Do not include a trailing slash."
        );
        return;
    }

    m_sessionToken.clear();
    m_matchToken.clear();
    m_attemptId.clear();
    m_attemptLevelId = 0;
    m_gameplayMap.reset();
    m_gameplayMatchId.clear();
    m_pendingStart.reset();
    m_pendingEnd.reset();
    m_attemptBacklog.clear();
    m_pendingProgress.reset();
    m_lastSubmittedProgress = -1;
    m_localDeathmatchSequence = 0;
    m_localDeathmatchVisualAttempts = 0;
    m_optimisticScoreDelta = 0.0;
    m_optimisticClearDelta = 0;
    m_songBypassAllowed = false;
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
                std::vector<std::string> reasons;
                reasons.insert(reasons.end(), decision.unauthorizedModIds.begin(), decision.unauthorizedModIds.end());
                reasons.insert(reasons.end(), decision.missingRequiredModIds.begin(), decision.missingRequiredModIds.end());
                reasons.insert(reasons.end(), decision.versionViolations.begin(), decision.versionViolations.end());
                reasons.insert(reasons.end(), decision.cbfIssues.begin(), decision.cbfIssues.end());
                setStage(
                    RuntimeStage::Blocked,
                    "Active mod environment does not match the server allowlist.",
                    joinReasons(reasons)
                );
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
            m_view.profileTier = player["displayedTier"].asString().unwrapOr("UNRANKED");
            m_view.profileScore = static_cast<int>(player["visibleRankedScore"].asInt().unwrapOr(0));
            m_view.placementGames = static_cast<int>(player["placementGames"].asInt().unwrapOr(0));
            m_view.placementGamesRequired = static_cast<int>(
                player["placementGamesRequired"].asInt().unwrapOr(0)
            );
            setStage(RuntimeStage::Ready, "Ranked session ready. Join the queue when prepared.");
        }
    );
}

void RankedRuntime::joinQueue() {
    if (m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready) return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        setStage(RuntimeStage::Blocked, "The active mod environment changed. Re-open Ranked to recheck.");
        return;
    }
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
        setStage(RuntimeStage::Blocked, "The active mod environment changed. Re-open Ranked to recheck.");
        return;
    }
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
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/queue/leave")),
        [this](web::WebResponse response) {
            m_controlBusy = false;
            if (!successful(response)) {
                setTransientError(responseError(response));
                return;
            }
            setStage(RuntimeStage::Ready, "Left the queue.");
        }
    );
}

void RankedRuntime::pollQueue() {
    if (m_pollBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Queued) return;
    auto request = baseRequest(m_sessionToken);
    m_pollBusy = true;
    m_pollRequest.spawn(
        request.get(endpoint("/api/ranked/queue/status")),
        [this](web::WebResponse response) {
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
                    ? fmt::format("Searching for an opponent (MMR range +/-{})...", range)
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
        m_pollBusy || m_sessionToken.empty() || m_matchToken.empty() ||
        m_view.stage != RuntimeStage::Matched || m_view.match.matchId.empty()
    ) return;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    m_pollBusy = true;
    m_pollRequest.spawn(
        request.get(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/state")),
        [this](web::WebResponse response) {
            m_pollBusy = false;
            if (!successful(response)) {
                m_nextPollAt = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(pollIntervalMillis());
                setTransientError(responseError(response));
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            parseMatchState(root);
            m_nextPollAt = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(pollIntervalMillis(m_view.match.spectatorActive));
        }
    );
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
    m_view.match.playerATier = root["players"]["A"]["displayedTier"].asString().unwrapOr("UNRANKED");
    m_view.match.playerBTier = root["players"]["B"]["displayedTier"].asString().unwrapOr("UNRANKED");
    m_view.match.playerAScore = static_cast<int>(root["players"]["A"]["visibleRankedScore"].asInt().unwrapOr(0));
    m_view.match.playerBScore = static_cast<int>(root["players"]["B"]["visibleRankedScore"].asInt().unwrapOr(0));
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
    m_view.match.spectatorCurrentProgress.reset();
    m_view.match.spectatorOpponentName.clear();

    auto const opponentSide = m_view.match.side == "A" ? "B" : "A";
    m_view.match.opponentName = root["players"][opponentSide]["gdUsername"].asString().unwrapOr("Opponent");

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

    auto const result = root["result"];
    m_view.match.winnerSide = result["winnerSide"].asString().unwrapOr("");
    m_view.match.ownMmrDelta.reset();
    m_view.match.ownRatingAfter.reset();
    m_view.match.mmrDeltaA.reset();
    m_view.match.mmrDeltaB.reset();
    m_view.match.ratingAfterA.reset();
    m_view.match.ratingAfterB.reset();
    m_view.match.profileBeforeTierA.clear();
    m_view.match.profileBeforeTierB.clear();
    m_view.match.profileAfterTierA.clear();
    m_view.match.profileAfterTierB.clear();
    m_view.match.profileAfterScoreA.reset();
    m_view.match.profileAfterScoreB.reset();
    if (result.isObject() && !m_view.match.side.empty()) {
        auto const deltaA = result["mmrDelta"]["A"].asInt();
        auto const deltaB = result["mmrDelta"]["B"].asInt();
        auto const afterA = result["ratingAfter"]["A"].asInt();
        auto const afterB = result["ratingAfter"]["B"].asInt();
        if (deltaA.isOk()) m_view.match.mmrDeltaA = static_cast<int>(deltaA.unwrap());
        if (deltaB.isOk()) m_view.match.mmrDeltaB = static_cast<int>(deltaB.unwrap());
        if (afterA.isOk()) m_view.match.ratingAfterA = static_cast<int>(afterA.unwrap());
        if (afterB.isOk()) m_view.match.ratingAfterB = static_cast<int>(afterB.unwrap());
        m_view.match.ownMmrDelta = m_view.match.side == "A" ? m_view.match.mmrDeltaA : m_view.match.mmrDeltaB;
        m_view.match.ownRatingAfter = m_view.match.side == "A" ? m_view.match.ratingAfterA : m_view.match.ratingAfterB;
        auto const profileBefore = result["profileBefore"];
        if (profileBefore.isObject()) {
            m_view.match.profileBeforeTierA = profileBefore["A"]["displayedTier"].asString().unwrapOr("");
            m_view.match.profileBeforeTierB = profileBefore["B"]["displayedTier"].asString().unwrapOr("");
        }
        auto const profileAfter = result["profileAfter"];
        if (profileAfter.isObject()) {
            m_view.match.profileAfterTierA = profileAfter["A"]["displayedTier"].asString().unwrapOr("");
            m_view.match.profileAfterTierB = profileAfter["B"]["displayedTier"].asString().unwrapOr("");
            auto const scoreA = profileAfter["A"]["visibleRankedScore"].asInt();
            auto const scoreB = profileAfter["B"]["visibleRankedScore"].asInt();
            if (scoreA.isOk()) m_view.match.profileAfterScoreA = static_cast<int>(scoreA.unwrap());
            if (scoreB.isOk()) m_view.match.profileAfterScoreB = static_cast<int>(scoreB.unwrap());
        }
    }

    if (!stateAllowsActiveAttempt(m_view.match.state) && !m_attemptBusy) {
        m_attemptId.clear();
        m_attemptLevelId = 0;
        m_pendingStart.reset();
        m_pendingEnd.reset();
        m_attemptBacklog.clear();
        m_pendingProgress.reset();
        m_lastSubmittedProgress = -1;
        m_optimisticScoreDelta = 0.0;
        m_optimisticClearDelta = 0;
        if (
            m_view.match.state == "MATCH_RESULT" || m_view.match.state == "CANCELLED" ||
            m_view.match.state == "ROUND_RESULT" || m_view.match.state == "DEATHMATCH_RESULT"
        ) {
            m_gameplayMap.reset();
            m_gameplayMatchId.clear();
        }
    }
    if (m_view.match.spectatorActive) {
        // A trigger-side player must not create a new visual attempt after the
        // server enters LAST_ATTEMPT. Any speculative future starts are invalid.
        // Preserve the currently transmitting end acknowledgement, but discard
        // queued later visuals and their optimistic presentation contribution.
        for (auto const& queued : m_attemptBacklog) {
            if (!queued.end) continue;
            m_optimisticScoreDelta = std::max(0.0, m_optimisticScoreDelta - queued.end->optimisticScore);
            m_optimisticClearDelta = std::max(0, m_optimisticClearDelta - queued.end->optimisticClear);
        }
        m_attemptBacklog.clear();
        m_pendingStart.reset();
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
            // two Clears cannot be auto-entered into an illegal extra attempt.
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
            } else if (!inLastAttemptFlow) {
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
    if (state != "MATCHED" && state != "ROUND_PREPARE" && state != "DEATHMATCH_PREPARE") return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        setTransientError("Environment check failed. Ranked Ready was not sent.");
        return;
    }
    matjson::Value body;
    body["installedMods"] = installedModsJson();
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_controlBusy = true;
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/ready")),
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
    if (m_view.match.state != "MATCH_RESULT" && m_view.match.state != "CANCELLED") return;
    if (m_view.match.state == "MATCH_RESULT") {
        auto const ownSide = m_view.match.side;
        auto const& tier = ownSide == "A" ? m_view.match.profileAfterTierA : m_view.match.profileAfterTierB;
        auto const score = ownSide == "A" ? m_view.match.profileAfterScoreA : m_view.match.profileAfterScoreB;
        if (!tier.empty()) m_view.profileTier = tier;
        if (score) m_view.profileScore = *score;
        ++m_view.placementGames;
    }
    m_matchToken.clear();
    m_attemptId.clear();
    m_attemptLevelId = 0;
    m_gameplayMap.reset();
    m_gameplayMatchId.clear();
    m_pendingStart.reset();
    m_pendingEnd.reset();
    m_attemptBacklog.clear();
    m_pendingProgress.reset();
    m_lastSubmittedProgress = -1;
    m_localDeathmatchSequence = 0;
    m_localDeathmatchVisualAttempts = 0;
    m_optimisticScoreDelta = 0.0;
    m_optimisticClearDelta = 0;
    m_songBypassAllowed = false;
    m_view.match = {};
    setStage(RuntimeStage::Ready, "Ranked session ready.");
}

void RankedRuntime::queueAgain() {
    if (m_view.stage != RuntimeStage::Matched) return;
    if (m_view.match.state != "MATCH_RESULT" && m_view.match.state != "CANCELLED") return;
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
                    auto const delta = item["mmrDelta"][history.side].asInt();
                    auto const rating = item["ratingAfter"][history.side].asInt();
                    if (delta.isOk()) history.ownMmrDelta = static_cast<int>(delta.unwrap());
                    if (rating.isOk()) history.ownRatingAfter = static_cast<int>(rating.unwrap());
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

int RankedRuntime::currentLevelId() const {
    return m_view.match.currentMap ? m_view.match.currentMap->levelId : 0;
}

bool RankedRuntime::armCurrentLevelForGameplay() {
    if (
        m_view.stage != RuntimeStage::Matched || !m_view.match.currentMap ||
        m_view.match.spectatorActive || !stateAllowsActiveAttempt(m_view.match.state)
    ) return false;
    m_gameplayMap = *m_view.match.currentMap;
    m_gameplayMatchId = m_view.match.matchId;
    log::debug(
        "Ranked gameplay armed: match={} level={} qualifying={}",
        m_gameplayMatchId,
        m_gameplayMap->levelId,
        m_gameplayMap->qualifyingPercent
    );
    return true;
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

bool RankedRuntime::canEnterCurrentLevel() const {
    if (m_view.stage != RuntimeStage::Matched || !m_view.match.currentMap || m_view.match.spectatorActive) return false;
    if (!stateAllowsActiveAttempt(m_view.match.state)) return false;

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
    // If that pending Clear would make this side reach two Clears, do not let
    // LevelInfoLayer auto-enter another PlayLayer while waiting for the server
    // to transition into LAST_ATTEMPT_WINDOW / ROUND_SETTLING. This closes the
    // clear->LevelInfo->re-enter race that could create an illegal third Clear.
    auto const committedClears = m_view.match.side == "A" ? m_view.match.clearsA : m_view.match.clearsB;
    if (
        stateAllowsAttemptStart(m_view.match.state) &&
        committedClears + m_optimisticClearDelta >= 2 &&
        hasLocalAttemptInFlight()
    ) return false;

    return true;
}

bool RankedRuntime::reserveDeathmatchVisualAttempt() {
    if (m_view.match.state != "DEATHMATCH_PLAYING") return true;
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
    if (m_localDeathmatchVisualAttempts >= 3) return false;
    ++m_localDeathmatchVisualAttempts;
    ++m_view.revision;
    return true;
}

int RankedRuntime::localDeathmatchVisualAttemptsUsed() const {
    if (m_localDeathmatchSequence != m_view.match.deathmatchSequence) return 0;
    return std::clamp(m_localDeathmatchVisualAttempts, 0, 3);
}

double RankedRuntime::localDisplayScore(double progressPercent) const {
    auto const ownIsA = m_view.match.side == "A";
    auto const committed = ownIsA ? m_view.match.committedScoreA : m_view.match.committedScoreB;
    auto const serverDisplay = ownIsA ? m_view.match.scoreA : m_view.match.scoreB;
    auto const optimisticBase = committed + m_optimisticScoreDelta;

    // A negative progress means the current visual attempt already ended. Its
    // value is therefore in m_optimisticScoreDelta and must not be counted again.
    if (progressPercent < 0.0 || m_view.match.spectatorActive) {
        return std::max(serverDisplay, optimisticBase);
    }

    RankedMapView const* scoringMap = nullptr;
    if (m_gameplayMap && m_gameplayMatchId == m_view.match.matchId) {
        scoringMap = &*m_gameplayMap;
    } else if (m_view.match.currentMap) {
        scoringMap = &*m_view.match.currentMap;
    }
    if (!scoringMap) return std::max(serverDisplay, optimisticBase);

    auto const progress = std::clamp(progressPercent, 0.0, 100.0);
    auto const qualifying = scoringMap->qualifyingPercent;
    auto const live = progress >= qualifying ? std::floor(progress) : 0.0;
    return std::max(serverDisplay, optimisticBase + live);
}

int RankedRuntime::localDisplayClears() const {
    auto const committed = m_view.match.side == "A" ? m_view.match.clearsA : m_view.match.clearsB;
    return std::max(0, committed + m_optimisticClearDelta);
}

bool RankedRuntime::canTrackLevel(int levelId) const {
    return
        m_view.stage == RuntimeStage::Matched &&
        stateAllowsActiveAttempt(m_view.match.state) &&
        !m_view.match.spectatorActive &&
        isGameplayLevel(levelId);
}

bool RankedRuntime::isSpectating() const {
    return m_view.match.spectatorActive;
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
    if (!canTrackLevel(levelId) || !stateAllowsAttemptStart(m_view.match.state)) return false;

    if (m_view.match.state == "DEATHMATCH_PLAYING") {
        auto const serverUsed = m_view.match.side == "A"
            ? m_view.match.deathmatchAttemptsUsedA
            : m_view.match.deathmatchAttemptsUsedB;
        if (serverUsed >= 3) return false;
    }

    // A PlayLayer can retry this call while waiting for the transport. Treat an
    // already registered local start as success instead of creating duplicates.
    if (
        !m_attemptId.empty() && m_attemptLevelId == levelId &&
        !m_pendingEnd && m_attemptBacklog.empty()
    ) return true;
    if (
        m_pendingStart && m_pendingStart->levelId == levelId &&
        !m_pendingEnd && m_attemptBacklog.empty()
    ) return true;

    PendingStart start {
        .levelId = levelId,
        .eventId = newEventId("start"),
    };

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

void RankedRuntime::reportAttemptProgress(int levelId, double progressPercent) {
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

bool RankedRuntime::reportAttemptEnd(int levelId, double progressPercent, bool cleared) {
    if (!canTrackLevel(levelId)) return false;

    // Self-heal a missed init-time start. PlayLayer can be created on the same
    // frame as a state refresh; if the first reportAttemptStart() was skipped,
    // never silently discard the eventual death/Clear. Queue a start first while
    // starts are still legal and attach this end to it.
    if (
        m_attemptId.empty() && !m_pendingStart && !m_attemptBusy &&
        m_attemptBacklog.empty()
    ) {
        if (!stateAllowsAttemptStart(m_view.match.state) || !reportAttemptStart(levelId)) {
            log::warn("Ranked attempt end could not recover a missing start: level={}", levelId);
            return false;
        }
    }

    auto const finalProgress = std::clamp(progressPercent, 0.0, 100.0);
    RankedMapView const* scoringMap = nullptr;
    if (m_gameplayMap && m_gameplayMatchId == m_view.match.matchId && m_gameplayMap->levelId == levelId) {
        scoringMap = &*m_gameplayMap;
    } else if (m_view.match.currentMap && m_view.match.currentMap->levelId == levelId) {
        scoringMap = &*m_view.match.currentMap;
    }
    auto optimisticScore = 0.0;
    if (scoringMap) {
        auto const qualifying = scoringMap->qualifyingPercent;
        optimisticScore = cleared
            ? 100.0 + qualifying
            : (finalProgress >= qualifying ? std::floor(finalProgress) : 0.0);
    }
    PendingEnd end {
        .levelId = levelId,
        .progressPercent = finalProgress,
        .cleared = cleared,
        .eventId = newEventId("end"),
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

    m_optimisticScoreDelta += end.optimisticScore;
    m_optimisticClearDelta += end.optimisticClear;
    ++m_view.revision;
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
    if (m_attemptId.empty() && m_pendingStart && canTrackLevel(m_pendingStart->levelId)) {
        sendAttemptStart();
    }
}

void RankedRuntime::sendAttemptStart() {
    if (!m_pendingStart) return;
    auto const starting = *m_pendingStart;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", starting.levelId);
    body["clientEventId"] = starting.eventId;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_attemptBusy = true;
    m_attemptRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/attempt/start")),
        [this, starting](web::WebResponse response) {
            m_attemptBusy = false;
            if (!successful(response)) {
                setTransientError("Attempt start retrying: " + responseError(response));
                m_nextAttemptRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            if (!root["accepted"].asBool().unwrapOr(false)) {
                setTransientError(root["reason"].asString().unwrapOr("Attempt start rejected"));
                applyAttemptSnapshot(root);
                if (m_pendingEnd) {
                    m_optimisticScoreDelta = std::max(0.0, m_optimisticScoreDelta - m_pendingEnd->optimisticScore);
                    m_optimisticClearDelta = std::max(0, m_optimisticClearDelta - m_pendingEnd->optimisticClear);
                }
                m_pendingStart.reset();
                m_pendingEnd.reset();
                m_attemptLevelId = 0;
                m_pendingProgress.reset();
                m_lastSubmittedProgress = -1;
                m_nextAttemptRetryAt = {};
                promoteQueuedAttempt();
                ++m_view.revision;
                flushAttemptEvents();
                return;
            }
            m_attemptId = root["attemptId"].asString().unwrapOr("");
            m_attemptLevelId = starting.levelId;
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
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_attemptBusy = true;
    m_attemptRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/attempt/end")),
        [this, ending](web::WebResponse response) {
            m_attemptBusy = false;
            if (!successful(response)) {
                setTransientError("Attempt end retrying: " + responseError(response));
                m_nextAttemptRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            observeServerNow(root);
            auto const accepted = root["accepted"].asBool().unwrapOr(false);
            auto const reason = root["reason"].asString().unwrapOr("");
            if (!accepted && reason != "ATTEMPT_ALREADY_ENDED") {
                setTransientError(reason.empty() ? "Attempt end rejected" : reason);
            }
            applyAttemptSnapshot(root);
            m_attemptId.clear();
            m_attemptLevelId = 0;
            m_pendingEnd.reset();
            m_pendingProgress.reset();
            m_lastSubmittedProgress = -1;
            m_optimisticScoreDelta = std::max(0.0, m_optimisticScoreDelta - ending.optimisticScore);
            m_optimisticClearDelta = std::max(0, m_optimisticClearDelta - ending.optimisticClear);
            m_nextAttemptRetryAt = {};
            m_nextPollAt = std::chrono::steady_clock::now();
            promoteQueuedAttempt();
            ++m_view.revision;
            flushAttemptEvents();
        }
    );
}

void RankedRuntime::flushProgressTelemetry() {
    if (
        m_progressBusy || !m_pendingProgress || m_attemptId.empty() || m_pendingEnd ||
        m_view.match.spectatorActive || std::chrono::steady_clock::now() < m_nextProgressAt
    ) return;
    sendAttemptProgress();
}

void RankedRuntime::sendAttemptProgress() {
    if (!m_pendingProgress || m_attemptId.empty()) return;
    auto const progress = *m_pendingProgress;
    auto const attemptId = m_attemptId;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", m_attemptLevelId);
    body["attemptId"] = attemptId;
    body["progressPercent"] = progress;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_progressBusy = true;
    m_nextProgressAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    m_progressRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/attempt/progress")),
        [this, attemptId, progress](web::WebResponse response) {
            m_progressBusy = false;
            if (successful(response)) {
                auto const root = response.json().unwrapOr(matjson::Value());
                observeServerNow(root);
                applyAttemptSnapshot(root);
                if (m_attemptId == attemptId) m_lastSubmittedProgress = progress;
            } else {
                log::debug("Ranked spectator telemetry dropped: {}", response.code());
            }
            if (m_pendingProgress && *m_pendingProgress == progress) m_pendingProgress.reset();
            flushProgressTelemetry();
        }
    );
}

void RankedRuntime::tick() {
    auto const now = std::chrono::steady_clock::now();
    if (now >= m_nextPollAt) {
        if (m_view.stage == RuntimeStage::Queued) pollQueue();
        if (m_view.stage == RuntimeStage::Matched) pollMatch();
    }
    flushAttemptEvents();
    flushProgressTelemetry();
}

} // namespace corum::ranked
