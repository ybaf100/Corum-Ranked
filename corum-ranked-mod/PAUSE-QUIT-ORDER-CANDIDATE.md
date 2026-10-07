# Ranked Pause quit-order candidate (beta41)

This is a candidate on a work branch, not a device-verified iOS fix. Production main, client/server beta41 gates and protocol v2 remain unchanged.

## Evidence

The user log `Geode 2026-10-07 23.48.57.log` loads beta41. Pause opens in match `b47d4992-43f5-439d-bf51-89a2a88c502d` R1 at 23:51:03. MATCH surrender is acknowledged at 23:51:16. In a **different match**, `a893908d-e117-4dcd-b1d1-c1c7fb86fe72` R1, five requests at 23:51:45–47 report `buttonPaused=false`, `nativeFallback=true`, `paused=false`, `canPause=true`, correct scene/UI/button ownership, and native pause-counter increments. There is no subsequent Ranked Pause opened message.

This contradicts an explanation limited to Round 2, touch delivery, or a no-op button selector. It does not identify GD's exact rejecting guard. Corum's opened log is after vanilla customSetup, so its absence alone does not establish whether customSetup was entered.

## Source defect and reproduction

`PauseLayer::watchBudget` resumes native Pause before requesting concession return. Independently scheduled `PlayLayer::spectatorFrame` and postUpdate call `finishConcededGameplayHandoff`, which queues PlayLayer::onQuit directly. Queue draining before the next 50ms Pause watcher permits native Quit while still paused. A source-extracted C++ ordering fixture reproduced:

- watcher first: Resume, then Quit unpaused;
- PlayLayer heartbeat first and queue drains: Quit paused, without prior Resume;
- heartbeat then watcher before queue drains: Resume, then Quit unpaused;
- repeated heartbeat: exactly one Quit, but still without prior Resume.

Native Resume/Quit in this fixture are explicit boundary doubles. These results prove Corum's ordering gap, **not** a stale AppDelegate flag, iOS pauseGame disassembly, or device crash/pause resolution.

## Candidate contract

Track the native Ranked Pause and its exact PlayLayer with Geode WeakRef ownership. The already-existing PlayLayer::onQuit boundary retires that exact Pause using vanilla PauseLayer::onResume before performing gameplay cleanup and native Quit. Retirement clears the surrender modal and exact pause lease. Tracking is released after a successful native Resume, and a strong Ref keeps the removed Pause alive through post-call cleanup. A Cocos retain/release fixture demonstrates why this candidate ownership cleanup needs that hold; this is not attributed to the original device failure. If the owned native Resume returns with m_isPaused still true, abort Quit and emit one bounded error instead of forcing private/global state.

The deferred concession Quit enters through the PlayLayer binding, and quit preparation and the scheduled Resume helper enter through the PauseLayer binding, allowing the installed Resume chain to run normally. Geode binding code generation (`geode-sdk/bindings`, `7f6c2a75`, `codegen/src/SourceGen.cpp`) and Tulip Handler nesting were inspected; direct detour calls are not treated as binding entry in the updated test doubles.

No new native detours, pauseGame replacement, AppDelegate flag writes, manually created PauseLayer, transport/result changes, or version bump. Unowned, ordinary and Placement Pause keep their existing quit path. Spider, persistence, camera, and protocol implementations are unchanged.

`RankedPauseQuitNativeTest.mjs` executes the real Corum helper and retirement callbacks with an explicit native Resume double. It covers exact ownership, stale/detached Pause, idempotence, native refusal, next-match R1, and ordering before gameplay cleanup. Existing safety suites remain required.

## Acceptance still pending

One final candidate artifact may be checked on the user's existing iPad/mod stack: normal Pause, confirmed surrender, next round Pause, and a subsequent match R1 Pause. Multiple isolation builds are not requested. CI and host tests cannot substitute for this device acceptance. Do not merge this branch solely because it builds.

## Independent audio/UI findings

Read-only production config on 2026-10-07 returned fadeInSeconds=1.8 and fadeOutSeconds=1.8. RankedAudioManager applies overlapping DSP ramps with those durations; a short 0.2–0.3s transition is the proposed change. No live config has been modified.

A bottom-right bilingual Settings/설정 button and persistent Ranked volume control remain planned. Native gameplay audio ownership transfers back to GD, so an own-channel slider alone would not cover the entire Ranked gameplay sound scope. Those changes are intentionally not included in this narrow Pause candidate.
