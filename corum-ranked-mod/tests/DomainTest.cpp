#include "../src/domain/AttemptScoring.hpp"
#include "../src/domain/EnvironmentPolicy.hpp"
#include "../src/domain/HudPresentation.hpp"
#include "../src/domain/RenderFpsMeter.hpp"
#include "../src/domain/ServerClock.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace corum::ranked;

namespace {

EnvironmentPolicy policy() {
    return {
        .allowedMods = {
            {
                .id = "hwanhee1.corum_ranked",
                .displayName = "Corum Ranked",
                .minVersion = "v0.1.0",
                .maxVersion = std::nullopt,
                .required = true,
                .enabled = true,
            },
            {
                .id = "syzzi.click_between_frames",
                .displayName = "CBF",
                .minVersion = "v1.5.0",
                .maxVersion = std::nullopt,
                .required = true,
                .enabled = true,
            },
        },
        .cbfModId = "syzzi.click_between_frames",
        .cbfRequiredSettings = {
            {"click-on-steps", false},
            {"physics-bypass", false},
            {"soft-toggle", false},
        },
    };
}

std::vector<InstalledModSnapshot> validMods() {
    return {
        {
            .id = "hwanhee1.corum_ranked",
            .version = "v0.1.0",
            .enabled = true,
            .loaded = true,
            .settings = {},
        },
        {
            .id = "syzzi.click_between_frames",
            .version = "v1.5.0",
            .enabled = true,
            .loaded = true,
            .settings = {
                {"click-on-steps", false},
                {"physics-bypass", false},
                {"soft-toggle", false},
            },
        },
        {
            .id = "geode.loader",
            .version = "v5.8.2",
            .enabled = true,
            .loaded = true,
            .internal = true,
            .system = true,
            .settings = {},
        },
    };
}

void environmentTests() {
    auto mods = validMods();
    assert(evaluateEnvironment(mods, policy()).allowed);

    mods.push_back({
        .id = "unapproved.disabled_mod",
        .version = "v1.0.0",
        .enabled = false,
        .loaded = false,
        .settings = {},
    });
    auto blocked = evaluateEnvironment(mods, policy());
    assert(blocked.allowed);
    assert(blocked.unauthorizedModIds.empty());

    mods.push_back({
        .id = "unapproved.active_mod",
        .version = "v1.0.0",
        .enabled = true,
        .loaded = true,
        .settings = {},
    });
    blocked = evaluateEnvironment(mods, policy());
    assert(!blocked.allowed);
    assert(blocked.unauthorizedModIds.size() == 1);
    assert(blocked.unauthorizedModIds.front() == "unapproved.active_mod");

    mods = validMods();
    mods[1].settings["soft-toggle"] = true;
    blocked = evaluateEnvironment(mods, policy());
    assert(!blocked.allowed);
    assert(blocked.cbfIssues.front() == "CBF_SETTING_MISMATCH:soft-toggle");

    mods = validMods();
    mods[1].version = "v1.5.0-beta.1";
    blocked = evaluateEnvironment(mods, policy());
    assert(!blocked.allowed);
    assert(!blocked.versionViolations.empty());

    assert(isAcceptableServerURL("https://ranked.example.com"));
    assert(isAcceptableServerURL("http://127.0.0.1:3000"));
    assert(!isAcceptableServerURL("http://ranked.example.com"));
    assert(!isAcceptableServerURL("https://ranked.example.com/"));
}

void scoringTests() {
    assert(calculateAttemptScore(19.99, false, 20.0) == 0.0);
    assert(calculateAttemptScore(20.0, false, 20.0) == 20.0);
    assert(calculateAttemptScore(69.99, false, 20.0) == 69.0);
    assert(calculateAttemptScore(70.0, false, 20.0) == 105.0);
    assert(calculateAttemptScore(79.99, false, 20.0) == 118.5);
    assert(calculateAttemptScore(99.99, false, 20.0) == 148.5);
    assert(calculateAttemptScore(100.0, true, 20.1) == 200.0);
}

void clockTests() {
    auto const epoch = parseIso8601Millis("1970-01-01T00:00:00.000Z");
    assert(epoch && *epoch == 0);
    auto const leap = parseIso8601Millis("2024-02-29T12:34:56.789Z");
    assert(leap);
    assert(!parseIso8601Millis("2023-02-29T00:00:00.000Z"));
    assert(!parseIso8601Millis("2024-01-01 00:00:00Z"));

    ServerClock clock;
    constexpr std::int64_t local = 1'000'000;
    assert(clock.observe("1970-01-01T00:20:00.000Z", local));
    assert(clock.serverNowMillis(local) == 1'200'000);
    auto const remaining = clock.remainingSeconds("1970-01-01T00:20:10.001Z", local);
    assert(remaining && *remaining == 11);
    auto const remainingMillis = clock.remainingMillis("1970-01-01T00:20:10.001Z", local);
    assert(remainingMillis && *remainingMillis == 10'001);
}

void fpsTests() {
    RenderFpsMeter meter;
    assert(!meter.fps());
    for (std::int64_t index = 0; index < 20; ++index) {
        meter.observeFrame(index * 8'333);
    }
    assert(meter.fps());
    assert(*meter.fps() > 119.0 && *meter.fps() < 121.0);
}

void hudTests() {
    auto const compactLayout = layoutHud(568.0f, 320.0f);
    auto const wideLayout = layoutHud(854.0f, 480.0f);
    assert(compactLayout.topLeftX == wideLayout.topLeftX);
    assert(compactLayout.bottomLeftX == wideLayout.bottomLeftX);
    assert(compactLayout.bottomY == wideLayout.bottomY);
    assert(compactLayout.topRightX == 526.0f);
    assert(wideLayout.topRightX == 812.0f);
    assert(compactLayout.topY == 313.0f);
    assert(wideLayout.topY == 473.0f);
    assert(compactLayout.spectatorCenterX == 284.0f);
    assert(wideLayout.spectatorCenterX == 427.0f);

    HudInput input {
        .side = "A",
        .state = "ROUND_PLAYING",
        .banner = "MATCH_POINT",
        .scoreA = 382,
        .scoreB = 417,
        .clearsA = 1,
        .clearsB = 2,
        .qualifyingPercent = 35,
        .remainingMillis = 134'000,
        .renderFps = std::nullopt,
        .spectatorActive = false,
        .spectatorOpponentName = {},
        .spectatorCurrentProgress = std::nullopt,
    };
    auto hud = presentHud(input);
    assert(hud.fpsText == "FPS : -");
    assert(hud.ownScoreText == "Score : 382");
    assert(hud.opponentScoreText == "Score : 417");
    assert(hud.ownChecks[0] == ClearCheckColor::Green);
    assert(hud.ownChecks[1] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[0] == ClearCheckColor::Green);
    assert(hud.opponentChecks[1] == ClearCheckColor::Green);
    assert(hud.timerText == "02:14");
    assert(hud.stateText == "MATCH POINT");
    assert(hud.qualifyingText == "Qualifying : 35%");

    // Ranked score transport must preserve fractional totals produced by the
    // 70-99% x1.5 multiplier.
    input.scoreA = 118.5;
    input.scoreB = 73.0;
    input.qualifyingPercent = 20.1;
    hud = presentHud(input);
    assert(hud.ownScoreText == "Score : 118.5");
    assert(hud.opponentScoreText == "Score : 73");
    assert(hud.qualifyingText == "Qualifying : 20.1%");
    input.scoreA = 382;
    input.scoreB = 417;
    input.qualifyingPercent = 35;

    input.clearsA = 0;
    input.clearsB = 0;
    input.spectatorCurrentProgress = 88;
    hud = presentHud(input);
    assert(hud.ownChecks[0] == ClearCheckColor::Gray);
    assert(hud.ownChecks[1] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[0] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[1] == ClearCheckColor::Gray);
    assert(!hud.spectatorVisible);

    input.clearsA = 1;
    input.clearsB = 2;
    input.spectatorCurrentProgress.reset();

    input.side = "B";
    input.renderFps = 120.2;
    input.banner = "TIEBREAKER";
    hud = presentHud(input);
    assert(hud.fpsText == "FPS : 120");
    assert(hud.ownScoreText == "Score : 417");
    assert(hud.opponentScoreText == "Score : 382");
    assert(hud.stateText == "TIEBREAKER");

    input.state = "FINAL_ATTEMPT_WINDOW";
    input.remainingMillis = 7'350;
    hud = presentHud(input);
    assert(hud.stateText == "FINAL ATTEMPT");
    assert(hud.timerText == "7.4");
    assert(hud.windowStateFirst);

    input.state = "LAST_ATTEMPT_WINDOW";
    input.remainingMillis = 6'400;
    input.spectatorActive = true;
    input.spectatorOpponentName = "PlayerB";
    input.spectatorCurrentProgress = 73;
    hud = presentHud(input);
    assert(hud.stateText == "LAST ATTEMPT");
    assert(hud.timerText == "6.4");
    assert(hud.spectatorVisible);
    assert(hud.spectatorOpponentText == "PlayerB");
    assert(hud.spectatorProgressText == "CURRENT : 73%");
    assert(hud.ownScoreText == "Score : 417");
    assert(hud.opponentScoreText == "Score : 382");
    assert(hud.ownChecks[0] == ClearCheckColor::Green);
    assert(hud.ownChecks[1] == ClearCheckColor::Green);
    assert(hud.opponentChecks[0] == ClearCheckColor::Green);
    assert(hud.opponentChecks[1] == ClearCheckColor::Gray);

    input.state = "DEATHMATCH_PLAYING";
    input.deathmatch = true;
    input.deathmatchAttemptsUsedA = 3;
    input.deathmatchAttemptsUsedB = 2;
    input.spectatorActive = false;
    hud = presentHud(input);
    assert(hud.stateText == "DEATH MATCH");
    assert(hud.timerText == "3 ATTEMPTS");
    assert(hud.ownAttemptText == "Attempts : 2/3"); // side is still B
    assert(hud.opponentAttemptText == "Attempts : 3/3");
}

} // namespace

int main() {
    environmentTests();
    scoringTests();
    clockTests();
    fpsTests();
    hudTests();
    std::cout << "Corum Ranked domain tests passed\n";
}
