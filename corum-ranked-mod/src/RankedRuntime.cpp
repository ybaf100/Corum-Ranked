#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "ui/RankedModUpdateManager.hpp"
#include "ui/RankAssetPackManager.hpp"
#include "SpectatorLiveRuntime.hpp"
#include "domain/SpectatorLocalSafety.hpp"
#include "domain/AttemptScoring.hpp"
#include "domain/RankedGameplayPolicy.hpp"
#include "domain/RoundResourcePolicy.hpp"
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
#include <random>
#include <sstream>
#include <string_view>
#include <utility>

using namespace geode::prelude;

namespace {
double gameplayControlNowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


constexpr std::string_view kCbfId = "syzzi.click_between_frames";
constexpr std::string_view kDefaultRankedServerURL = "https://corum-ranked-production.up.railway.app";
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

corum::ranked::MapMetadata parseMapMetadata(matjson::Value const& value) {
    corum::ranked::MapMetadata info;
    if (!value.isObject()) return info;
    auto const kind = value["kind"].asString().unwrapOr("UNKNOWN");
    if (kind == "NORMAL" || kind == "DEMON") info.kind = kind;
    auto const bounded = [&](char const* name, int min, int max) -> std::optional<int> {
        auto const raw = value[name].asInt();
        if (!raw.isOk() || raw.unwrap() < min || raw.unwrap() > max) return std::nullopt;
        return static_cast<int>(raw.unwrap());
    };
    info.stars = bounded("stars",0,10); info.gddlTier = bounded("gddlTier",1,40);
    info.officialLevelId = bounded("officialLevelId",1,22);
    auto const color = value["gddlColor"].asString().unwrapOr("");
    if (color.size() == 7 && color.front() == '#' && std::all_of(color.begin()+1,color.end(),[](unsigned char c) {
        return std::isxdigit(c) != 0;
    })) info.gddlColor = color;
    auto const seconds = value["durationSeconds"].asDouble().unwrapOr(0);
    if (std::isfinite(seconds) && seconds > 0 && seconds <= 3600*24*30) info.durationSeconds = seconds;
    return info;
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
        .metadata = parseMapMetadata(value["metadata"]),
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

corum::ranked::ForumRecordsView parseForumRecords(matjson::Value const& root) {
    corum::ranked::ForumRecordsView result;
    auto const records = root["records"].asArray();
    if (records.isErr()) { result.error = "Invalid forum records response"; return result; }
    result.loaded = true;
    result.refreshAvailableAt = root["refreshAvailableAt"].asString().unwrapOr("");
    for (auto const& item : records.unwrap()) {
        corum::ranked::ForumRankView record;
        record.provider = item["provider"].asString().unwrapOr("");
        record.externalName = item["externalName"].asString().unwrapOr("");
        record.status = item["status"].asString().unwrapOr("PENDING");
        record.lastSuccessAt = item["lastSuccessAt"].asString().unwrapOr("");
        auto const rank = item["rank"].asInt();
        if (rank.isOk() && rank.unwrap() > 0) record.rank = static_cast<int>(rank.unwrap());
        result.records.push_back(std::move(record));
    }
    return result;
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

corum::ranked::StandingView parseStanding(matjson::Value const& value) {
    corum::ranked::StandingView view;
    if (!value.isObject()) return view;
    view.displayTier=value["displayTier"].asString().unwrapOr("");
    auto const gold=value["goldTier"];
    if(gold.isObject()) {
        view.goldPosition=static_cast<int>(gold["position"].asInt().unwrapOr(0));
        view.goldTotal=static_cast<int>(gold["total"].asInt().unwrapOr(0));
        view.nextTier=gold["nextTier"].asString().unwrapOr("");
        if(auto cut=gold["nextCutoffLp"].asInt();cut.isOk())view.nextTierCutoff=static_cast<int>(cut.unwrap());
        if(auto cut=gold["championCutoffLp"].asInt();cut.isOk())view.championCutoff=static_cast<int>(cut.unwrap());
        if(auto cut=gold["masterCutoffLp"].asInt();cut.isOk())view.masterCutoff=static_cast<int>(cut.unwrap());
    }
    auto const rank = value["rank"].asInt();
    view.total = static_cast<int>(value["total"].asInt().unwrapOr(0));
    if (rank.isOk() && rank.unwrap() > 0 && rank.unwrap() <= view.total) {
        view.rank = static_cast<int>(rank.unwrap());
        // Derive only the display percentage from the authoritative rank/count.
        view.topPercent = static_cast<double>(*view.rank) / view.total * 100.0;
    }
    auto const target = value["target"];
    if (view.rank && *view.rank > 1 && target.isObject()) {
        corum::ranked::StandingTargetView t;
        t.accountId = numFromString<int>(target["gdAccountId"].asString().unwrapOr("")).unwrapOr(0);
        t.player = target["player"].asString().unwrapOr("");
        t.bot = target["isBot"].asBool().unwrapOr(false);
        t.rank = static_cast<int>(target["rank"].asInt().unwrapOr(0));
        t.score = static_cast<int>(target["visibleRankedScore"].asInt().unwrapOr(0));
        if (t.rank > 0 && t.rank < *view.rank) view.target = std::move(t);
    }
    return view;
}

corum::ranked::ScoreSeriesView parseScoreSeries(matjson::Value const& value) {
    corum::ranked::ScoreSeriesView series;
    if (!value.isObject()) return series;
    series.sourceTier = value["sourceTier"].asString().unwrapOr("");
    series.targetTier = value["targetTier"].asString().unwrapOr("");
    series.cutoff = static_cast<int>(value["cutoff"].asInt().unwrapOr(0));
    series.gamesRequired = static_cast<int>(value["gamesRequired"].asInt().unwrapOr(0));
    series.winsRequired = static_cast<int>(value["winsRequired"].asInt().unwrapOr(0));
    series.results = parseRatingMarks(value["results"]);
    series.active = series.gamesRequired > 0 && series.winsRequired > 0 &&
        series.winsRequired <= series.gamesRequired && series.results.size() <= static_cast<std::size_t>(series.gamesRequired);
    return series;
}

corum::ranked::GoldScoreTimeline parseGoldTimeline(matjson::Value const& value) {
    corum::ranked::GoldScoreTimeline view;
    if (!value.isObject()) return view;
    auto const integer = [](matjson::Value const& v) -> std::optional<int> {
        auto n = v.asInt();
        if (!n.isOk() || n.unwrap() < 0 || n.unwrap() > 2147483647) return std::nullopt;
        return static_cast<int>(n.unwrap());
    };
    auto const target = [&](matjson::Value const& v) -> std::optional<corum::ranked::StandingTargetView> {
        if (!v.isObject()) return std::nullopt;
        auto rank = integer(v["rank"]), score = integer(v["visibleRankedScore"]);
        auto account = numFromString<int>(v["gdAccountId"].asString().unwrapOr("")).unwrapOr(0);
        auto bot = v["isBot"].asBool().unwrapOr(false);
        if (!rank || *rank == 0 || !score || (!bot && account <= 0)) return std::nullopt;
        return corum::ranked::StandingTargetView {account,v["player"].asString().unwrapOr(""),bot,*rank,*score};
    };
    auto total = integer(value["fixedTotal"]), higher = integer(value["higherFixed"]);
    auto groups = value["groups"].asArray();
    if (!total || !higher || *higher > *total || *total > 2147483645 || !groups.isOk() || groups.unwrap().size() > 4096) return {};
    view.fixedTotal = *total; view.higherFixed = *higher;
    auto const priority = [&](matjson::Value const& data,char const* key) -> std::optional<int> {
        if (!data.contains(key)) return 1; // Legacy LP-only result snapshots.
        auto parsed = integer(data[key]);
        return parsed && *parsed <= 32 ? parsed : std::nullopt;
    };
    auto beforePriority = priority(value,"beforePriority"), afterPriority = priority(value,"afterPriority");
    if (!beforePriority || !afterPriority) return {};
    view.beforePriority = *beforePriority; view.afterPriority = *afterPriority;
    std::int64_t represented = *higher;
    std::optional<int> previous;
    for (auto const& group : groups.unwrap()) {
        auto score = integer(group["score"]), count = integer(group["count"]);
        auto avatar = target(group["target"]);
        if (!score || !count || *count == 0 || !avatar || avatar->score != *score || (previous && *score >= *previous)) return {};
        represented += *count; if (represented > *total) return {};
        corum::ranked::GoldScoreGroup parsed {*score,*count,*avatar,group["preferMoving"].asBool().unwrapOr(false)};
        if (auto states = group["states"].asArray(); group.contains("states")) {
            if (!states.isOk()) return {};
            if (states.unwrap().empty() || states.unwrap().size() > 32) return {};
            std::int64_t counted = 0; std::optional<int> previousPriority;
            for (auto const& state : states.unwrap()) {
                auto priority = integer(state["priority"]), amount = integer(state["count"]);
                auto representative = target(state["target"]);
                if (!priority || *priority > 32 || !amount || *amount == 0 || !representative ||
                    representative->score != *score || (previousPriority && *priority >= *previousPriority)) return {};
                counted += *amount; if (counted > *count) return {};
                parsed.states.push_back({*priority,*amount,*representative,state["preferMoving"].asBool().unwrapOr(false)});
                previousPriority = *priority;
            }
            if (counted != *count) return {};
        }
        view.groups.push_back(std::move(parsed)); previous = *score;
    }
    auto const moving = value["moving"];
    if (moving.isObject()) {
        auto avatar = target(moving["target"]);
        auto before = integer(moving["beforeScore"]), after = integer(moving["afterScore"]);
        if (!avatar || (!before && !after)) return {};
        view.moving = corum::ranked::GoldMovingRival {before,after,*avatar};
        auto beforeState = priority(moving,"beforePriority"), afterState = priority(moving,"afterPriority");
        if (!beforeState || !afterState) return {};
        view.moving->beforePriority = *beforeState; view.moving->afterPriority = *afterState;
    }
    auto const overtakes=value["overtakes"].asArray();
    if (overtakes.isOk()) {
        if (overtakes.unwrap().size()>5) return {};
        for (auto const& item:overtakes.unwrap()) {
            auto avatar=target(item); if (!avatar) return {};
            if (std::any_of(view.overtakes.begin(),view.overtakes.end(),[&](auto const& previous) {
                return previous.accountId==avatar->accountId && previous.bot==avatar->bot;
            })) return {};
            view.overtakes.push_back(*avatar);
        }
    }
    view.available = true; return view;
}

corum::ranked::ResultScoreView parseResultScore(matjson::Value const& value) {
    corum::ranked::ResultScoreView view;
    if (value["version"].asInt().unwrapOr(0) != 1) return view;
    if (auto bands = value["tierBands"].asArray(); bands.isOk()) {
        for (auto const& band : bands.unwrap()) {
            auto const cutoff = band["minInclusive"].asInt();
            if (!cutoff.isOk()) return {};
            view.bands.push_back({band["tier"].asString().unwrapOr(""), static_cast<int>(cutoff.unwrap())});
        }
    }
    if (!corum::ranked::validScoreBands(view.bands)) return {};
    auto const player = [&](char const* side) {
        auto const data = value["players"][side];
        return corum::ranked::ResultScorePlayerView {parseStanding(data["before"]),
            parseStanding(data["after"]), parseScoreSeries(data["promotion"]),parseGoldTimeline(data["goldTimeline"])};
    };
    view.playerA = player("A"); view.playerB = player("B"); view.available = true;
    return view;
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
    profile.wins = std::max(0,static_cast<int>(value["wins"].asInt().unwrapOr(0)));
    profile.losses = std::max(0,static_cast<int>(value["losses"].asInt().unwrapOr(0)));
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
    // An admission blocked before session creation must reread the server's
    // current switch, rather than keep evaluating a permanently cached TRUE.
    if(m_view.stage==RuntimeStage::Blocked && m_sessionToken.empty()) {fetchConfig();return;}
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) {
        showEnvironmentWarning(decision, m_view.stage == RuntimeStage::Blocked);
        return;
    }

    auto const wasBlocked = m_view.stage == RuntimeStage::Blocked;
    m_view.environmentWarning = {};
    m_view.error.clear();
    ++m_view.revision;
    if (wasBlocked) setStage(m_view.match.matchId.empty()?RuntimeStage::Ready:RuntimeStage::Matched,
        "Ranked mod environment is ready.");
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

std::string RankedRuntime::leaderboardUrl(int offset, int limit, RankedMode mode) const {
    auto const path = mode == RankedMode::Survival ? "/api/ranked/survival/leaderboard" : "/api/ranked/leaderboard";
    return endpoint(fmt::format("{}?offset={}&limit={}",path,std::max(0,offset),std::clamp(limit,1,50)));
}

std::string RankedRuntime::tierGuideUrl() const { return endpoint("/api/ranked/leaderboard/tiers"); }

void RankedRuntime::begin() {
    m_rankedOpen = true;
    m_sessionRejected = false;
    if (
        m_view.stage == RuntimeStage::Loading ||
        m_view.stage == RuntimeStage::JoiningQueue ||
        m_view.stage == RuntimeStage::Queued ||
        m_view.stage == RuntimeStage::Matched
    ) return;

    ++m_sessionGeneration;
    m_survivalProfileRequest.cancel(); m_modeConfigRequest.cancel();
    m_survivalProfileBusy = m_modeConfigBusy = false;
    m_nextSurvivalProfileAt = {}; m_nextModeConfigAt = {};
    // A successful Admin action reloads admission while a lobby metadata read
    // may still be pending. Retire it before fetchConfig reuses this channel.
    m_controlRequest.cancel(); m_controlBusy = false;
    ++m_historyGeneration; m_historyRequest.cancel(); m_view.historyLoading = false;
    ++m_standingGeneration; m_standingRequest.cancel(); m_standingBusy = false;
    m_nextStandingAt = {}; m_view.standing = {}; m_view.survival = {};
    m_sessionHeartbeatRequest.cancel(); m_sessionHeartbeatBusy = false;
    m_sessionLeaseDeadline = {}; m_nextSessionHeartbeatAt = {};
    resetGameplayControls();
    invalidatePollRequest();
    invalidateAttemptRequest();
    ++m_progressRequestGeneration;
    m_progressRequest.cancel();
    m_progressBusy = false;

    m_sessionToken.clear();
    ++m_playerInfoGeneration; ++m_forumRecordsGeneration;
    m_playerInfoRequest.cancel(); m_forumRecordsRequest.cancel();
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
    m_view.accountActiveElsewhere = false;
    m_recoveringMatch = false;
    m_view.placement = {};
    m_view.match = {};
    m_view.playerInfo = {};
    m_rejectedRequiredVersion.clear();
    m_view.forumRecords = {};
    m_view.historyLoading = false; m_view.historyError.clear(); m_view.history.clear();
    fetchConfig();
}

void RankedRuntime::fetchConfig() {
    if (m_controlBusy) return;
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Loading authoritative Ranked configuration...");
    auto request = baseRequest({});
    auto const sessionGeneration = m_sessionGeneration;
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/config")),
        [this, sessionGeneration](web::WebResponse response) {
            if (sessionGeneration != m_sessionGeneration || !m_rankedOpen) return;
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
                auto const assetsNode = clientNode["assets"];
                if (assetsNode.isObject()) {
                    auto& assets = clientPresentation.assets;
                    assets.enabled = assetsNode["enabled"].asBool().unwrapOr(false);
                    assets.assetPackVersion = assetsNode["assetPackVersion"].asString().unwrapOr("");
                    assets.sha256 = assetsNode["sha256"].asString().unwrapOr("");
                    auto const size = assetsNode["sizeBytes"].asInt().unwrapOr(0);
                    assets.sizeBytes = size > 0 ? static_cast<std::uintmax_t>(size) : 0;
                }
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
                            auto const percent = item["volumePercent"].asInt().unwrapOr(100);
                            resource.volume = percent >= 0 && percent <= 100 ? percent / 100.0 : 1.0;
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

            m_view.soloEnabled = root["queueEnabled"].asBool().unwrapOr(false);
            m_view.survivalEnabled = root["modes"]["survival"]["enabled"].asBool().unwrapOr(false);
            m_view.survivalConfigReady = root["modes"]["survival"]["ready"].asBool().unwrapOr(false);

            EnvironmentPolicy policy;
            if (root.contains("modRestrictionEnabled") && !root["modRestrictionEnabled"].isBool()) {
                setStage(RuntimeStage::Error, "Ranked configuration is invalid.", "modRestrictionEnabled must be boolean");
                return;
            }
            policy.modRestrictionEnabled = root["modRestrictionEnabled"].asBool().unwrapOr(true);
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
            auto const channel = rankedBuildChannel();
            auto const update = root["modUpdates"][channel];
            auto& updates = ui::RankedModUpdateManager::get();
            auto const bytes = update["sizeBytes"].asInt().unwrapOr(0);
            updates.configure({
                .version = update["version"].asString().unwrapOr(root["requiredClientVersion"].asString().unwrapOr(Mod::get()->getVersion().toVString())),
                .sha256 = update["sha256"].asString().unwrapOr(""),
                .channel = channel,
                .sizeBytes = bytes > 0 ? static_cast<std::uintmax_t>(bytes) : 0,
                .url = update["downloadUrl"].asString().unwrapOr(""),
            }, update.isObject());
            if (updates.ready() && !m_rejectedRequiredVersion.empty() &&
                m_rejectedRequiredVersion != Mod::get()->getVersion().toVString()) {
                // A rolling deployment can return config from the old instance
                // after the new instance rejected admission. Keep the gate
                // closed until its pinned download metadata is available.
                updates.configure({.version = m_rejectedRequiredVersion, .sha256 = "", .channel = channel, .sizeBytes = 0}, false);
            }
            if (!updates.ready()) {
                setStage(RuntimeStage::AwaitingUpdate, "A mod update is required before entering Ranked.");
                return;
            }
            auto& audio = RankedAudioManager::get();
            auto& assets = ui::RankAssetPackManager::get();
            audio.configure(m_view.client);
            assets.configure(m_view.client.assets);
            if (audio.requiresResourceDownload() || assets.requiresDownload()) {
                setStage(RuntimeStage::AwaitingResources, "Download the additional resources before entering Ranked.");
                return;
            }
            m_installedMods = captureInstalledMods();
            auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
            if (!decision.allowed) { showEnvironmentWarning(decision, true); return; }
            createSession();
        }
    );
}

bool RankedRuntime::handleRequiredUpdate(web::WebResponse const& response) {
    if (response.code() != 426) return false;
    m_rejectedRequiredVersion = response.json().unwrapOr(matjson::Value())["requiredVersion"].asString().unwrapOr("");
    // The server rejects before creating a queue/match. Re-enter the same
    // admission gate; do not introduce any version gate into round entry.
    invalidatePollRequest();
    fetchConfig();
    return true;
}

void RankedRuntime::finishAdmission() {
    if (!m_rankedOpen || m_view.stage != RuntimeStage::AwaitingResources || m_controlBusy ||
        !ui::RankedModUpdateManager::get().ready() ||
        !RankedAudioManager::get().resourcesReady() || ui::RankAssetPackManager::get().requiresDownload()) return;
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) { showEnvironmentWarning(decision, true); return; }
    createSession();
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

void RankedRuntime::takeOverSession() {
    if (!m_rankedOpen || !m_view.accountActiveElsewhere || m_controlBusy) return;
    createSession(true);
}

void RankedRuntime::createSession(bool takeOver) {
    if (m_controlBusy) return;
    auto* account = GJAccountManager::get();
    auto const accountId = account ? account->m_accountID : 0;
    auto const username = account ? std::string(account->m_username) : std::string();
    if (accountId <= 0 || username.empty()) {
        setStage(RuntimeStage::Error, "A logged-in Geometry Dash account is required.");
        return;
    }

    matjson::Value body;
    if (m_clientInstanceId.empty()) {
        std::random_device entropy;
        m_clientInstanceId = fmt::format("{:08x}{:08x}{:08x}{:08x}", entropy(), entropy(), entropy(), entropy());
    }
    body["clientInstanceId"] = m_clientInstanceId;
    body["gdAccountId"] = fmt::format("{}", accountId);
    body["gdUsername"] = username;
    body["clientVersion"] = Mod::get()->getVersion().toVString();
    body["clientBuildChannel"] = rankedBuildChannel();
    body["installedMods"] = installedModsJson();
    body["takeOver"] = takeOver;
    auto request = baseRequest({});
    // Admission no longer waits for CSMP; keep the existing cold-server allowance.
    request.timeout(std::chrono::seconds(70));
    request.bodyJSON(body);
    m_controlBusy = true;
    m_view.accountActiveElsewhere = false;
    setStage(RuntimeStage::Loading, "Creating a server session...");
    auto const sessionGeneration = ++m_sessionGeneration;
    auto const sentAt = std::chrono::steady_clock::now();
    m_controlRequest.spawn(
        request.post(endpoint("/api/ranked/session")),
        [this, sessionGeneration, sentAt](web::WebResponse response) {
            if (sessionGeneration != m_sessionGeneration || !m_rankedOpen) return;
            m_controlBusy = false;
            if (!successful(response)) {
                if (handleRequiredUpdate(response)) return;
                auto const error = response.json().unwrapOr(matjson::Value());
                m_view.accountActiveElsewhere = response.code() == 409 &&
                    error["code"].asString().unwrapOr("") == "RANKED_ACCOUNT_ACTIVE";
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
            m_sessionLeaseDeadline = sentAt + std::chrono::seconds(std::clamp<std::int64_t>(root["sessionLeaseSeconds"].asInt().unwrapOr(90), 5, 120));
            m_nextSessionHeartbeatAt = std::chrono::steady_clock::now();
            auto const player = root["player"];
            auto const profile = parseRankedProfile(player);
            m_view.profileTier = profile.displayedTier;
            m_view.profileScoreVisible = profile.visibleRankedScore.has_value();
            m_view.profileScore = profile.visibleRankedScore.value_or(0);
            m_view.placementGames = profile.placementGames;
            m_view.placementGamesRequired = profile.placementGamesRequired;
            m_view.placementResults = profile.placementResults;
            m_view.resultStreak = profile.resultStreak;
            m_view.profileWins = profile.wins; m_view.profileLosses = profile.losses;
            m_view.promotion = profile.promotion;
            m_view.demotionWarning = profile.demotionWarning;
            m_view.placement.required =
                profile.displayedTier == "UNRANKED" ||
                profile.placementGames < profile.placementGamesRequired;
            // Completed runs can still have an unacknowledged Reveal after a lost
            // finalization response, so every session checks the recovery endpoint.
            if (root["activeMatch"].asBool().unwrapOr(false)) {
                m_view.placement.active = false;
                m_recoveringMatch = true;
                setStage(RuntimeStage::Queued, "Recovering the existing match...");
                m_nextPollAt = std::chrono::steady_clock::now();
                pollQueue();
            } else fetchActivePlacement();
        }
    );
}

void RankedRuntime::leaveRanked() {
    m_rankedOpen = false; m_sessionRejected = true;
    ++m_sessionGeneration;
    m_survivalProfileRequest.cancel(); m_modeConfigRequest.cancel();
    m_survivalProfileBusy = m_modeConfigBusy = false;
    m_nextSurvivalProfileAt = {}; m_nextModeConfigAt = {};
    ++m_standingGeneration; m_standingRequest.cancel(); m_standingBusy = false;
    m_sessionHeartbeatRequest.cancel(); m_sessionHeartbeatBusy = false;
    m_controlRequest.cancel(); m_controlBusy = false;
    ++m_historyGeneration; m_historyRequest.cancel(); m_view.historyLoading = false;
    ++m_playerInfoGeneration; ++m_forumRecordsGeneration;
    m_playerInfoRequest.cancel(); m_forumRecordsRequest.cancel();
    m_view.playerInfo.forum.loading = m_view.forumRecords.loading = false;
    invalidatePollRequest();
    auto const token = std::exchange(m_sessionToken, {});
    m_sessionLeaseDeadline = {}; m_nextSessionHeartbeatAt = {};
    if (!token.empty()) {
        auto request = baseRequest(token);
        request.timeout(std::chrono::seconds(10));
        m_sessionCloseRequest.spawn(request.post(endpoint("/api/ranked/session/close")), [](web::WebResponse) {});
    }
    setStage(RuntimeStage::Idle, "Ranked disconnected.");
}

void RankedRuntime::updateSessionHeartbeat() {
    if (!m_rankedOpen || m_sessionToken.empty() || m_sessionRejected) return;
    auto const now = std::chrono::steady_clock::now();
    if (m_sessionLeaseDeadline != std::chrono::steady_clock::time_point{} && now >= m_sessionLeaseDeadline) {
        leaveRanked();
        setStage(RuntimeStage::Error, "Ranked connection expired. Enter Ranked again.");
        return;
    }
    if (m_sessionHeartbeatBusy || now < m_nextSessionHeartbeatAt) return;
    auto request = baseRequest(m_sessionToken);
    request.timeout(std::chrono::seconds(10));
    auto const generation = m_sessionGeneration;
    m_sessionHeartbeatBusy = true;
    m_nextSessionHeartbeatAt = now + std::chrono::seconds(5);
    m_sessionHeartbeatRequest.spawn(request.post(endpoint("/api/ranked/session/heartbeat")),
        [this, generation, now](web::WebResponse response) {
            if (generation != m_sessionGeneration || !m_rankedOpen) return;
            m_sessionHeartbeatBusy = false;
            if (successful(response)) {
                auto const root = response.json().unwrapOr(matjson::Value());
                m_sessionLeaseDeadline = now + std::chrono::seconds(std::clamp<std::int64_t>(root["sessionLeaseSeconds"].asInt().unwrapOr(90), 5, 120));
                applyAudioVolumes(root["audioVolumes"]);
            } else if (response.code() == 401 || response.code() == 403 || response.code() == 426) {
                auto const error = responseError(response);
                leaveRanked(); setStage(RuntimeStage::Error, "Ranked connection is no longer valid.", error);
            }
            // Transport/5xx errors retain the lease only until its existing
            // deadline; neither retries nor stale replies can extend it locally.
        });
}

void RankedRuntime::applyAudioVolumes(matjson::Value const& volumes) {
    auto const array = volumes.asArray();
    if (array.isErr()) return;
    bool changed = false;
    for (auto const& item : array.unwrap()) {
        if (!item.isObject()) continue;
        auto const id = item["songId"].asInt().unwrapOr(0);
        auto const percent = item["volumePercent"].asInt().unwrapOr(-1);
        if (id <= 0 || id > 2147483647 || percent < 0 || percent > 100) continue;
        for (auto& resource : m_view.client.audio.resources) {
            if (resource.songId != id) continue;
            auto const volume = percent / 100.0;
            if (resource.volume != volume) { resource.volume = volume; changed = true; }
        }
    }
    if (changed) RankedAudioManager::get().configure(m_view.client);
}

void RankedRuntime::fetchActivePlacement() {
    if (m_controlBusy || m_sessionToken.empty()) return;
    auto request = baseRequest(m_sessionToken);
    m_controlBusy = true;
    setStage(RuntimeStage::Loading, "Checking for an active Placement run...");
    auto const generation = m_sessionGeneration;
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/placement/active")),
        [this, generation](web::WebResponse response) {
            if (generation != m_sessionGeneration || !m_rankedOpen) return;
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
    if (!m_view.soloEnabled) return;
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
                if (handleRequiredUpdate(response)) return;
                setStage(RuntimeStage::Ready, "Placement skip was not applied.", responseError(response));
                return;
            }
            parsePlacementState(response.json().unwrapOr(matjson::Value()));
            setStage(RuntimeStage::Matched, "Placement skipped. Showing the server result.");
        }
    );
}

void RankedRuntime::startPlacement(int startingPool, int stageCount) {
    if (!m_view.soloEnabled) return;
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
                if (handleRequiredUpdate(response)) return;
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
    if (!m_view.soloEnabled || m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready) return;
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
                if (handleRequiredUpdate(response)) return;
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
    if (!m_view.soloEnabled || m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready) return;
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
                if (handleRequiredUpdate(response)) return;
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

bool RankedRuntime::prepareSurvivalDebugAdmission(matjson::Value& body) {
    return prepareSurvivalAdmission(body);
}
#endif

bool RankedRuntime::handleSurvivalAdmissionError(web::WebResponse const& response) { return handleRequiredUpdate(response); }
bool RankedRuntime::prepareSurvivalAdmission(matjson::Value& body) {
    if (!m_view.survivalEnabled || m_controlBusy || m_sessionToken.empty() || m_view.stage != RuntimeStage::Ready) return false;
    return prepareSurvivalEnvironment(body);
}
bool RankedRuntime::prepareSurvivalEnvironment(matjson::Value& body) {
    m_installedMods = captureInstalledMods();
    auto const decision = evaluateEnvironment(m_installedMods, m_environmentPolicy);
    if (!decision.allowed) { showEnvironmentWarning(decision, true); return false; }
    body["installedMods"] = installedModsJson();
    return true;
}

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
                if (m_recoveringMatch && status == "NOT_QUEUED") {
                    m_recoveringMatch = false; fetchActivePlacement(); return;
                }
                auto const range = root["searchRange"].asInt().unwrapOr(0);
                m_view.status = range > 0
                    ? fmt::format("Searching for an opponent (rating range +/-{})...", range)
                    : "Searching for an opponent...";
                m_view.error.clear();
                ++m_view.revision;
                return;
            }
            m_view.match.matchId = root["matchId"].asString().unwrapOr("");
            m_recoveringMatch = false;
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
                std::chrono::milliseconds(roundEnteringState(m_view.match.state) ? 100 : pollIntervalMillis(m_view.match.spectatorActive));
        }
    );
}

void RankedRuntime::parsePlacementState(matjson::Value const& root) {
    if (!root.isObject()) {
        setTransientError("Invalid Placement state response");
        return;
    }
    auto const previousState = m_view.placement.state;
    auto const runId = root["runId"].asString().unwrapOr(m_view.placement.runId);
    if (runId.empty()) {
        setTransientError("Placement Run ID is missing");
        return;
    }

    auto const incomingAttemptId = root["activeAttempt"]["attemptId"].asString().unwrapOr("");
    auto const incomingVersion = root["stateVersion"].asInt().unwrapOr(0);
    if (m_acknowledgedPlacementEnds.contains(incomingAttemptId) ||
        (runId == m_view.placement.runId && incomingVersion < m_view.placement.stateVersion)) {
        // Ignore the whole stale snapshot, including score/result/timer fields.
        // A different, genuinely active UUID can still be restored normally.
        log::debug("Ignoring stale Placement state: run={} attempt={} version={}",
            runId, incomingAttemptId, incomingVersion);
        m_nextPollAt = std::chrono::steady_clock::now();
        return;
    }
    observeServerNow(root);

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
    auto const thresholds = root["mapGradeThresholds"];
    m_view.placement.mapGradeThresholds = {
        thresholds["cMin"].asDouble().unwrapOr(150), thresholds["bMin"].asDouble().unwrapOr(250),
        thresholds["aMin"].asDouble().unwrapOr(350), thresholds["sMin"].asDouble().unwrapOr(425)};
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
    if (root["matchId"].asString().unwrapOr("") == m_view.match.matchId &&
        root["stateVersion"].asInt().unwrapOr(0) > 0 &&
        root["stateVersion"].asInt().unwrapOr(0) < m_view.match.stateVersion) {
        log::debug("Ignoring older Ranked state after authoritative transition"); return;
    }
    observeServerNow(root);
    auto const previousState = m_view.match.state;
    m_view.match.state = root["state"].asString().unwrapOr("");
    m_view.match.matchType = root["matchType"].asString().unwrapOr("");
    m_view.match.stateVersion = root["stateVersion"].asInt().unwrapOr(0);
    m_view.match.deadlineAt = root["deadlineAt"].asString().unwrapOr("");
    m_view.match.side = root["side"].asString().unwrapOr("");
    auto const entry = root["entry"];
    m_view.match.entry = {};
    if (entry.isObject()) {
        m_view.match.entry.roundNumber = static_cast<int>(entry["roundNumber"].asInt().unwrapOr(0));
        m_view.match.entry.deathmatchSequence = static_cast<int>(entry["deathmatchSequence"].asInt().unwrapOr(0));
        m_view.match.entry.levelId = numFromString<int>(entry["playableLevelId"].asString().unwrapOr("")).unwrapOr(0);
        m_view.match.entry.enteredA = entry["entered"]["A"].asBool().unwrapOr(false);
        m_view.match.entry.enteredB = entry["entered"]["B"].asBool().unwrapOr(false);
        m_view.match.entry.startsAt = entry["startsAt"].asString().unwrapOr("");
    }
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
    m_view.match.decisiveRoundEnded = round["outcome"]["decisive"].asBool().unwrapOr(false);
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

    auto const settingVersion = root["spectator"]["liveSettingVersion"].asInt().unwrapOr(0);
    if (settingVersion >= m_view.match.spectatorLiveSettingVersion) {
        m_view.match.spectatorLiveEnabled = root["spectator"]["liveEnabled"].asBool().unwrapOr(true);
        m_view.match.spectatorLiveSettingVersion = settingVersion;
    }
    observeGameplayConcession(root["surrender"]["scope"].asString().unwrapOr(""),
        static_cast<int>(root["surrender"]["roundNumber"].asInt().unwrapOr(0)),
        static_cast<int>(root["surrender"]["deathmatchSequence"].asInt().unwrapOr(0)),
        root["surrender"]["side"].asString().unwrapOr(""));
    if (root["pause"].isObject()) {
        m_matchPauseBudget.bind(m_view.match.matchId);
        m_matchPauseBudget.observe(root["pause"]["remainingMs"].asDouble().unwrapOr(30000), gameplayControlNowMs());
    }
    auto const concededKey = m_view.match.surrenderDeathmatch > 0
        ? fmt::format("{}:D:{}", m_view.match.matchId, m_view.match.surrenderDeathmatch)
        : fmt::format("{}:R:{}", m_view.match.matchId, m_view.match.surrenderRound);
    if (m_view.match.surrenderScope == "MATCH" || (m_view.match.surrenderScope == "ROUND" &&
        (m_attemptContextKey == concededKey || m_gameplayStartContextKey == concededKey))) retireConcededAttemptTransport();
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
    m_view.match.scorePresentation = {};
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
            // Older immutable results do not contain record counts. Use the
            // live player projection rather than overwriting the lobby with 0.
            if (!profileAfter["A"]["wins"].asInt().isOk()) m_view.match.profileAfterA.wins = m_view.match.playerAProfile.wins;
            if (!profileAfter["A"]["losses"].asInt().isOk()) m_view.match.profileAfterA.losses = m_view.match.playerAProfile.losses;
            if (!profileAfter["B"]["wins"].asInt().isOk()) m_view.match.profileAfterB.wins = m_view.match.playerBProfile.wins;
            if (!profileAfter["B"]["losses"].asInt().isOk()) m_view.match.profileAfterB.losses = m_view.match.playerBProfile.losses;
            m_view.match.profileAfterTierA = profileAfter["A"]["displayedTier"].asString().unwrapOr("");
            m_view.match.profileAfterTierB = profileAfter["B"]["displayedTier"].asString().unwrapOr("");
            auto const scoreA = profileAfter["A"]["visibleRankedScore"].asInt();
            auto const scoreB = profileAfter["B"]["visibleRankedScore"].asInt();
            if (scoreA.isOk()) m_view.match.profileAfterScoreA = static_cast<int>(scoreA.unwrap());
            if (scoreB.isOk()) m_view.match.profileAfterScoreB = static_cast<int>(scoreB.unwrap());
        }
        m_view.match.ratingEventA = parseRatingEvent(result["events"]["A"]);
        m_view.match.ratingEventB = parseRatingEvent(result["events"]["B"]);
        m_view.match.scorePresentation = parseResultScore(result["scorePresentation"]);
        auto applyTier=[](RankedProfileView& profile,std::string& tier,StandingView const& standing) {
            if(!standing.displayTier.empty()){profile.displayedTier=standing.displayTier;tier=standing.displayTier;}
        };
        applyTier(m_view.match.profileBeforeA,m_view.match.profileBeforeTierA,m_view.match.scorePresentation.playerA.before);
        applyTier(m_view.match.profileBeforeB,m_view.match.profileBeforeTierB,m_view.match.scorePresentation.playerB.before);
        applyTier(m_view.match.profileAfterA,m_view.match.profileAfterTierA,m_view.match.scorePresentation.playerA.after);
        applyTier(m_view.match.profileAfterB,m_view.match.profileAfterTierB,m_view.match.scorePresentation.playerB.after);
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

    if (m_deathFeedContext != currentAttemptContextKey()) {
        m_deathFeedContext = currentAttemptContextKey(); m_view.match.deathLog.clear();
    }
    if (auto deaths = root["deathLog"].asArray(); deaths.isOk()) {
        auto const context = currentAttemptContextKey();
        for (auto const& event : deaths.unwrap()) {
            auto const side = event["side"].asString().unwrapOr("");
            if (side == m_view.match.side) continue; // own deaths appear immediately from native death
            recordDeath({event["id"].asString().unwrapOr(""), side,
                event["progressPercent"].asDouble().unwrapOr(0), event["endedAtMs"].asInt().unwrapOr(0)}, context);
        }
    }

    if (m_view.match.decisiveRoundEnded) retireDecisiveAttempt();
    if (!stateAllowsActiveAttempt(m_view.match.state) && !nativeEntryStartGranted()) {
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

bool RankedRuntime::gameplayConceded(int round, int deathmatch) const {
    if (m_concessions.conceded(m_view.match.matchId, round, deathmatch)) return true;
    auto const& match = m_view.match;
    if (match.surrenderScope == "MATCH") return true;
    if (match.surrenderScope != "ROUND") return false;
    return deathmatch > 0 ? deathmatch == match.surrenderDeathmatch :
        match.surrenderDeathmatch == 0 && round > 0 && round == match.surrenderRound;
}

void RankedRuntime::observeGameplayConcession(std::string const& scope, int round, int deathmatch, std::string const& side) {
    if (!m_concessions.observe(m_view.match.matchId, scope, round, deathmatch)) return;
    auto& match = m_view.match;
    // Retain the old scene command without replacing a newer opponent notice
    // when an earlier POST ACK arrives after a later authoritative state.
    if (match.surrenderScope == "MATCH" && scope != "MATCH") return;
    if (scope != "MATCH" && (deathmatch < match.surrenderDeathmatch ||
        (deathmatch == 0 && match.surrenderDeathmatch == 0 && round < match.surrenderRound))) return;
    match.surrenderScope = scope; match.surrenderRound = round; match.surrenderDeathmatch = deathmatch;
    match.surrenderSide = side;
}

bool RankedRuntime::opponentConceded(int round, int deathmatch) const {
    auto const& match = m_view.match;
    return (match.side == "A" || match.side == "B") &&
        (match.surrenderSide == "A" || match.surrenderSide == "B") &&
        match.surrenderSide != match.side && gameplayConceded(round, deathmatch);
}

void RankedRuntime::retireConcededAttemptTransport() {
    // Explicit authenticated concession is different from a deadline. Normal
    // timer=0 final Attempts still retain their Start/End journal and authority.
    invalidateAttemptRequest();
    ++m_attemptIntentRequestGeneration; m_attemptIntentRequest.cancel(); m_attemptIntentBusy = false;
    ++m_progressRequestGeneration; m_progressRequest.cancel(); m_progressBusy = false;
    m_pendingStart.reset(); m_pendingEnd.reset(); m_attemptBacklog.clear(); m_attemptIntentBacklog.clear();
    m_attemptId.clear(); m_attemptLevelId = 0; m_attemptContextKey.clear();
    m_pendingProgress.reset(); m_gameplayStartEligible = false;
}

bool RankedRuntime::placementMapSkipped(std::string const& runId, int stageIndex, int mapIndex) const {
    return m_skippedPlacementMaps.contains(fmt::format("{}:P:{}:{}", runId, stageIndex, mapIndex));
}

void RankedRuntime::skipPlacementMap(int levelId, double progressPercent) {
    if (m_pendingPlacementMapSkip) return;
    auto const& placement = m_view.placement;
    if (!isPlacement() || isSpectating() || m_view.stage != RuntimeStage::Matched ||
        (placement.state != "MAP_PLAYING" && placement.state != "MAP_SETTLING") ||
        !m_view.match.currentMap || m_view.match.currentMap->levelId != levelId ||
        !std::isfinite(progressPercent) || progressPercent < 0 || progressPercent > 100) {
        m_placementMapSkipStatus = SurrenderStatus::Rejected; return;
    }
    // Pause freezes native progress. Deliver that exact visual Attempt End
    // through the existing idempotent FIFO before settling this one map.
    if ((hasLocalOpenAttempt() || (!hasLocalAttemptInFlight() && progressPercent > 0)) &&
        !reportAttemptEnd(levelId, progressPercent, false)) {
        m_placementMapSkipStatus = SurrenderStatus::Rejected; return;
    }
    matjson::Value body;
    body["clientEventId"] = newEventId("placement-map-skip");
    body["stageIndex"] = placement.currentStageIndex;
    body["mapIndex"] = placement.currentMapIndex;
    body["levelId"] = std::to_string(levelId);
    m_pendingPlacementMapSkip = std::move(body);
    m_placementMapSkipRunId = placement.runId;
    ++m_placementMapSkipGeneration;
    m_placementMapSkipStatus = hasLocalAttemptInFlight() ? SurrenderStatus::WaitingAttempt : SurrenderStatus::Sending;
    m_nextPlacementMapSkipAt = {};
    flushPlacementMapSkip();
}

void RankedRuntime::flushPlacementMapSkip() {
    if (!m_pendingPlacementMapSkip || m_placementMapSkipBusy ||
        std::chrono::steady_clock::now() < m_nextPlacementMapSkipAt) return;
    auto const& body = *m_pendingPlacementMapSkip;
    auto const stage = static_cast<int>(body["stageIndex"].asInt().unwrapOr(0));
    auto const map = static_cast<int>(body["mapIndex"].asInt().unwrapOr(0));
    auto const runId = m_placementMapSkipRunId;
    auto const& placement = m_view.placement;
    if (!isPlacement() || placement.runId != runId || placement.currentStageIndex != stage ||
        placement.currentMapIndex != map || (placement.state != "MAP_PLAYING" &&
        placement.state != "MAP_SETTLING" && placement.state != "MAP_RESULT")) {
        m_pendingPlacementMapSkip.reset(); m_placementMapSkipStatus = SurrenderStatus::Rejected; return;
    }
    if (hasLocalAttemptInFlight()) {
        m_placementMapSkipStatus = SurrenderStatus::WaitingAttempt;
        flushAttemptEvents(); return;
    }
    auto request = baseRequest(m_sessionToken); request.bodyJSON(body);
    auto const generation = m_placementMapSkipGeneration;
    auto const levelId = body["levelId"].asString().unwrapOr("");
    m_placementMapSkipBusy = true; m_placementMapSkipStatus = SurrenderStatus::Sending;
    m_placementMapSkipRequest.spawn(request.post(endpoint("/api/ranked/placement/" + runId + "/map/skip")),
        [this, runId, stage, map, levelId, generation](web::WebResponse response) {
            if (generation != m_placementMapSkipGeneration || runId != m_view.placement.runId) return;
            m_placementMapSkipBusy = false;
            if (successful(response)) {
                auto const root = response.json().unwrapOr(matjson::Value());
                if (!root["accepted"].asBool().unwrapOr(false) || root["runId"].asString().unwrapOr("") != runId ||
                    root["skippedMap"]["stageIndex"].asInt().unwrapOr(0) != stage ||
                    root["skippedMap"]["mapIndex"].asInt().unwrapOr(0) != map ||
                    root["skippedMap"]["levelId"].asString().unwrapOr("") != levelId) {
                    m_pendingPlacementMapSkip.reset(); m_placementMapSkipStatus = SurrenderStatus::Rejected;
                    setTransientError("Invalid Placement map skip acknowledgement"); return;
                }
                m_skippedPlacementMaps.insert(fmt::format("{}:P:{}:{}", runId, stage, map));
                m_pendingPlacementMapSkip.reset(); m_placementMapSkipStatus = SurrenderStatus::Accepted;
                parsePlacementState(root);
                log::info("[PlacementSkip] accepted run={} stage={} map={}", runId, stage, map);
            } else if (response.code() >= 400 && response.code() < 500) {
                m_pendingPlacementMapSkip.reset(); m_placementMapSkipStatus = SurrenderStatus::Rejected;
                setTransientError(responseError(response));
            } else {
                m_placementMapSkipStatus = SurrenderStatus::Retrying;
                m_nextPlacementMapSkipAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            }
        });
}

void RankedRuntime::resetPlacementMapSkip() {
    ++m_placementMapSkipGeneration; m_placementMapSkipRequest.cancel();
    m_pendingPlacementMapSkip.reset(); m_placementMapSkipBusy = false;
    m_placementMapSkipRunId.clear(); m_skippedPlacementMaps.clear();
    m_placementMapSkipStatus = SurrenderStatus::Idle; m_nextPlacementMapSkipAt = {};
}

void RankedRuntime::surrenderCurrentMatch(bool wholeMatch) {
    if (m_view.stage != RuntimeStage::Matched || isPlacement() || m_view.match.matchId.empty() ||
        !(stateAllowsActiveAttempt(m_view.match.state) || nativeEntryStartGranted())) {
        m_surrenderStatus = SurrenderStatus::Rejected;
        return;
    }
    if (m_pendingSurrender) return;
    matjson::Value body;
    body["scope"] = wholeMatch ? "MATCH" : "ROUND";
    body["clientEventId"] = newEventId("surrender");
    body["roundNumber"] = m_view.match.roundNumber;
    body["deathmatchSequence"] = m_view.match.deathmatchSequence;
    m_pendingSurrender = std::move(body);
    m_surrenderMatchId = m_view.match.matchId;
    ++m_surrenderRequestGeneration;
    m_surrenderStatus = hasLocalAttemptInFlight() && !hasLocalOpenAttempt()
        ? SurrenderStatus::WaitingAttempt : SurrenderStatus::Sending;
    log::info("[RankedSurrender] queued scope={} round={} deathmatch={} waitingAttempt={}",
        wholeMatch ? "MATCH" : "ROUND", m_view.match.roundNumber, m_view.match.deathmatchSequence,
        m_surrenderStatus == SurrenderStatus::WaitingAttempt);
    m_nextSurrenderAt = {}; flushGameplayControls();
}

std::string RankedRuntime::beginMatchGameplayPause(int levelId) {
    if (m_view.stage != RuntimeStage::Matched || isPlacement() ||
        !isGameplayLevel(levelId) || !(stateAllowsActiveAttempt(m_view.match.state) || nativeEntryStartGranted())) return {};
    if (m_pauseMatchId != m_view.match.matchId) {
        m_pauseRequest.cancel(); m_pauseBusy = false;
        m_pauseMatchId = m_view.match.matchId; m_pauseLeaseId.clear();
    }
    m_matchPauseBudget.bind(m_pauseMatchId);
    if (m_pauseDesired) return m_pauseLeaseId;
    m_pauseLeaseId = newEventId("pause"); m_pauseDesired = true; m_pauseNeedsSync = true;
    SpectatorLiveRuntime::get().publisherPaused(true);
    m_matchPauseBudget.begin(gameplayControlNowMs()); m_nextPauseAt = {};
    flushGameplayControls(); return m_pauseLeaseId;
}
void RankedRuntime::endMatchGameplayPause(std::string const& leaseId) {
    if (leaseId.empty() || leaseId != m_pauseLeaseId || !m_pauseDesired) return;
    m_matchPauseBudget.end(gameplayControlNowMs());
    SpectatorLiveRuntime::get().publisherPaused(false);
    m_pauseDesired = false; m_pauseNeedsSync = true; m_nextPauseAt = {};
    flushGameplayControls();
}
double RankedRuntime::matchPauseRemainingMs() const {
    return m_matchPauseBudget.remaining(gameplayControlNowMs());
}
void RankedRuntime::syncMatchGameplayPause(bool nativePaused) {
    if (!nativePaused && m_pauseDesired) endMatchGameplayPause(m_pauseLeaseId);
}
void RankedRuntime::flushGameplayControls() {
    auto const now = std::chrono::steady_clock::now();
    if (!m_pauseBusy && m_pauseNeedsSync && now >= m_nextPauseAt && !m_pauseMatchId.empty() &&
        m_pauseMatchId == m_view.match.matchId) {
        auto request = baseRequest(m_sessionToken, m_matchToken);
        matjson::Value body; body["leaseId"] = m_pauseLeaseId; body["paused"] = m_pauseDesired;
        request.bodyJSON(body);
        auto const matchId = m_pauseMatchId, lease = m_pauseLeaseId;
        auto const paused = m_pauseDesired; m_pauseBusy = true; m_pauseNeedsSync = false;
        m_pauseRequest.spawn(request.post(endpoint("/api/ranked/matches/" + matchId + "/pause")),
            [this, matchId, lease, paused](web::WebResponse response) {
                if (matchId != m_pauseMatchId) return;
                m_pauseBusy = false;
                if (successful(response)) {
                    auto const root = response.json().unwrapOr(matjson::Value());
                    if (lease == m_pauseLeaseId) {
                        m_matchPauseBudget.observe(root["remainingMs"].asDouble().unwrapOr(0), gameplayControlNowMs());
                        if (!root["accepted"].asBool().unwrapOr(false)) m_matchPauseBudget.observe(0, gameplayControlNowMs());
                    }
                } else { m_pauseNeedsSync = true; m_nextPauseAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500); }
                if (lease != m_pauseLeaseId || paused != m_pauseDesired) m_pauseNeedsSync = true;
            });
    }
    if (!m_surrenderBusy && m_pendingSurrender && now >= m_nextSurrenderAt) {
        auto const& body = *m_pendingSurrender;
        if (m_view.stage != RuntimeStage::Matched || m_surrenderMatchId != m_view.match.matchId ||
            body["roundNumber"].asInt().unwrapOr(0) != m_view.match.roundNumber ||
            body["deathmatchSequence"].asInt().unwrapOr(0) != m_view.match.deathmatchSequence ||
            !(stateAllowsActiveAttempt(m_view.match.state) || nativeEntryStartGranted())) {
            m_pendingSurrender.reset(); m_surrenderStatus = SurrenderStatus::Rejected;
            log::warn("[RankedSurrender] discarded stale round request");
            return;
        }
        if (hasLocalAttemptInFlight() && !hasLocalOpenAttempt()) {
            m_surrenderStatus = SurrenderStatus::WaitingAttempt;
            flushAttemptEvents(); return;
        }
        auto request = baseRequest(m_sessionToken, m_matchToken); request.bodyJSON(body);
        auto const matchId = m_surrenderMatchId;
        auto const generation = m_surrenderRequestGeneration;
        auto const round = m_view.match.roundNumber;
        auto const deathmatch = m_view.match.deathmatchSequence;
        auto const scope = body["scope"].asString().unwrapOr("");
        auto const side = m_view.match.side;
        m_surrenderBusy = true; m_surrenderStatus = SurrenderStatus::Sending;
        log::info("[RankedSurrender] POST scope={} round={}",
            body["scope"].asString().unwrapOr(""), m_view.match.roundNumber);
        m_surrenderRequest.spawn(request.post(endpoint("/api/ranked/matches/" + matchId + "/surrender")),
            [this, matchId, generation, round, deathmatch, scope, side](web::WebResponse response) {
                if (generation != m_surrenderRequestGeneration || matchId != m_view.match.matchId) return;
                m_surrenderBusy = false;
                auto const sameRound = round == m_view.match.roundNumber && deathmatch == m_view.match.deathmatchSequence;
                if (!sameRound && !successful(response)) {
                    m_pendingSurrender.reset(); m_surrenderStatus = SurrenderStatus::Rejected;
                    return;
                }
                log::info("[RankedSurrender] response status={}", response.code());
                if (successful(response)) {
                    // ACK closes this exact scene even if a following GET has
                    // advanced the public round or omits its surrender object.
                    observeGameplayConcession(scope, round, deathmatch, side);
                    auto const key = deathmatch > 0 ? fmt::format("{}:D:{}", matchId, deathmatch)
                        : fmt::format("{}:R:{}", matchId, round);
                    if (scope == "MATCH" || key == m_attemptContextKey || key == m_gameplayStartContextKey)
                        retireConcededAttemptTransport();
                    m_pendingSurrender.reset();
                    if (sameRound) parseMatchState(response.json().unwrapOr(matjson::Value()));
                    m_surrenderStatus = SurrenderStatus::Accepted;
                }
                else if (response.code() >= 400 && response.code() < 500) {
                    m_pendingSurrender.reset(); m_surrenderStatus = SurrenderStatus::Rejected;
                    setTransientError(responseError(response));
                } else {
                    m_surrenderStatus = SurrenderStatus::Retrying;
                    m_nextSurrenderAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                }
            });
    }
}

void RankedRuntime::resetGameplayControls() {
    resetPlacementMapSkip();
    m_concessions.bind({});
    SpectatorLiveRuntime::get().publisherPaused(false);
    m_pauseRequest.cancel(); m_surrenderRequest.cancel();
    m_pauseBusy = m_pauseDesired = m_pauseNeedsSync = m_surrenderBusy = false;
    m_pauseMatchId.clear(); m_pauseLeaseId.clear(); m_pendingSurrender.reset();
    m_surrenderMatchId.clear(); ++m_surrenderRequestGeneration;
    m_surrenderStatus = SurrenderStatus::Idle;
    m_matchPauseBudget.bind("");
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
    m_nextStandingAt = {};
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
    resetGameplayControls();
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
        m_view.profileWins = profile.wins; m_view.profileLosses = profile.losses;
        m_view.promotion = profile.promotion;
        m_view.demotionWarning = profile.demotionWarning;
        if (m_view.match.scorePresentation.available) m_view.standing = ownSide == "A"
            ? m_view.match.scorePresentation.playerA.after : m_view.match.scorePresentation.playerB.after;
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

void RankedRuntime::fetchHistory(RankedMode mode) {
    fetchHistoryScope(mode == RankedMode::Survival ? HistoryScope::Survival : HistoryScope::SoloRanked);
}

void RankedRuntime::fetchHistoryScope(HistoryScope scope) {
    if (m_sessionToken.empty() || !m_rankedOpen) return;
    auto const historyGeneration = ++m_historyGeneration;
    m_historyRequest.cancel();
    m_view.historyScope = scope;
    ++m_view.historyRevision;
    m_view.history.clear();
    m_view.historyLoading = true;
    m_view.historyError.clear();
    ++m_view.revision;
    auto request = baseRequest(m_sessionToken);
    auto const generation = m_sessionGeneration;
    m_historyRequest.spawn(
        request.get(endpoint(fmt::format("/api/ranked/history?scope={}",historyScopeKey(scope)))),
        [this, generation, historyGeneration, scope](web::WebResponse response) {
            if (generation != m_sessionGeneration || historyGeneration != m_historyGeneration || !m_rankedOpen) return;
            m_view.historyLoading = false;
            ++m_view.historyRevision;
            if (!successful(response)) {
                m_view.historyError = responseError(response);
                ++m_view.revision;
                return;
            }
            auto const root = response.json().unwrapOr(matjson::Value());
            if (root["scope"].asString().unwrapOr("") != historyScopeKey(scope) || !root["matches"].isArray()) {
                m_view.historyError = "Invalid match history response";
                ++m_view.revision;
                return;
            }
            observeServerNow(root);
            m_view.history.clear();
            auto const matches = root["matches"].asArray();
            if (matches.isOk()) {
                for (auto const& item : matches.unwrap()) {
                    if (!item.isObject()) continue;
                    HistoryMatchView history;
                    history.mode = item["mode"].asString().unwrapOr("SOLO_RANKED") == "SURVIVAL" ? RankedMode::Survival : RankedMode::SoloRanked;
                    history.matchId = item["matchId"].asString().unwrapOr("");
                    history.finishedAt = item["finishedAt"].asString().unwrapOr("");
                    if(history.mode==RankedMode::Survival) {
                        history.survivalRecord=item;
                        history.survivalRank=static_cast<int>(item["result"]["rank"].asInt().unwrapOr(0));
                        history.ownRankedScoreDelta=static_cast<int>(item["result"]["actualDelta"].asInt().unwrapOr(0));
                        history.ownRankedScoreAfter=static_cast<int>(item["result"]["profileAfter"]["lp"].asInt().unwrapOr(0));
                    }
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
    auto const generation = m_sessionGeneration;
    m_controlRequest.spawn(
        request.get(endpoint("/api/ranked/achievements")),
        [this, generation](web::WebResponse response) {
            if (generation != m_sessionGeneration || !m_rankedOpen) return;
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

void RankedRuntime::fetchPlayerInfo(std::string const& side) {
    if ((side != "A" && side != "B") || m_sessionToken.empty() || m_matchToken.empty() ||
        m_view.stage != RuntimeStage::Matched || m_view.match.matchId.empty()) return;
    m_playerInfoRequest.cancel();
    auto const generation = ++m_playerInfoGeneration;
    auto const matchId = m_view.match.matchId;
    auto const session = m_sessionToken;
    m_view.playerInfo = {};
    auto& info = m_view.playerInfo;
    info.side = side;
    info.accountId = side == "A" ? m_view.match.playerAAccountId : m_view.match.playerBAccountId;
    info.username = side == "A" ? m_view.match.playerAName : m_view.match.playerBName;
    if (m_view.match.matchType == "DEBUG_BOT" && side == "B") {
        info.accountId = 16792985; info.username = "CoJong"; info.botReference = true;
    }
    info.forum.loading = true;
    ++m_view.revision;
    auto request = baseRequest(session, m_matchToken);
    m_playerInfoRequest.spawn(request.get(endpoint("/api/ranked/matches/" + matchId + "/player-info/" + side)),
        [this, generation, matchId, session](web::WebResponse response) {
            if (generation != m_playerInfoGeneration || matchId != m_view.match.matchId || session != m_sessionToken) return;
            auto& info = m_view.playerInfo;
            info.forum.loading = false;
            if (!successful(response)) { info.forum.error = responseError(response); ++m_view.revision; return; }
            auto const root = response.json().unwrapOr(matjson::Value());
            auto const account = numFromString<int>(root["gdAccountId"].asString().unwrapOr("")).unwrapOr(0);
            if (account != info.accountId) { info.forum.error = "Player identity changed"; ++m_view.revision; return; }
            info.forum = parseForumRecords(root);
            ++m_view.revision;
        });
}

void RankedRuntime::fetchForumRecords(bool refresh) {
    if (m_sessionToken.empty() || m_view.forumRecords.loading ||
        (refresh && forumRefreshRemainingMs() > 0)) return;
    auto const generation = ++m_forumRecordsGeneration;
    auto const session = m_sessionToken;
    m_view.forumRecords.loading = true; m_view.forumRecords.error.clear(); ++m_view.revision;
    auto request = baseRequest(session);
    auto task = refresh ? request.post(endpoint("/api/ranked/session/forum-records/refresh"))
                        : request.get(endpoint("/api/ranked/session/forum-records"));
    m_forumRecordsRequest.spawn(std::move(task), [this, generation, session](web::WebResponse response) {
        if (generation != m_forumRecordsGeneration || session != m_sessionToken) return;
        m_view.forumRecords.loading = false;
        if (!successful(response)) { m_view.forumRecords.error = responseError(response); ++m_view.revision; return; }
        auto const root = response.json().unwrapOr(matjson::Value());
        observeServerNow(root);
        m_view.forumRecords = parseForumRecords(root);
        ++m_view.revision;
    });
}

std::int64_t RankedRuntime::forumRefreshRemainingMs() const {
    if (m_view.forumRecords.refreshAvailableAt.empty()) return 0;
    return std::clamp<std::int64_t>(m_serverClock.remainingMillis(
        m_view.forumRecords.refreshAvailableAt, localNowMillis()).value_or(3600000), 0, 3600000);
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
            card.scope = value["scope"].asString().unwrapOr("");
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
    m_gameplayStartedAt = nativeEntryStartGranted()
        ? m_view.match.entry.startsAt
        : iso8601FromMillis(m_serverClock.serverNowMillis(localNowMillis()));
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

    if (m_view.match.deathmatchSequence > 0) {
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
    if (!isSpectating() || !m_view.match.spectatorLiveEnabled) return false;
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

bool RankedRuntime::canEnterNativeRoundScene() const {
    auto const& match = m_view.match;
    if (m_view.stage != RuntimeStage::Matched || isPlacement() || isSpectating() ||
        !roundEnteringState(match.state) || match.matchId.empty() || !match.currentMap ||
        match.entry.levelId != match.currentMap->levelId) return false;
    return match.deathmatchSequence > 0
        ? match.entry.roundNumber == 0 && match.entry.deathmatchSequence == match.deathmatchSequence
        : match.entry.roundNumber == match.roundNumber && match.entry.deathmatchSequence == 0;
}

std::optional<std::int64_t> RankedRuntime::nativeEntryCountdownMillis() const {
    if (m_view.match.entry.startsAt.empty()) return std::nullopt;
    return m_serverClock.remainingMillis(m_view.match.entry.startsAt, localNowMillis());
}

bool RankedRuntime::nativeEntryStartGranted() const {
    if (!canEnterNativeRoundScene() || !m_view.match.entry.enteredA ||
        !m_view.match.entry.enteredB || m_view.match.entry.startsAt.empty()) return false;
    auto const remaining = m_serverClock.signedRemainingMillis(
        m_view.match.entry.startsAt, localNowMillis());
    // This exact-context server certificate avoids another polling RTT at GO.
    // A disconnected/stale certificate cannot open a new attempt indefinitely.
    return remaining && *remaining <= 0 && *remaining >= -3'000;
}

void RankedRuntime::noteNativeRoundSceneEntered(int levelId) {
    if (!canEnterNativeRoundScene() || levelId != m_view.match.entry.levelId) return;
    auto const& match = m_view.match;
    auto const key = fmt::format("{}:{}:{}:{}", match.matchId, match.entry.roundNumber,
        match.entry.deathmatchSequence, levelId);
    if (key != m_nativeEntryKey) {
        ++m_nativeEntryGeneration;
        m_nativeEntryRequest.cancel();
        m_nativeEntryKey = key;
        m_nativeEntryBusy = false;
        m_nextNativeEntryAt = {};
    }
    auto const ownEntered = match.side == "A" ? match.entry.enteredA : match.entry.enteredB;
    if (ownEntered || m_nativeEntryBusy || std::chrono::steady_clock::now() < m_nextNativeEntryAt) return;
    matjson::Value body;
    body["roundNumber"] = match.entry.roundNumber;
    body["deathmatchSequence"] = match.entry.deathmatchSequence;
    body["playableLevelId"] = std::to_string(levelId);
    auto request = baseRequest(m_sessionToken, m_matchToken); request.bodyJSON(body);
    auto const generation = m_nativeEntryGeneration;
    auto const matchId = match.matchId;
    m_nativeEntryBusy = true;
    log::info("[RankedEntry] native scene entered context={}", key);
    m_nativeEntryRequest.spawn(request.post(endpoint("/api/ranked/matches/" + matchId + "/entered")),
        [this, key, generation, matchId](web::WebResponse response) {
            if (generation != m_nativeEntryGeneration || key != m_nativeEntryKey ||
                matchId != m_view.match.matchId) return;
            m_nativeEntryBusy = false;
            if (!canEnterNativeRoundScene() || key != fmt::format("{}:{}:{}:{}",
                m_view.match.matchId, m_view.match.entry.roundNumber,
                m_view.match.entry.deathmatchSequence, m_view.match.entry.levelId)) return;
            m_nextNativeEntryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            if (successful(response)) {
                parseMatchState(response.json().unwrapOr(matjson::Value()));
                m_nextPollAt = {};
            } else {
                log::warn("[RankedEntry] entry acknowledgement failed context={} status={}", key, response.code());
                setTransientError(responseError(response));
            }
        });
}

std::string RankedRuntime::placementDisplayGrade(double score) const {
    auto const& placement = m_view.placement;
    if (placement.currentMapClears >= 2 || score >= placement.mapGradeThresholds[3]) return "S";
    if (score >= placement.mapGradeThresholds[2]) return "A";
    if (score >= placement.mapGradeThresholds[1]) return "B";
    if (score >= placement.mapGradeThresholds[0]) return "C";
    return "D";
}

double RankedRuntime::localDisplayScore(double progressPercent, std::optional<double> qualifyingPercentOverride) const {
    if (isSpectating() || m_spectatorLocalTransportOwner != nullptr) progressPercent = -1.0;
    if (isPlacement()) {
        auto const& placement = m_view.placement;
        auto score = std::max(placement.currentMapScore, placement.provisionalMapScore);
        if (progressPercent >= 0 && hasLocalOpenAttempt() && m_view.match.currentMap) {
            auto const qualifying = qualifyingPercentOverride.value_or(m_view.match.currentMap->qualifyingPercent);
            if (std::isfinite(qualifying) && qualifying >= 0 && qualifying <= 100) {
                double bestCompleted = 0;
                for (auto const& attempt : placement.currentMapTopAttempts)
                    bestCompleted = std::max(bestCompleted, attempt.awardedScore);
                score = std::max(score, bestCompleted + calculateAttemptScore(progressPercent, false, qualifying));
            }
        }
        return std::clamp(score, 0.0, 500.0);
    }
    auto const ownIsA = m_view.match.side == "A";
    if (m_view.match.deathmatchSequence > 0) {
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
        (stateAllowsActiveAttempt(m_view.match.state) || nativeEntryStartGranted()) &&
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
        if (state != "MAP_PLAYING" || placementMapSkipPending()) return false;
        auto const signedRemaining = signedDeadlineMillis();
        return !signedRemaining || *signedRemaining > -10'000;
    }
    if (nativeEntryStartGranted()) return true;
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
    if (isPlacement() && placementMapSkipPending()) return false;
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

    if (m_view.match.deathmatchSequence > 0) {
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
    if (!isPlacement() && m_view.match.deathmatchSequence <= 0) {
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

void RankedRuntime::recordDeath(DeathFeedEvent event, std::string const& contextKey) {
    if (m_deathFeedContext != contextKey) {
        m_deathFeedContext = contextKey; m_view.match.deathLog.clear();
    }
    if (event.id.empty() || (event.side != "A" && event.side != "B")) return;
    auto& events = m_view.match.deathLog;
    if (std::any_of(events.begin(), events.end(), [&event](auto const& old) { return old.id == event.id; })) return;
    events.push_back(std::move(event));
    std::stable_sort(events.begin(), events.end(), [](auto const& a, auto const& b) { return a.endedAtMs < b.endedAtMs; });
    if (events.size() > 3) events.erase(events.begin(), events.end() - 3);
}

std::int64_t RankedRuntime::serverNowMillis() const {
    return m_serverClock.serverNowMillis(localNowMillis());
}

void RankedRuntime::retireDecisiveAttempt() {
    // The server explicitly revoked all open attempts after the decisive
    // deadline. Retire that exact match's local journal without scoring it.
    invalidateAttemptRequest();
    ++m_progressRequestGeneration; m_progressRequest.cancel(); m_progressBusy = false;
    ++m_attemptIntentRequestGeneration; m_attemptIntentRequest.cancel(); m_attemptIntentBusy = false;
    m_attemptId.clear(); m_attemptLevelId = 0; m_attemptContextKey.clear();
    m_pendingStart.reset(); m_pendingEnd.reset(); m_attemptBacklog.clear(); m_attemptIntentBacklog.clear();
    m_pendingProgress.reset(); m_gameplayStartEligible = false;
    m_nextAttemptRetryAt = {}; m_nextIntentRetryAt = {};
}

bool RankedRuntime::reportAttemptEnd(int levelId, double progressPercent, bool cleared, std::optional<double> qualifyingPercentOverride, bool nativeDeath) {
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
    if (m_view.match.deathmatchSequence > 0) {
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
        .nativeDeath = nativeDeath && !cleared,
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

    if (end.nativeDeath) recordDeath({end.eventId, m_view.match.side, finalProgress, observedEndMillis}, contextKey);
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
            if (m_view.match.deathmatchSequence > 0) {
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
    auto const endingAttemptId = m_attemptId;
    matjson::Value body;
    body["levelId"] = fmt::format("{}", ending.levelId);
    body["attemptId"] = endingAttemptId;
    body["clientEventId"] = ending.eventId;
    body["progressPercent"] = ending.progressPercent;
    body["cleared"] = ending.cleared;
    if (!isPlacement()) body["nativeDeath"] = ending.nativeDeath;
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
        [this, ending, endingAttemptId, requestGeneration, placement](web::WebResponse response) {
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
                if (accepted || reason == "ATTEMPT_ALREADY_ENDED") {
                    m_acknowledgedPlacementEnds.insert(endingAttemptId);
                }
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

void RankedRuntime::fetchStanding() {
    if (!m_rankedOpen || m_sessionToken.empty() || m_sessionRejected || m_standingBusy) return;
    m_standingBusy = true;
    m_nextStandingAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto const generation = ++m_standingGeneration;
    auto const session = m_sessionToken;
    auto request = baseRequest(session);
    request.timeout(std::chrono::seconds(10));
    m_standingRequest.spawn(request.get(endpoint("/api/ranked/session/standing")),
        [this, generation, session](web::WebResponse response) {
            if (generation != m_standingGeneration || session != m_sessionToken || !m_rankedOpen) return;
            m_standingBusy = false;
            if (!successful(response)) return;
            m_view.standing = parseStanding(response.json().unwrapOr(matjson::Value())["standing"]);
            if(!m_view.standing.displayTier.empty()) {
                m_view.profileTier=m_view.standing.displayTier;
            }
            ++m_view.revision;
        });
}

void RankedRuntime::fetchModeAvailability() {
    if (!m_rankedOpen || m_sessionToken.empty() || m_sessionRejected || m_modeConfigBusy) return;
    m_modeConfigBusy = true;
    m_nextModeConfigAt = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto const generation = m_sessionGeneration;
    auto request = baseRequest({}); request.timeout(std::chrono::seconds(10));
    m_modeConfigRequest.spawn(request.get(endpoint("/api/ranked/config")), [this,generation](web::WebResponse response) {
        if (generation != m_sessionGeneration || !m_rankedOpen) return;
        m_modeConfigBusy = false;
        if (!successful(response)) return;
        auto const root = response.json().unwrapOr(matjson::Value());
        auto const restrictionEnabled = root["modRestrictionEnabled"].asBool().unwrapOr(true);
        if (m_environmentPolicy.modRestrictionEnabled != restrictionEnabled) {
            m_environmentPolicy.modRestrictionEnabled = restrictionEnabled;
            m_installedMods = captureInstalledMods();
            auto const decision = evaluateEnvironment(m_installedMods,m_environmentPolicy);
            // A live policy refresh updates admission warnings without resetting
            // sessions, attempts or an already running match.
            if (!decision.allowed) showEnvironmentWarning(decision,false);
            else {
                m_view.environmentWarning = {};
                if(m_view.stage==RuntimeStage::Blocked)setStage(m_view.match.matchId.empty()?RuntimeStage::Ready:RuntimeStage::Matched,
                    "Ranked mod environment is ready.");
            }
            ++m_view.revision;
        }
        m_view.soloEnabled = root["queueEnabled"].asBool().unwrapOr(false);
        m_view.survivalEnabled = root["modes"]["survival"]["enabled"].asBool().unwrapOr(false);
        m_view.survivalConfigReady = root["modes"]["survival"]["ready"].asBool().unwrapOr(false);
    });
}

void RankedRuntime::fetchSurvivalProfile() {
    if (!m_rankedOpen || m_sessionToken.empty() || m_sessionRejected || m_survivalProfileBusy) return;
    m_survivalProfileBusy = true; m_view.survival.loading = true; m_view.survival.error.clear();
    m_nextSurvivalProfileAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto const generation = m_sessionGeneration;
    auto request = baseRequest(m_sessionToken); request.timeout(std::chrono::seconds(15));
    m_survivalProfileRequest.spawn(request.get(endpoint("/api/ranked/survival/profile")), [this,generation](web::WebResponse response) {
        if (generation != m_sessionGeneration || !m_rankedOpen) return;
        m_survivalProfileBusy = false; m_view.survival.loading = false;
        if (!successful(response)) { m_view.survival.error = responseError(response); ++m_view.revision; return; }
        auto const root = response.json().unwrapOr(matjson::Value());
        auto const profile = root["profile"];
        if (!profile.isObject() || !profile["lp"].isNumber()) { m_view.survival.error = "Invalid Survival profile"; ++m_view.revision; return; }
        auto& sur = m_view.survival;
        sur.completedMatches=static_cast<int>(root["recordSummary"]["completedMatches"].asInt().unwrapOr(0));
        sur.wins=static_cast<int>(root["recordSummary"]["wins"].asInt().unwrapOr(0));
        sur.losses=static_cast<int>(root["recordSummary"]["losses"].asInt().unwrapOr(0));
        sur.bestRank=root["recordSummary"]["bestRank"].isNumber()?std::optional<int>(static_cast<int>(root["recordSummary"]["bestRank"].asInt().unwrapOr(0))):std::nullopt;
        sur.loaded = true;sur.onlineAvailable=root["onlineAvailable"].asBool().unwrapOr(false);
        sur.profile.displayedTier = profile["tier"].asString().unwrapOr("RED");
        sur.tierId = profile["tierId"].asString().unwrapOr(sur.profile.displayedTier);
        sur.profile.visibleRankedScore = static_cast<int>(profile["lp"].asInt().unwrapOr(0));
        sur.profile.demotionWarning = profile["demotionWarning"].asBool().unwrapOr(false);
        auto const recovery = profile["warningRecoveryLp"].asInt();
        sur.warningRecoveryLp = recovery.isOk() ? std::optional<int>(static_cast<int>(recovery.unwrap())) : std::nullopt;
        sur.standing = parseStanding(root["standing"]);
        observeServerNow(root); ++m_view.revision;
    });
}

void RankedRuntime::applyAdminProjection(matjson::Value const& result,std::string const& scope) {
    if(scope=="COMMON" && result["accountsDeleted"].asBool().unwrapOr(false)){
        leaveRanked();return;
    }
    if((scope=="SOLO_RANKED" || (scope=="COMMON" && result["reset"].asBool().unwrapOr(false))) && (result["own"].asBool().unwrapOr(false) || result["reset"].asBool().unwrapOr(false))) {
        auto const profile=result["profile"];
        if(profile.isObject()) {
            m_view.profileTier=profile["displayedTier"].asString().unwrapOr(m_view.profileTier);
            m_view.profileScoreVisible=profile["visibleRankedScore"].isNumber();
            m_view.profileScore=static_cast<int>(profile["visibleRankedScore"].asInt().unwrapOr(0));
            m_view.placementGames=static_cast<int>(profile["placementGames"].asInt().unwrapOr(m_view.placementGames));
        }
        if(result["resetPlayer"].asBool().unwrapOr(false) || result["reset"].asBool().unwrapOr(false)) {
            m_view.profileTier="UNRANKED";m_view.profileScoreVisible=false;m_view.profileScore=0;m_view.placementGames=0;
            m_view.profileWins=m_view.profileLosses=m_view.resultStreak=0;m_view.placementResults.clear();m_view.placement={};
        }
        m_view.promotion={};m_view.demotionWarning=false;
        m_view.placement.required=m_view.profileTier=="UNRANKED" || m_view.placementGames<m_view.placementGamesRequired;
        ++m_view.revision;
    }
    fetchStanding();fetchSurvivalProfile();fetchAchievements();
}

void RankedRuntime::tick() {
    updateSessionHeartbeat();
    if (m_sessionRejected) return;
    flushGameplayControls();
    flushPlacementMapSkip();
    SpectatorLiveRuntime::get().tickTerminal();
    auto const now = std::chrono::steady_clock::now();
    if (now >= m_nextModeConfigAt) fetchModeAvailability();
    if (m_view.survivalConfigReady && now >= m_nextSurvivalProfileAt) fetchSurvivalProfile();
    if ((m_view.stage == RuntimeStage::Ready || m_view.stage == RuntimeStage::Queued) && now >= m_nextStandingAt) fetchStanding();
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

