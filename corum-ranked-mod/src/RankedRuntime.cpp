#include "RankedRuntime.hpp"

#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <string_view>

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

bool stateAllowsAttempts(std::string const& state) {
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
    m_pendingStart.reset();
    m_pendingEnd.reset();
    m_pendingProgress.reset();
    m_lastSubmittedProgress = -1;
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
                    "Installed mod environment does not match the server allowlist.",
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
        if (snapshot.id == m_environmentPolicy.cbfModId) {
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
        setStage(RuntimeStage::Blocked, "The installed mod environment changed. Re-open Ranked to recheck.");
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
        setStage(RuntimeStage::Blocked, "The installed mod environment changed. Re-open Ranked to recheck.");
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
            setStage(RuntimeStage::Matched, "Opponent found. Confirm Ready.");
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
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    m_view.match.debug = root["debug"].asBool().unwrapOr(false);
#endif
    m_view.match.candidateMaps.clear();
    m_view.match.currentMap.reset();
    m_view.match.banner.clear();
    m_view.match.roundNumber = 0;
    m_view.match.deathmatchSequence = 0;
    m_view.match.scoreA = 0;
    m_view.match.scoreB = 0;
    m_view.match.clearsA = 0;
    m_view.match.clearsB = 0;
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
        m_view.match.scoreA = static_cast<int>(round["scores"]["A"].asInt().unwrapOr(0));
        m_view.match.scoreB = static_cast<int>(round["scores"]["B"].asInt().unwrapOr(0));
        m_view.match.clearsA = static_cast<int>(round["clears"]["A"].asInt().unwrapOr(0));
        m_view.match.clearsB = static_cast<int>(round["clears"]["B"].asInt().unwrapOr(0));
        m_view.match.currentMap = parseMap(round["map"]);
    }
    auto const deathmatch = root["deathmatch"];
    if (deathmatch.isObject()) {
        m_view.match.deathmatchSequence = static_cast<int>(deathmatch["sequence"].asInt().unwrapOr(0));
        m_view.match.currentMap = parseMap(deathmatch["map"]);
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
    if (result.isObject() && !m_view.match.side.empty()) {
        auto const delta = result["mmrDelta"][m_view.match.side].asInt();
        auto const after = result["ratingAfter"][m_view.match.side].asInt();
        if (delta.isOk()) m_view.match.ownMmrDelta = static_cast<int>(delta.unwrap());
        if (after.isOk()) m_view.match.ownRatingAfter = static_cast<int>(after.unwrap());
    }

    if (!stateAllowsAttempts(m_view.match.state) && !m_attemptBusy) {
        m_attemptId.clear();
        m_pendingStart.reset();
        m_pendingEnd.reset();
        m_pendingProgress.reset();
        m_lastSubmittedProgress = -1;
    }
    if (m_view.match.spectatorActive) {
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
    } else if (m_view.match.state == "FINAL_ATTEMPT_WINDOW") {
        m_view.status = "Final start window - active attempts remain valid.";
    } else if (m_view.match.state.starts_with("DEATHMATCH")) {
        m_view.status = "Deathmatch: exactly three attempts per player.";
    } else if (m_view.match.state == "BAN_PHASE") {
        m_view.status = "Choose one private ban. The opponent's choice stays hidden.";
    } else if (m_view.match.state == "MATCHED") {
        m_view.status = "Opponent found. Confirm Ready.";
    } else if (m_view.match.state == "ROUND_PREPARE") {
        m_view.status = "Current map revealed. Open it, then confirm Ready.";
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

bool RankedRuntime::canTrackLevel(int levelId) const {
    return
        m_view.stage == RuntimeStage::Matched &&
        stateAllowsAttempts(m_view.match.state) &&
        !m_view.match.spectatorActive &&
        m_view.match.currentMap &&
        m_view.match.currentMap->levelId == levelId;
}

bool RankedRuntime::isSpectating() const {
    return m_view.match.spectatorActive;
}

std::string RankedRuntime::newEventId(std::string_view kind) {
    ++m_eventSequence;
    return fmt::format("{}-{}-{}", kind, localNowMillis(), m_eventSequence);
}

void RankedRuntime::reportAttemptStart(int levelId) {
    if (!canTrackLevel(levelId)) return;
    if (!m_pendingStart) {
        m_pendingStart = PendingStart {
            .levelId = levelId,
            .eventId = newEventId("start"),
        };
    }
    flushAttemptEvents();
}

void RankedRuntime::reportAttemptProgress(int levelId, double progressPercent) {
    if (!canTrackLevel(levelId) || m_attemptId.empty() || m_pendingEnd) return;
    auto const progress = std::clamp(static_cast<int>(std::floor(progressPercent)), 0, 100);
    if (progress == m_lastSubmittedProgress && !m_pendingProgress) return;
    m_pendingProgress = progress;
    flushProgressTelemetry();
}

void RankedRuntime::reportAttemptEnd(int levelId, double progressPercent, bool cleared) {
    if (!canTrackLevel(levelId)) return;
    if (m_pendingEnd) return;
    if (m_attemptId.empty() && !m_pendingStart && !m_attemptBusy) return;
    m_pendingEnd = PendingEnd {
        .levelId = levelId,
        .progressPercent = std::clamp(progressPercent, 0.0, 100.0),
        .cleared = cleared,
        .eventId = newEventId("end"),
    };
    m_pendingProgress.reset();
    flushAttemptEvents();
}

void RankedRuntime::flushAttemptEvents() {
    if (m_attemptBusy || std::chrono::steady_clock::now() < m_nextAttemptRetryAt) return;
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
    matjson::Value body;
    body["levelId"] = fmt::format("{}", m_pendingStart->levelId);
    body["clientEventId"] = m_pendingStart->eventId;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_attemptBusy = true;
    m_attemptRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/attempt/start")),
        [this](web::WebResponse response) {
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
                m_pendingStart.reset();
                m_pendingEnd.reset();
                return;
            }
            m_attemptId = root["attemptId"].asString().unwrapOr("");
            m_pendingStart.reset();
            m_pendingProgress.reset();
            m_lastSubmittedProgress = -1;
            m_nextAttemptRetryAt = {};
            ++m_view.revision;
            flushAttemptEvents();
        }
    );
}

void RankedRuntime::sendAttemptEnd() {
    if (!m_pendingEnd || m_attemptId.empty()) return;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", m_pendingEnd->levelId);
    body["attemptId"] = m_attemptId;
    body["clientEventId"] = m_pendingEnd->eventId;
    body["progressPercent"] = m_pendingEnd->progressPercent;
    body["cleared"] = m_pendingEnd->cleared;
    auto request = baseRequest(m_sessionToken, m_matchToken);
    request.bodyJSON(body);
    m_attemptBusy = true;
    m_attemptRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/attempt/end")),
        [this](web::WebResponse response) {
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
            m_attemptId.clear();
            m_pendingEnd.reset();
            m_pendingProgress.reset();
            m_lastSubmittedProgress = -1;
            m_nextAttemptRetryAt = {};
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
    body["levelId"] = fmt::format("{}", currentLevelId());
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
                observeServerNow(response.json().unwrapOr(matjson::Value()));
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
