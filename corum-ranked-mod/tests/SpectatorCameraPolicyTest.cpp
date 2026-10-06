#include "../src/domain/SpectatorCameraPolicy.hpp"
#include "../src/domain/SpectatorLocalSafety.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace corum::ranked;

int main() {
    SpectatorCameraPolicy camera;
    SpectatorRemoteContext context{"match", 1, 0, 123, "attempt", 1, 1};
    SpectatorRemoteFrame frame{context, 0, SpectatorGhostPose{1000, 400, 90, false, false}};
    auto const origin = SpectatorCameraPoint{150, 20};
    auto const coverage = SpectatorCameraPoint{600, 300};
    auto update = [&](bool active, std::optional<SpectatorRemoteFrame> pose) {
        camera.update(active, origin, coverage, context, pose);
    };
    // Genuine FINAL/LAST/DM Attempt, Placement and user Practice cannot own camera.
    for (auto authority : {
        SpectatorLocalAuthority{.rankedScene = true, .serverSpectator = true, .ownAttemptAlive = true},
        SpectatorLocalAuthority{.rankedScene = true, .placement = true, .serverSpectator = true},
        SpectatorLocalAuthority{.rankedScene = true, .userPractice = true, .serverSpectator = true},
        SpectatorLocalAuthority{.rankedScene = true},
    }) {
        SpectatorLocalSafety safety; (void)safety.activate(authority);
        update(safety.active(), frame); assert(!camera.ownsCamera());
    }
    SpectatorLocalSafety safety;
    assert(safety.activate({.rankedScene = true, .serverSpectator = true}));
    update(safety.active(), std::nullopt);
    assert(camera.state() == SpectatorCameraState::Waiting && camera.origin() == origin);
    update(true, frame);
    assert(camera.state() == SpectatorCameraState::Following);
    assert((camera.origin() == origin));
    frame.pose->camera = SpectatorCameraOrigin{520, -100}; update(true, frame);
    assert((camera.origin() == SpectatorCameraPoint{520, -100}));
    frame.pose->camera = SpectatorCameraOrigin{std::numeric_limits<float>::infinity(), 0}; update(true, frame);
    assert((camera.origin() == SpectatorCameraPoint{520, -100}));
    frame.pose->camera.reset(); frame.pose->cameraInvalid=true; update(true,frame);
    assert((camera.origin() == SpectatorCameraPoint{520, -100}));
    frame.pose->cameraInvalid=false;
    frame.pose->x += 300; frame.pose->y += 200; ++frame.sequence; update(true, frame);
    assert((camera.origin() == origin));
    auto const held = camera.origin();
    // Dummy/new vanilla camera position is irrelevant while holding/waiting.
    camera.update(true, {9000, 9000}, coverage, context, std::nullopt);
    assert(camera.origin() == held && camera.state() == SpectatorCameraState::Waiting);
    auto stale = frame; --stale.context.generation;
    update(true, stale); assert(camera.origin() == held);
    stale = frame; --stale.sequence; update(true, stale); assert(camera.origin() == held);
    stale = frame; stale.pose->x = std::numeric_limits<float>::quiet_NaN();
    update(true, stale); assert(camera.origin() == held);
    stale = frame; --stale.context.epoch; update(true, stale); assert(camera.origin() == held);
    // A new attempt/context waits; the previous context can never steer camera.
    ++context.generation; ++context.epoch; context.attemptId = "attempt2";
    update(true, frame); assert(camera.origin() == held);
    frame.context = context; frame.sequence = 0;
    frame.pose = SpectatorGhostPose{2000, 500, 0, false, false}; update(true, frame);
    assert(camera.origin().x == origin.x && camera.state() == SpectatorCameraState::Following);
    frame.pose->dead = true; ++frame.sequence; update(true, frame);
    assert(camera.state() == SpectatorCameraState::Terminal);
    frame.pose->dead = false; frame.pose->finished = true; update(true, frame);
    assert(camera.state() == SpectatorCameraState::Terminal);
    camera.update(true, {999, 999}, {}, context, frame);
    assert(camera.state() == SpectatorCameraState::Waiting && camera.origin().x == origin.x);
    update(false, frame); assert(!camera.ownsCamera());

    SpectatorSceneContext scene{"match", 1, 0, 123};
    for (auto state : {"ROUND_PLAYING", "LAST_ATTEMPT_WINDOW", "FINAL_ATTEMPT_WINDOW", "ROUND_SETTLING", "DEATHMATCH_PLAYING"}) {
        assert(spectatorSceneWatchable(true, scene, scene, state));
    }
    for (auto state : {"ROUND_RESULT", "DEATHMATCH_RESULT", "MATCH_RESULT", "CANCELLED", "MAP_PLAYING"}) {
        assert(!spectatorSceneWatchable(true, scene, scene, state));
    }
    for (int dimension = 0; dimension < 4; ++dimension) {
        auto next = scene;
        if (dimension == 0) next.matchId = "other";
        if (dimension == 1) ++next.roundNumber;
        if (dimension == 2) ++next.deathmatchSequence;
        if (dimension == 3) ++next.levelId;
        assert(!spectatorSceneWatchable(true, scene, next, "ROUND_PLAYING"));
    }
    assert(!spectatorSceneWatchable(false, scene, scene, "ROUND_PLAYING"));
    std::cout << "spectator entry/context/camera policy scenarios passed\n";
}
