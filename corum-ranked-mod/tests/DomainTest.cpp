#include "../src/domain/AttemptScoring.hpp"
#include "../src/domain/EnvironmentPolicy.hpp"
#include "../src/domain/HudPresentation.hpp"
#include "../src/domain/PlacementStageMerge.hpp"
#include "../src/domain/PlayerIconResolution.hpp"
#include "../src/domain/RankedGameplayPolicy.hpp"
#include "../src/domain/RenderFpsMeter.hpp"
#include "../src/domain/ServerClock.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

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
            {
                .id = "geode.node-ids",
                .displayName = "Node IDs",
                .minVersion = "v1.23.3",
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
            .displayName = "Corum Ranked",
            .version = "v0.1.0",
            .enabled = true,
            .loaded = true,
            .settings = {},
        },
        {
            .id = "syzzi.click_between_frames",
            .displayName = "CBF",
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
            .id = "geode.node-ids",
            .displayName = "Node IDs",
            .version = "v1.23.3",
            .enabled = true,
            .loaded = true,
            .settings = {},
        },
        {
            .id = "geode.loader",
            .displayName = "Geode",
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
        .displayName = "Disabled Test Mod",
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
        .displayName = "Active Test Mod",
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
    assert(blocked.versionViolationModIds == std::vector<std::string>{"syzzi.click_between_frames"});
    assert(!blocked.versionViolations.empty());

    mods = validMods();
    mods.erase(mods.begin() + 1);
    blocked = evaluateEnvironment(mods, policy());
    assert(!blocked.allowed);
    assert(blocked.missingRequiredModIds == std::vector<std::string>{"syzzi.click_between_frames"});
    assert(blocked.cbfIssues == std::vector<std::string>{"CBF_NOT_INSTALLED"});

    assert(isAcceptableServerURL("https://ranked.example.com"));
    assert(isAcceptableServerURL("http://127.0.0.1:3000"));
    assert(!isAcceptableServerURL("http://ranked.example.com"));
    assert(!isAcceptableServerURL("https://ranked.example.com/"));
}

void scoringTests() {
    assert(std::abs(normalizeAttemptProgress(0.019547325102880658) - 0.020) < 0.0000001);
    assert(std::abs(normalizeAttemptProgress(24.505414197332836) - 24.505) < 0.0000001);
    assert(normalizeAttemptProgress(-1.0) == 0.0);
    assert(normalizeAttemptProgress(101.0) == 100.0);
    assert(normalizeAttemptProgress(std::numeric_limits<double>::quiet_NaN()) == 0.0);
    assert(calculateAttemptScore(19.99, false, 20.0) == 0.0);
    assert(calculateAttemptScore(20.0, false, 20.0) == 20.0);
    assert(calculateAttemptScore(49.99, false, 20.0) == 49.0);
    assert(calculateAttemptScore(50.0, false, 20.0) == 75.0);
    assert(calculateAttemptScore(69.99, false, 20.0) == 103.5);
    assert(calculateAttemptScore(70.0, false, 20.0) == 105.0);
    assert(calculateAttemptScore(74.99, false, 20.0) == 111.0);
    assert(calculateAttemptScore(75.0, false, 20.0) == 150.0);
    assert(calculateAttemptScore(99.99, false, 20.0) == 198.0);
    assert(calculateAttemptScore(100.0, true, 20.1) == 250.0);
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
    auto const signedBefore = clock.signedRemainingMillis("1970-01-01T00:20:10.001Z", local);
    assert(signedBefore && *signedBefore == 10'001);
    auto const signedAfter = clock.signedRemainingMillis("1970-01-01T00:20:10.001Z", local + 10'501);
    assert(signedAfter && *signedAfter == -500);
    auto const clampedAfter = clock.remainingMillis("1970-01-01T00:20:10.001Z", local + 10'501);
    assert(clampedAfter && *clampedAfter == 0);
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
    assert(compactLayout.bottomRightX == 558.0f);
    assert(wideLayout.bottomRightX == 844.0f);
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
        .roundNumber = 2,
        .scoreA = 382,
        .scoreB = 417,
        .clearsA = 1,
        .clearsB = 3,
        .qualifyingPercent = 35,
        .remainingMillis = 134'000,
        .signedRemainingMillis = 134'000,
        .renderFps = std::nullopt,
        .spectatorActive = false,
        .spectatorOpponentName = {},
        .spectatorCurrentProgress = std::nullopt,
        .opponentProgressEnabled = true,
        .opponentCurrentProgress = std::nullopt,
        .opponentPractice = false,
    };
    auto hud = presentHud(input);
    assert(hud.fpsText == "FPS : -");
    assert(hud.ownScoreText == "Score : 382");
    assert(hud.opponentScoreText == "Score : 417");
    assert(hud.ownChecks[0] == ClearCheckColor::Green);
    assert(hud.ownChecks[1] == ClearCheckColor::Gray);
    assert(hud.ownChecks[2] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[0] == ClearCheckColor::Green);
    assert(hud.opponentChecks[1] == ClearCheckColor::Green);
    assert(hud.opponentChecks[2] == ClearCheckColor::Green);
    assert(hud.timerText == "02:14");
    assert(hud.roundText == "ROUND 2");
    assert(hud.stateText == "MATCH POINT");
    assert(hud.qualifyingText == "Qualifying : 35%");
    assert(hud.opponentProgressVisible);
    assert(hud.opponentProgressText == "0%");
    assert(!hud.practiceIndicatorVisible);

    input.opponentCurrentProgress = 63;
    input.opponentPractice = true;
    hud = presentHud(input);
    assert(hud.opponentProgressText == "63%");
    assert(hud.practiceIndicatorVisible);
    input.opponentCurrentProgress = 100;
    hud = presentHud(input);
    assert(hud.opponentProgressText == "100%");
    input.opponentCurrentProgress = 0;
    hud = presentHud(input);
    assert(hud.opponentProgressText == "0%");
    input.opponentPractice = false;

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
    assert(hud.ownChecks[2] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[0] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[1] == ClearCheckColor::Gray);
    assert(hud.opponentChecks[2] == ClearCheckColor::Gray);
    assert(!hud.spectatorVisible);

    input.clearsA = 2;
    input.clearsB = 3;
    input.spectatorCurrentProgress.reset();

    input.side = "B";
    input.renderFps = 120.2;
    input.banner = "TIEBREAKER";
    hud = presentHud(input);
    assert(hud.fpsText == "FPS : 120");
    assert(hud.ownScoreText == "Score : 417");
    assert(hud.opponentScoreText == "Score : 382");
    assert(hud.stateText == "TIEBREAKER");

    // ROUND_PLAYING can remain visible until the next poll after 5:00. Derive
    // the fixed FINAL ATTEMPT countdown from the signed normal deadline so the
    // HUD never flashes 00:00 before the decimal ten-second timer appears.
    input.state = "ROUND_PLAYING";
    input.remainingMillis = 0;
    input.signedRemainingMillis = 0;
    hud = presentHud(input);
    assert(hud.stateText == "FINAL ATTEMPT");
    assert(hud.timerText == "10.0");
    assert(hud.windowStateFirst);

    input.signedRemainingMillis = -500;
    hud = presentHud(input);
    assert(hud.timerText == "9.5");

    input.signedRemainingMillis = -10'100;
    hud = presentHud(input);
    assert(hud.timerText == "0.0");

    input.state = "FINAL_ATTEMPT_WINDOW";
    input.remainingMillis = 7'350;
    input.signedRemainingMillis = 7'350;
    hud = presentHud(input);
    assert(hud.stateText == "FINAL ATTEMPT");
    assert(hud.timerText == "7.4");
    assert(hud.windowStateFirst);

    input.state = "ROUND_SETTLING";
    input.remainingMillis.reset();
    input.clearsA = 0;
    input.clearsB = 0;
    hud = presentHud(input);
    assert(hud.stateText == "FINAL ATTEMPT");
    assert(hud.timerText == "0.0");
    assert(hud.windowStateFirst);

    input.state = "LAST_ATTEMPT_WINDOW";
    input.remainingMillis = 6'400;
    input.clearsA = 2;
    input.clearsB = 3;
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
    assert(hud.ownChecks[2] == ClearCheckColor::Green);
    assert(hud.opponentChecks[0] == ClearCheckColor::Green);
    assert(hud.opponentChecks[1] == ClearCheckColor::Green);
    assert(hud.opponentChecks[2] == ClearCheckColor::Gray);

    input.state = "ROUND_SETTLING";
    input.remainingMillis.reset();
    hud = presentHud(input);
    assert(hud.stateText == "LAST ATTEMPT");
    assert(hud.timerText == "0.0");

    input.state = "DEATHMATCH_PLAYING";
    input.deathmatch = true;
    input.deathmatchSequence = 2;
    input.deathmatchAttemptsUsedA = 3;
    input.deathmatchAttemptsUsedB = 2;
    input.deathmatchAttemptsCompletedA = 2;
    input.deathmatchAttemptsCompletedB = 0;
    input.spectatorActive = false;
    hud = presentHud(input);
    assert(hud.stateText == "DEATH MATCH");
    assert(hud.roundText == "DEATH MATCH 2");
    assert(hud.timerText == "3"); // side B: no authoritative End accepted yet
    assert(!hud.qualifyingVisible);
    assert(hud.qualifyingText.empty());
    assert(hud.ownAttemptText == "Attempts : 2/3");
    assert(hud.opponentAttemptText == "Attempts : 3/3");

    input.deathmatchAttemptsCompletedB = 1;
    hud = presentHud(input);
    assert(hud.timerText == "2");
    input.deathmatchAttemptsCompletedB = 2;
    hud = presentHud(input);
    assert(hud.timerText == "1");
    input.deathmatchAttemptsCompletedB = 3;
    hud = presentHud(input);
    assert(hud.timerText == "0");
}

void playerIconTests() {
    PlayerIconResolution missing(0);
    assert(!missing.hasRemoteIdentity());
    assert(!missing.accept(100));
    assert(!missing.resolved());

    PlayerIconResolution delayed(9102);
    assert(delayed.hasRemoteIdentity());
    assert(!delayed.resolved());
    assert(!delayed.accept(9101));
    assert(!delayed.resolved());
    assert(delayed.accept(9102));
    assert(delayed.resolved());
}

void placementStageMergeTests() {
    constexpr auto timeline = placementStageMergeTimeline();
    static_assert(validPlacementStageMergeTimeline(timeline));
    assert(timeline.finalReveal == 0.44f);
    assert(timeline.controlsUnlock == 0.76f);
}

void practiceIsolationTests() {
    assert(rankedScoringEnabled(true, false, false, false));
    assert(!rankedScoringEnabled(true, false, true, true));
    assert(!rankedScoringEnabled(true, false, true, false));

    auto transition = practiceTransition({
        .rankedLevel = true,
        .requestedPractice = true,
        .canStartFreshAttempt = true,
    });
    assert(transition == RankedPracticeTransition::Enter);
    auto actions = practiceActions(transition);
    assert(actions.endNormalAttempt);
    assert(actions.suppressScoring);
    assert(!actions.affectOpponent);

    transition = practiceTransition({
        .rankedLevel = true,
        .rankedPractice = true,
        .requestedPractice = false,
        .canStartFreshAttempt = true,
    });
    assert(transition == RankedPracticeTransition::LeaveForFreshAttempt);
    actions = practiceActions(transition);
    assert(actions.resetForFreshAttempt);
    assert(!actions.endNormalAttempt);
    assert(!actions.affectOpponent);

    transition = practiceTransition({
        .rankedLevel = true,
        .rankedPractice = true,
        .requestedPractice = false,
        .canStartFreshAttempt = false,
    });
    assert(transition == RankedPracticeTransition::LeaveAndExit);
    actions = practiceActions(transition);
    assert(actions.suppressScoring);
    assert(actions.exitLocalGameplay);
    assert(!actions.affectOpponent);

    // Placement uses the same local-only gate: Practice progress and Clear can
    // never become Attempt transport or Top-2 grade evidence.
    assert(!rankedScoringEnabled(true, false, true, true));
}

void placementFinalWindowResetTests() {
    // A Death whose authoritative End was already emitted must remain in the
    // same PlayLayer while the 10-second start window is open. It creates no
    // duplicate End and at most one fresh Start after vanilla reset.
    auto actions = rankedResetActions(true, true, true);
    assert(!actions.endAttempt);
    assert(actions.restartInPlace);
    assert(actions.startFreshAttempt);
    assert(!actions.exitLocalGameplay);

    // A manual reset while alive closes that Attempt once, then follows the same
    // in-place path while the authoritative start gate remains open.
    actions = rankedResetActions(true, false, true);
    assert(actions.endAttempt);
    assert(actions.restartInPlace);
    assert(actions.startFreshAttempt);
    assert(!actions.exitLocalGameplay);

    // Once the authoritative gate closes and no eligible Attempt remains, only
    // the local gameplay exits; there is no extra End or restart.
    actions = rankedResetActions(true, true, false);
    assert(!actions.endAttempt);
    assert(!actions.restartInPlace);
    assert(!actions.startFreshAttempt);
    assert(actions.exitLocalGameplay);
}

void spectatorLevelEntryTests() {
    auto input = SpectatorLevelEntryInput {
        .matched = true,
        .hasCurrentMap = true,
        .hasMatchId = true,
        .spectatorActive = true,
        .state = "ROUND_PLAYING",
    };
    for (auto const state : {
        "ROUND_PLAYING",
        "FINAL_ATTEMPT_WINDOW",
        "LAST_ATTEMPT_WINDOW",
        "ROUND_SETTLING",
        "DEATHMATCH_PLAYING",
    }) {
        input.state = state;
        assert(spectatorLevelEntryAllowed(input));
    }

    input.state = "ROUND_RESULT";
    assert(!spectatorLevelEntryAllowed(input));
    input.state = "MATCH_RESULT";
    assert(!spectatorLevelEntryAllowed(input));
    input.state = "MAP_PLAYING";
    assert(!spectatorLevelEntryAllowed(input));
    input.state = "DEATHMATCH_PLAYING";
    input.placement = true;
    assert(!spectatorLevelEntryAllowed(input));
    input.placement = false;
    input.spectatorActive = false;
    assert(!spectatorLevelEntryAllowed(input));
    input.spectatorActive = true;
    input.hasCurrentMap = false;
    assert(!spectatorLevelEntryAllowed(input));
}

} // namespace

int main() {
    environmentTests();
    scoringTests();
    clockTests();
    fpsTests();
    hudTests();
    playerIconTests();
    placementStageMergeTests();
    practiceIsolationTests();
    placementFinalWindowResetTests();
    spectatorLevelEntryTests();
    std::cout << "Corum Ranked domain tests passed\n";
}
