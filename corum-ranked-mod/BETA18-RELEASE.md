# Corum Ranked v0.5.0-beta.18

## Spectator Phase 2 only

- `CorumRemotePlayer` owns a dedicated basic Cube `PlayerObject`, with a retained world-coordinate CCLayer and hidden native trail/particle companion root. It is not registered as a local player, collision object or scoring participant and does not run native PlayerObject simulation.
- Server-authorized spectator + active Phase 1 dummy + matching live level/context creates one renderer. Existing interpolation supplies x/y/rotation; no packet hides it, dead hides/freezes it, finished holds its exact terminal pose without invoking local death or completion.
- Match/round/map/Death Match sequence/Attempt/generation changes and same-stream reset epochs discard the old visual. Spectator exit, quit, reset and scene exit clean up ownership before vanilla teardown, with idempotent RAII fallback.
- Beta17 already removed local `m_player1` pose reuse. This patch never reinstates it. Local dummy safety remains independent of the renderer: noclip, 99% freeze, completion/transport/evidence and exact-level New Best/progress persistence guards stay intact. User Practice, Placement, real Ranked/Death Match/LAST/FINAL Attempts and Achievement/AP/title/rating authority are unchanged.
- No DB, network protocol, polling rate or camera changes. Client manifest and exact server gate advance to beta18; npm workspace versions retain the existing convention. No Release workflow is dispatched.

## Reference and verification boundary

GlobedGD/globed2 main `05fbd11756b81173075178adb8b4c8eefce40e43` informed separation of RemotePlayer ownership, PlayerObject presentation, object-layer parenting and cleanup. Native factory initialization is used instead of copying Globed's subclass/zero-construction framework. Only the companion cleanup inventory is adapted; full MIT attribution is included in source and the mod package.

Strict domain tests exercise lifecycle/identity/terminal policies. A separate native-double test compiles and executes the complete production renderer translation unit with observable ownership, scheduling, pose and teardown doubles. Existing Phase 1 production-hook and server/domain regressions remain required. This does not certify GD ABI, device rendering or actual two-client spectator UX; automatic native Debug compilation and device QA are distinct checks.

Pre-push local checks passed: 114 rules tests, 127 server tests, 45 Node source/production-native-double tests, 15 remote policy scenarios, 14 preserved local safety scenarios, strict Domain/Debug Bot/Level Preparation (256 launch combinations), workspace typecheck/build and both Apps Script validators. Full target SDK compilation is delegated to automatic Debug CI because this workspace has neither CMake nor a configured Geode SDK; no workflow is manually dispatched.

## Deferred phases

Phase 3: entry/waiting/fallback E2E, remote camera follow, camera triggers/static/offset/zoom/rotation/platformer behavior and two-client MVP. Phase 4: game modes/gravity/mini/dual/death-respawn effects/remote Clear. Phase 5: cosmetics/advanced interpolation/jitter/disconnect polish/cross-platform E2E. Phase 6: performance/network/polling/WebSocket/DB/load work.
