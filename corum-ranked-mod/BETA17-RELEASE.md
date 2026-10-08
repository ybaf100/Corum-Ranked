# Corum Ranked v0.5.0-beta.17

## Spectator Phase 1 only

- Preserve the canonical PlayLayer and its local `m_player1` object, but convert it to a hidden non-scoring local dummy only after server spectator permission and the end of real local gameplay.
- Exact-player death suppression provides noclip. The dummy's physics freezes at 99%; separate end-check/end-animation/completion guards prevent Clear. The world and polling HUD remain live.
- Runtime Start/Progress/End/Clear recovery and Practice presence are blocked for the dummy and pending handoff. Pre-existing real Attempt FIFO entries still reconcile normally.
- Explicit `showNewBest`, exact-level `savePercentage` and `saveNewScore` guards prevent local progress/100% records independently of the Practice flag. Scoped persistence ownership covers synchronous vanilla quit/reset calls.
- Remove temporary spectator Practice-frame semantics and local-player remote pose reuse. Preserve user Practice and ordinary Ranked/Placement/Death Match/LAST/FINAL gameplay.
- No DB/API/gameplay telemetry changes. Achievement/AP/title/rating/LP/MMR authority remains unchanged.
- Client manifest and exact server gate: `v0.5.0-beta.17`. All future patches, even minor hotfixes, advance beta. Release remains manual-only.

## Verification boundary

Strict C++ lifecycle/policy and Node source-hook regressions cover authority, pending transition, death, transport, completion, 99%, reconnect, exact-level persistence and normal New Best preservation. An additional strict C++ harness compiles and executes the actual production death/completion/save/physics hook bodies against observable lifecycle doubles, including the final genuine record save before conversion. Existing rules/server integration, domain, Level Preparation, Achievement and Apps Script checks must remain green.

Native multi-platform compilation is checked by the automatic Debug workflow. Source/policy tests and native compilation do not substitute for a real GD device test; collision/reset/dual/platformer/quit/reconnect and local record QA remain device checks.

Pre-push local verification: rules 114 tests, server 126 tests, Node source/production-hook regressions 39 tests, spectator policy/lifecycle 14 scenarios, and existing strict Domain/Debug Bot/Level Preparation (256 launch combinations) checks passed. Workspace typecheck/build, manifest/gate synchronization and both Apps Script validators passed. This workspace has no configured Geode target SDK, so full native compilation is delegated to automatic CI; no workflow was manually dispatched.

## Not implemented (Phase 2)

CorumRemotePlayer, separate PlayerObject rendering, remote x/y/rotation, camera follow, remote game mode, cosmetics, interpolation. Opponent polling is the temporary presentation fallback.
