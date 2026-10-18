# Survival root causes — beta.81

Baseline: authoritative GitHub `main` **bb196bec0410892cd91d265cc98e008417d236dd**. Branch: `fix/survival-root-causes-beta81`. Previous beta.78 work and its uncommitted files are preserved in their original worktree. This change starts from main, including its readiness, player information, mod policy and native pause restoration changes.

## Evidence and failure mechanisms

The supplied `Geode 2026-10-18 14.46.29.log` runs beta.80 on iOS, GD 2.2081 and Geode 5.11.2. It proves input reaches the Pause observer: `valid=true`, with both actual menu items `enabled=false`. At world point `(101.59827,147.43843)`, the surrender item's local point `(61.098267,27.43843)` lies inside its `96x36` rectangle. A disabled action is deliberately rejected by hit testing. This is not evidence of a missing touch event.

1. **Phantom Debug Force Clear request.** Real SDK `matjson::Value()` constructs an empty **object**, not null. `m_forceClearBody` therefore starts as `{}`, and `forceClearPending()` using `isObject()` is true even before submission. Every `watchBudget()` first enables surrender from `canSurrender()`, then its Debug pending guard disables it; Force Clear also remains disabled. `surrender()` returns at the same pending guard, including after a waiting-room confirmation. Resetting the body with `{}` never leaves that state. `pumpForceClear()` repeatedly treats this empty body's round as stale and emits synchronization errors. Existing JSON doubles used a null default or a constant pending result, masking this actual SDK behavior.
2. **Deferred confirmation cancellation and ownership.** The former popup moved its callback out before the queued completion, so cancellation could not cancel that queued callback. It also relied on `isRunning()` instead of retained parent/current scene ownership. Production-method tests reproduce a valid paused completion being dropped, a queued confirmation surviving cancellation, and an action escaping to a different scene. The common `finish accepted=true` device message alone cannot identify an action or prove a FORFEIT request.
3. **Paused ACK return and native lifetime.** Both the runtime ACK return and the deferred PlayLayer exit rejected `!isRunning()`. A paused native PlayLayer can be stopped yet remain the exact current scene owner. Separately, native return discarded weak owners before popping the scene. Geode's WeakRefPool has a shared native retain that is reaped on a subsequent owner probe; probing while the scene still owns its child cannot retire that child. The beta.80 log's later `retiring orphan PlayLayer` fallback confirms an old owner survived. Arbitrarily detaching any stopped foreign PlayLayer is not an ownership-safe fix.
4. **Reveal identity and changing reel length.** The log transitions from level `0` at 14:47:05 to `73153577` at 14:47:07, then logs only 2750 ms at 14:47:10. Production `present()` compared the full context, including level ID and scene generation, and looked up a registry keyed by level ID. It consequently reset both visuals and the start time when the server resolved the map. Preview arrivals also resized the modulo list during rotation, visibly moving existing cards.

The beta.77–beta.80 input-priority and orphan-detach changes did not address the real JSON request sentinel or stable reveal identity. Their enabled-item fixtures did not reproduce the supplied disabled-item state. Passing those tests did not establish device input, ACK navigation or native destruction. Earlier server deployment skips are a separate issue documented by `AGENTS.md`; this release uses a normal main commit and checks the actual Render deployment.

## Production changes

- `SurvivalRuntime.{hpp,cpp}` represents request absence with `std::optional<matjson::Value>`, uses explicit reset/emplace, and cancels stale callbacks with the same session/match/round/generation/request identity checks. In-progress surrender and Force Clear exclude each other. Nullable certificate/result/completed-round fields start as JSON null. Server authentication, Debug/online/round validation, event identity, retries and 100% evidence stay authoritative.
- `RankedPauseLayer.cpp` remains the sole writer of action enabled states through `watchBudget()`. No action is force-enabled and no touch priority is lowered. The existing observer owns enabled action rectangles, swallows that Begin/End pair, and invokes once through its pending/epoch gate; native menu fallback reaches the same confirmation guard. Coordinate and centered-anchor reproduction now uses the exact device point.
- `RankedConfirmPopup.hpp` retains a cancellable callback until queued completion commits against its original parent/current scene, no pending scene transition, and no exited owner. Closing or retiring the owner cancels submission. The callback executes once after the popup closes, independent of a valid paused node's running flag.
- `SurvivalRuntime` and `PlayLayer.cpp` accept a paused exact scene owner for ACK-driven exit. They still queue to the main thread, use WeakRefs, validate the current PlayLayer and scene, respect native Pause Resume/refusal and completion cleanup, and suppress duplicate native quit while its return transaction is pending.
- `RankedRoundPreparation.{hpp,cpp}` retains its exact departing scene and PlayLayer until **after** the native pop. The heartbeat probes scene first, then PlayLayer, and permits another native setup only after the previous owner retires. It never detaches a foreign object. Existing exact-origin `m_gamePaused` restoration, cursor recovery, texture-cache retention and normal native cleanup remain in place.
- `SurvivalMapSelection.hpp` separates logical reveal identity (`match + round + mapGeneration`) from provider/scene request identity. The same reveal updates resolved metadata without restarting its clock. `SurvivalMapSelectionLayer.cpp` keeps the original registry start, consumed sounds, final state and mode across recreated coordinators. An eight-slot ordered reel fills placeholders without changing modulo or moving existing thumbnails. A truly new round/map generation gets a new reveal. The final selected texture still comes from the server-selected ID. A missing texture may use the existing one-way numeric fallback without restarting elapsed time.
- `SurvivalSelectionLayout.hpp` enlarges the carousel and cards with viewport width/height limits. Phone, 4:3 and wide policy tests check fit and increased size. This does not certify rendered device typography.
- Version metadata and required-client admission are synchronized to beta.81. Existing Debug/Release workflows and immutable version paths are retained. The Debug regression job downloads pinned real Geode JSON and Result source dependencies so the sentinel test cannot silently use a null-default JSON double.

The Solo 30-second Pause policy, LP/MMR calculations, division boundaries and result colors, Survival score/cutoff/bot rules, five-second ready-room delay, native countdown, FREE PLAY, HUD, HAC/Jukebox policy, Live Spectator and vanilla records/rewards are unchanged. Force Clear never calls native `levelComplete()`.

## Reproduction and regression coverage

Behavioral failures were run against the supplied baseline source before the corresponding replacement was accepted. Failures are assertions after successful compilation, not missing-symbol or compiler failures:

| Production regression | beta.80 | beta.81 |
| --- | --- | --- |
| Real SDK idle request must not be pending | assertion fails | passes |
| Actual `watchBudget()` must enable both allowed Debug actions | assertion fails | passes |
| Confirm completion with valid paused owner | callback-count assertion fails | passes |
| Cancel queued confirmation | callback-count assertion fails | passes |
| Confirm after scene replacement | callback-count assertion fails | passes |
| FORFEIT ACK while owning PlayLayer is stopped | immediate-return assertion fails | passes |
| Actual `present()`/registry path: level 0 to selected ID | reset/progress assertion fails | passes |

New tests: `SurvivalJsonSdkNativeTest.mjs`, `ConfirmCompletionNativeTest.mjs`, `NativeReturnOwnershipNativeTest.mjs`, `SurvivalRevealContinuityNativeTest.mjs`. Existing Pause, dispatcher, preparation, Free Play and transition tests exercise actual production methods with controlled network/native boundaries. The ownership test models the SDK's shared WeakRef native retain and verifies native destruction without manually clearing the global PlayLayer or detaching it. The reveal test compiles the actual production context/registry transaction, not only the timeline helper.

Source overrides allow replay against baseline files: `CORUM_RUNTIME_HEADER`, `CORUM_RUNTIME_SOURCE`, `CORUM_PAUSE_SOURCE`, `CORUM_CONFIRM_SOURCE`, `CORUM_REVEAL_LAYER_SOURCE`. Extract those files from the baseline SHA into a temporary directory; run the corresponding named tests with those paths, then run without overrides. Real dependency paths use `CORUM_MATJSON_SOURCE` and `CORUM_RESULT_INCLUDE`, prepared in the Debug workflow.

Verified local results before publication:

- Shared rules: **247 passed**. Server: **543 passed in 52 files**.
- Separate real PostgreSQL 16 Survival HTTP/concurrency/recovery run: **47 passed**, including waiting-room surrender, multiple surrenderers with continuing peers, exact-once settlement, certified re-entry and version/authentication rejection.
- Standalone strict Native C++ policy executables: **10 passed**.
- Publication/build/asset Python tests: **16 passed**. GD relay and Survival configuration: **17 passed**. Ranked and legacy record-sheet validators passed.
- All Ranked workspace type checks and server/rules builds passed.
- Final Native suite and platform matrix outcomes are recorded in the delivery report after completion. Local cross-compilation uses NDK r29, Geode SDK 5.8.2 and GD 2.2081 bindings; installed iOS log evidence uses Geode 5.11.2. Windows/iOS toolchains are unavailable locally. Official existing CI jobs provide the separate Windows/iOS/Android32/Android64 Debug ON/OFF package results.

## Device validation and diagnostic chain

**No physical iPadOS test was available. No unidentified native crash is claimed fixed.** CI compilation, behavioral boundary tests, package publication and Render deployment are reported separately from actual device behavior.

In a new beta.81 device log, trace this exact sequence:

1. `[SurvivalPauseState]` records the first four watcher calls, allowed/enabled actions and real Force Clear pending state. `[SurvivalPauseInput]` already records received point, transformed target and enabled/hit decisions. The recorded in-bounds point must now show `enabled=true` and `hit=true` in an authorized idle Debug round.
2. `[RankedConfirmInput]` records Begin/End, then queued `completion` with attachment/scene/callback flags. `[SurvivalAction] source=pause action=...` or `[SurvivalSurrender] board confirm` identifies the actual source and captured match/round/generation.
3. `[SurvivalSurrender] submit` identifies a stable FORFEIT event, followed by HTTP status and `ACK eliminated=true accepted=true`. Force Clear has separate request-ID submission/HTTP logs. A generic popup finish is not request evidence.
4. `[SurvivalExit]` distinguishes queued exit from stopped/retired/uninitialized/foreign/transitional owners. Existing native return logs must show only the owned scene pop, followed by `exact departed scene and PlayLayer retired; entry available` before the next native setup. There must be no orphan-detach fallback.
5. `[SurvivalReveal] mode` starts once per logical reveal; completion/sound times continue from its first start despite ID resolution, polls or recreated UI. A new round/map generation starts again normally.

Still requiring actual device validation: iPad finger hit delivery and popup placement, Force Clear BEST 100% and FREE PLAY after real server ACK, paused surrender/navigation/music restoration, repeated leave/re-entry and OS background/resume, ImagePlus texture upload and final selected thumbnail, adapted carousel clipping/typography, and any crash requiring a native crash stack. Staging an action, receiving a popup log or passing CI is not a substitute for these checks.
