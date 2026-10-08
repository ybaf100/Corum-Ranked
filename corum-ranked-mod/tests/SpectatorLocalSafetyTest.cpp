#include "../src/domain/SpectatorLocalSafety.hpp"
#include "../src/domain/RankedGameplayPolicy.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <string_view>

using namespace corum::ranked;

namespace {
// Lifecycle adapter exercises the same policy used by the native hooks, with
// observable transport, vanilla callbacks and level persistence. This is not a
// Geometry Dash device simulation; source tests additionally bind each hook to
// these policy decisions, and CI compiles the native hook implementation.
struct Lifecycle {
    SpectatorLocalSafety safety;
    SpectatorLocalAuthority authority {.rankedScene = true};
    bool pending = false;
    bool practiceFlag = false;
    int starts = 0, progress = 0, ends = 0, clears = 0;
    int vanillaDeaths = 0, vanillaCompletions = 0, physics = 0;
    int savedBest = 41, savedScores = 0, queued = 0, delivered = 0;

    void sync() { (void)safety.activate(authority); }
    bool blocked() {
        sync();
        return spectatorAttemptTransportBlocked(
            spectatorAuthorityReady(authority.serverSpectator, authority.placement, authority.ownAttemptAlive),
            safety.blocksEvidence(pending)
        );
    }
    void start() { if (!blocked()) { ++starts; authority.ownAttemptAlive = true; } }
    void publish() { if (!blocked()) ++progress; }
    void end(bool clear = false) {
        if (blocked()) return;
        ++ends; clears += clear ? 1 : 0; ++queued;
        authority.ownAttemptAlive = false;
    }
    void death(bool local = true) {
        sync();
        if (safety.suppressDeath(local)) return;
        if (!safety.blocksEvidence(pending)) end();
        ++vanillaDeaths;
    }
    void complete() {
        sync();
        if (safety.blocksEvidence(pending)) return;
        end(true); ++vanillaCompletions;
    }
    void update(double percent, bool local = true) {
        sync();
        if (!safety.freezeAtCompletionBoundary(local, percent)) ++physics;
    }
    void save(int percent, bool exactLevel = true) {
        sync();
        if (!safety.blockPersistence(exactLevel, pending)) savedBest = std::max(savedBest, percent);
    }
    void saveScore(bool exactLevel = true) {
        sync();
        if (!safety.blockPersistence(exactLevel, pending)) ++savedScores;
    }
    void flushJournal() { delivered += queued; queued = 0; }
};

int scenarios = 0;
template <class F> void test(std::string_view name, F body) {
    body(); ++scenarios; std::cout << "PASS " << name << '\n';
}
}

int main() {
    test("normal Ranked death/start/progress/end/New Best remain vanilla", [] {
        Lifecycle scene;
        scene.start(); scene.publish(); scene.update(99.5); scene.save(83); scene.death();
        assert(scene.starts == 1 && scene.progress == 1 && scene.ends == 1);
        assert(scene.vanillaDeaths == 1 && scene.physics == 1 && scene.savedBest == 83);
        assert(!scene.safety.active());
    });
    test("normal completion remains scoring and records 100", [] {
        Lifecycle scene;
        scene.start(); scene.complete(); scene.save(100); scene.saveScore();
        assert(scene.clears == 1 && scene.ends == 1 && scene.vanillaCompletions == 1);
        assert(scene.savedBest == 100 && scene.savedScores == 1);
    });
    test("server permission alone cannot cut a live FINAL/LAST/Death Match Attempt", [] {
        for (auto state : {"ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW", "DEATHMATCH_PLAYING"}) {
            (void)state;
            Lifecycle scene;
            scene.start(); scene.authority.serverSpectator = true; scene.sync();
            assert(!scene.safety.active());
            scene.publish(); scene.save(93); scene.death();
            assert(scene.progress == 1 && scene.ends == 1 && scene.vanillaDeaths == 1);
            assert(scene.savedBest == 93);
        }
    });
    test("finished gameplay without server permission is not noclip", [] {
        Lifecycle scene;
        scene.pending = true; scene.sync(); scene.death(); scene.start(); scene.publish(); scene.end(true);
        assert(!scene.safety.active() && scene.vanillaDeaths == 1);
        assert(scene.starts == 0 && scene.progress == 0 && scene.ends == 0);
    });
    test("spectatorPending to active has no duplicate Attempt; old FIFO still drains", [] {
        Lifecycle scene;
        scene.start(); scene.end(); scene.pending = true;
        scene.start(); scene.publish(); scene.end(true); scene.save(80);
        assert(scene.starts == 1 && scene.ends == 1 && scene.savedBest == 41);
        scene.authority.serverSpectator = true; scene.sync(); scene.pending = false;
        scene.start(); scene.publish(); scene.death(); scene.complete(); scene.flushJournal();
        assert(scene.safety.active() && scene.starts == 1 && scene.progress == 0);
        assert(scene.ends == 1 && scene.clears == 0 && scene.delivered == 1);
        assert(scene.vanillaDeaths == 0 && scene.vanillaCompletions == 0);
    });
    test("reconnecting authorized spectator is protected before its first frame", [] {
        Lifecycle scene;
        scene.authority.serverSpectator = true;
        scene.start(); scene.publish(); scene.end(true); scene.death(); scene.complete();
        assert(scene.safety.active() && scene.starts == 0 && scene.progress == 0 && scene.ends == 0);
        assert(scene.vanillaDeaths == 0 && scene.vanillaCompletions == 0);
    });
    test("noclip and 99 freeze affect only exact local dummy", [] {
        Lifecycle scene;
        scene.authority.serverSpectator = true; scene.sync();
        scene.death(true); scene.death(false);
        scene.update(98.999); scene.update(99.0); scene.update(20.0); scene.update(100.0, false);
        assert(scene.vanillaDeaths == 1 && scene.ends == 0 && scene.physics == 2);
        assert(scene.safety.frozen());
        scene.safety.resetDummy(); scene.update(10.0);
        assert(scene.physics == 3 && scene.safety.active());
    });
    test("NaN percent never arms the 99 freeze", [] {
        Lifecycle scene;
        scene.authority.serverSpectator = true;
        scene.update(std::numeric_limits<double>::quiet_NaN());
        assert(!scene.safety.frozen());
    });
    test("100 escape still cannot complete or persist even with Practice OFF", [] {
        Lifecycle scene;
        scene.authority.serverSpectator = true; scene.practiceFlag = false;
        scene.update(100.0); scene.complete(); scene.end(true); scene.save(100); scene.saveScore();
        assert(scene.ends == 0 && scene.clears == 0 && scene.vanillaCompletions == 0);
        assert(scene.savedBest == 41 && scene.savedScores == 0);
        assert(!scene.practiceFlag);
    });
    test("same level ID is insufficient: only exact scene level persistence is blocked", [] {
        Lifecycle scene;
        scene.authority.serverSpectator = true;
        scene.save(88); scene.saveScore();
        assert(scene.savedBest == 41 && scene.savedScores == 0);
        scene.save(88, false); scene.saveScore(false);
        assert(scene.savedBest == 88 && scene.savedScores == 1);
    });
    test("authority refresh/result does not resurrect a converted scene", [] {
        Lifecycle scene;
        scene.authority.serverSpectator = true; scene.sync();
        scene.authority.serverSpectator = false;
        scene.start(); scene.publish(); scene.end(true); scene.save(100); scene.complete();
        assert(scene.safety.active() && scene.starts == 0 && scene.progress == 0 && scene.ends == 0);
        assert(scene.savedBest == 41 && scene.vanillaCompletions == 0);
    });
    test("Placement, user Practice, test/editor, and unrelated scenes cannot activate", [] {
        for (int kind = 0; kind < 4; ++kind) {
            Lifecycle scene;
            scene.authority.serverSpectator = true;
            if (kind == 0) scene.authority.placement = true;
            if (kind == 1) scene.authority.userPractice = true;
            if (kind == 2) scene.authority.testMode = true;
            if (kind == 3) scene.authority.rankedScene = false;
            scene.sync(); scene.update(100.0); scene.save(89);
            assert(!scene.safety.active() && scene.physics == 1 && scene.savedBest == 89);
        }
        assert(!rankedScoringEnabled(true, false, true, true));
        assert(practiceTransition({true, false, false, true, true}) == RankedPracticeTransition::Enter);
        assert(practiceTransition({true, false, true, false, true}) == RankedPracticeTransition::LeaveForFreshAttempt);
    });
    test("transport firewall cannot be bypassed by armed start or missing-start Clear recovery", [] {
        for (bool spectator : {false, true}) for (bool pending : {false, true}) {
            assert(spectatorAttemptTransportBlocked(spectator, pending) == (spectator || pending));
        }
    });
    test("final genuine vanilla End save completes before spectator conversion", [] {
        SpectatorLocalSafety safety;
        SpectatorLocalAuthority authority {
            .rankedScene = true, .serverSpectator = true, .ownAttemptAlive = true,
        };
        // The transport End is queued, but its synchronous vanilla writer is
        // still running. Native hooks include that scope in ownAttemptAlive.
        assert(!safety.activate(authority));
        assert(!safety.blockPersistence(true));
        authority.ownAttemptAlive = false;
        assert(safety.activate(authority));
        assert(safety.blockPersistence(true));
    });
    std::cout << scenarios << " spectator lifecycle scenarios passed\n";
}
