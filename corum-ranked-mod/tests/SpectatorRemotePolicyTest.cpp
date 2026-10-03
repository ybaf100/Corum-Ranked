#include "../src/domain/SpectatorRemotePolicy.hpp"
#include "../src/domain/SpectatorLocalSafety.hpp"

#include <cassert>
#include <iostream>
#include <limits>
#include <string_view>

using namespace corum::ranked;
using Life = SpectatorRemoteLifecycle;
using Decision = SpectatorRemotePoseDecision;

namespace {
SpectatorRemoteContext context() {
    return {"match-1", 2, 0, 123, "attempt-b", 1, 1};
}
SpectatorGhostPose pose(float x = 50) { return {x, 80, 45, false, false}; }
int scenarios = 0;
template<class F> void test(std::string_view name, F body) {
    body(); ++scenarios; std::cout << "PASS " << name << '\n';
}
}

int main() {
    test("no presentation without authorized local dummy", [] {
        SpectatorRemotePolicy p;
        assert(p.reconcile(false, context()) == Life::None);
        assert(p.acceptPose(context(), 0, pose()) == Decision::Rejected);
    });
    test("active creates exactly once even across 1000 frames", [] {
        SpectatorRemotePolicy p;
        assert(p.reconcile(true, context()) == Life::Create);
        for (int i = 0; i < 1000; ++i) assert(p.reconcile(true, context()) == Life::None);
    });
    test("waiting context creates hidden without pretending to be a live opponent", [] {
        SpectatorRemotePolicy p; auto c = context(); c.attemptId.clear(); c.generation = 0;
        assert(p.reconcile(true, c) == Life::Create);
        assert(p.state() == SpectatorRemoteState::Waiting && !p.pose());
        assert(p.acceptPose(c, 0, pose()) == Decision::Rejected);
    });
    test("pose carries independent x y rotation", [] {
        SpectatorRemotePolicy p; (void)p.reconcile(true, context());
        assert(p.acceptPose(context(), 0, pose(75)) == Decision::Applied);
        assert(p.pose()->x == 75 && p.pose()->y == 80 && p.pose()->rotation == 45);
    });
    test("same packet sequence can feed multiple existing interpolation samples", [] {
        SpectatorRemotePolicy p; (void)p.reconcile(true, context());
        assert(p.acceptPose(context(), 2, pose(50)) == Decision::Applied);
        assert(p.acceptPose(context(), 2, pose(60)) == Decision::Applied);
        assert(p.pose()->x == 60);
    });
    test("reverse sequence never moves a valid visual", [] {
        SpectatorRemotePolicy p; (void)p.reconcile(true, context());
        (void)p.acceptPose(context(), 3, pose(75));
        assert(p.acceptPose(context(), 2, pose(5)) == Decision::Rejected);
        assert(p.pose()->x == 75);
    });
    test("exit and absent context destroy once", [] {
        SpectatorRemotePolicy p; (void)p.reconcile(true, context());
        assert(p.reconcile(false, context()) == Life::Destroy);
        assert(p.reconcile(false, context()) == Life::None);
        (void)p.reconcile(true, context());
        assert(p.reconcile(true, std::nullopt) == Life::Destroy);
        assert(!p.pose());
    });
    test("every stream identity dimension removes the previous presentation", [] {
        for (int dimension = 0; dimension < 7; ++dimension) {
            SpectatorRemotePolicy p; auto old = context(), next = old;
            (void)p.reconcile(true, old); (void)p.acceptPose(old, 0, pose());
            switch (dimension) {
                case 0: next.matchId = "match-2"; break;
                case 1: ++next.roundNumber; break;
                case 2: ++next.deathmatchSequence; break;
                case 3: ++next.levelId; break;
                case 4: next.attemptId = "next-attempt"; break;
                case 5: ++next.generation; break;
                case 6: ++next.epoch; break;
            }
            assert(p.reconcile(true, next) == Life::Recreate);
            assert(!p.pose() && p.state() == SpectatorRemoteState::Waiting);
            assert(p.acceptPose(old, 10, pose()) == Decision::Rejected);
        }
    });
    test("reset=true on same attempt and generation uses presentation epoch", [] {
        SpectatorRemotePolicy p; auto c = context();
        (void)p.reconcile(true, c); (void)p.acceptPose(c, 10, pose());
        ++c.epoch; assert(p.reconcile(true, c) == Life::Recreate);
        assert(p.acceptPose(c, 0, pose(1)) == Decision::Applied);
    });
    test("dead hides and cannot resume within same stream", [] {
        SpectatorRemotePolicy p; auto c = context();
        (void)p.reconcile(true, c); auto dead = pose(70); dead.dead = true;
        assert(p.acceptPose(c, 0, dead) == Decision::Applied);
        assert(p.state() == SpectatorRemoteState::Dead);
        assert(p.acceptPose(c, 1, pose(100)) == Decision::Frozen);
        assert(p.pose()->x == 70 && p.pose()->dead);
    });
    test("finished holds exact final pose instead of restarting local Clear", [] {
        SpectatorRemotePolicy p; auto c = context();
        (void)p.reconcile(true, c); auto final = pose(100); final.finished = true;
        assert(p.acceptPose(c, 0, final) == Decision::Applied);
        assert(p.state() == SpectatorRemoteState::Finished);
        assert(p.acceptPose(c, 1, pose(20)) == Decision::Frozen);
        assert(p.pose()->x == 100 && p.pose()->finished);
    });
    test("next attempt respawns a dead or finished visual", [] {
        for (bool dead : {false, true}) {
            SpectatorRemotePolicy p; auto c = context(); auto terminal = pose();
            terminal.dead = dead; terminal.finished = !dead;
            (void)p.reconcile(true, c); (void)p.acceptPose(c, 0, terminal);
            c.attemptId = "next"; ++c.epoch;
            assert(p.reconcile(true, c) == Life::Recreate);
            assert(p.acceptPose(c, 0, pose()) == Decision::Applied);
            assert(p.state() == SpectatorRemoteState::Live);
        }
    });
    test("invalid pose cannot mutate the renderer", [] {
        SpectatorRemotePolicy p; (void)p.reconcile(true, context());
        for (int component = 0; component < 4; ++component) {
            auto invalid = pose();
            if (component == 0) invalid.x = std::numeric_limits<float>::quiet_NaN();
            if (component == 1) invalid.y = std::numeric_limits<float>::infinity();
            if (component == 2) invalid.rotation = std::numeric_limits<float>::infinity();
            if (component == 3) invalid.x = 10'000'001;
            assert(p.acceptPose(context(), 0, invalid) == Decision::Rejected);
        }
        assert(!p.pose());
    });
    test("invalid context cannot create or retain stale remote", [] {
        for (int component = 0; component < 7; ++component) {
            SpectatorRemotePolicy p; auto c = context();
            (void)p.reconcile(true, c);
            switch (component) {
                case 0: c.matchId.clear(); break;
                case 1: c.levelId = 0; break;
                case 2: c.roundNumber = 0; break;
                case 3: c.deathmatchSequence = -1; break;
                case 4: c.generation = 0; break;
                case 5: c.epoch = 0; break;
                case 6: c.attemptId.clear(); break;
            }
            assert(p.reconcile(true, c) == Life::Destroy);
        }
    });
    test("Remote presentation never alters Phase 1 safety or grants gameplay authority", [] {
        for (auto authority : {
            SpectatorLocalAuthority{.rankedScene = true, .placement = true, .serverSpectator = true},
            SpectatorLocalAuthority{.rankedScene = true, .userPractice = true, .serverSpectator = true},
            SpectatorLocalAuthority{.rankedScene = true, .serverSpectator = true, .ownAttemptAlive = true},
            SpectatorLocalAuthority{.rankedScene = true},
        }) {
            SpectatorLocalSafety local; SpectatorRemotePolicy remote;
            assert(!local.activate(authority));
            assert(remote.reconcile(local.active(), context()) == Life::None);
        }
        SpectatorLocalSafety local; SpectatorRemotePolicy remote;
        assert(local.activate({.rankedScene = true, .serverSpectator = true}));
        (void)remote.reconcile(local.active(), context());
        (void)remote.acceptPose(context(), 0, {100, 1, 2, true, true});
        assert(local.active() && local.suppressDeath(true) && local.blocksEvidence());
        assert(local.freezeAtCompletionBoundary(true, 100) && local.blockPersistence(true));
        remote.reset(); assert(local.active() && local.blocksEvidence());
    });
    std::cout << scenarios << " spectator remote policy scenarios passed\n";
}
