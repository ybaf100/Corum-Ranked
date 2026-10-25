# beta.102 log-based entry, completion and shutdown fixes

Starting authoritative main: `eca2a0c157706477887b48761fbb6e037f4a148a`.
Work branch: `fix/log-entry-shutdown-beta102-20261025`.
The attached evidence was inspected locally; raw logs, screenshots and account
information are not committed. No achievement epoch, AP/LP/MMR, schema, rules,
server round timestamps or production mode settings are changed.

## Findings by requested item

| Item | Classification | Evidence and change | Remaining verification |
| --- | --- | --- | --- |
| 1. Board before first entry | Cause confirmed; code fixed | Board forced manual mode while the selected panel hid the entry button until a previous launch. Returning before PLAYING now restores first-entry automation; returning after PLAYING exposes manual entry. A board visit invalidates an already queued automatic launch, even if Back is pressed before its callback. | Device navigation and touch timing. |
| 2. Live-round entry | Cause confirmed; code fixed | First-entry panel hold also ran in PLAYING. Only pre-start entry retains that five-second hold. PLAYING shows RE-ENTER immediately, disabled until preparation/authority is ready; no automatic launch from a newly observed PLAYING screen. The existing shared startsAt countdown is already expired for live entry, so native release still waits for actual ENTER approval/canPlay without a new GO clock. | Real official/observer/local replay entry with network delay and expiry. |
| 3. Solo finished handoff | Specific completion-only gap confirmed; code fixed; other causes unconfirmed | `attemptEndReported` is a local journal/scene flag, not an End ACK or universal server spectator permission. PlayLayer has an ACK/authority-gated percentage fallback, but EndLevelLayer previously waited only for spectator permission or result states. A paused completion with ROUND_SETTLING and no immediate spectator permission could remain while the opponent played. Its heartbeat now exits after no pending Start/End, no open/armed attempt authority and no permission for another start. Live spectator handoff remains first priority. | Reproduce the user's full sequence on device. Stale arm authority is not cleared speculatively. A new attempt after 100% remains valid when the Solo rules allow it. |
| 4. beta.101 shutdown | Static lifetime hazard confirmed; code fixed; exact native crash reproduction pending | `2026-10-25_10-24-45.6846974.log` is in DLL/process onexit, with Corum WeakRef cleanup → Geode metadata → another WeakRef → PlayLayer destructor. Two global completion WeakRefs and the global Pause optional had engine-dependent static destruction. These bounded slots now have explicit process lifetime, consistent with existing scene runtimes. Completion onExit releases only its own controllers while retaining the callback owner; existing Pause Resume/Quit ownership cleanup remains intact. This prevents the DLL exit table from releasing these objects after Cocos shutdown. | Windows exit with Pause/completion open, repeated rounds and other installed mods. No blame assigned to Practice Checkpoint Permanence. |
| 5a. Repeating action crash | Unconfirmed | The beta.94 CCActionEase/CCSequence/CCRepeatForever stack has no Corum frame identifying an action owner. Inspected badge/transition/podium/action sites construct separate action trees per target; achievement flow is a different custom interval without the implicated Ease/Sequence chain. No confirmed action sharing/free defect was found; effects were not disabled or rewritten. | Symbolized live crash or a reproducible action/node lifetime trace. |
| 5b. 878ms frame gap | Observed; cause allocation unconfirmed | beta.101 logs bracket native PlayLayer creation around the gap; the same interval also includes native/mod initialization and later frame work. Add separate geometry validation/native scene timing and slow runtime-preparation/coordinator-presentation diagnostics. No unsupported attribution to audio, achievements or a specific mod, and no claimed FPS improvement. | Device timings from the new build. |
| 6. Earlier fixes | Existing fixes confirmed and preserved | No safetyRetired block is restored; retired=true queued returns and app-pause recovery remain. Existing native Pause, End FIFO, surrender, completed-map replay and scene-return tests remain. A retried End HTTP 500 does not establish a server root cause fix; no speculative backend fix is made. The two October 17 crashes without Corum installed are excluded. | New evidence only if these regress again. |
| 7. Fancy reveal setting | Implemented | Keep saved key/value `survival-selection-motion`; rename to 화려한 레벨 선택 모션 / Fancy Level Selection Animation. OFF uses existing numeric reels instead of skipping. Reel mode and selected-map thumbnail rendering are independent; numeric mode paints the selected image with the existing cached ThumbnailCard. Without thumbnail support both settings use numbers. Existing explicit thumbnail preference remains respected. | Four combinations on real screens, particularly numeric reveal plus selected thumbnail. |

## Shutdown symbol limits

The published beta.101 Release package contains no PDB/map/dSYM. The release
workflow's retained artifact is the combined `.geode` plus descriptor, not the
Windows linker symbols. Exact Corum RVAs `0x51a5` / `0x847ae` cannot be assigned
to a particular Fields destructor without matching symbols. The confirmed C++
static destruction hazard is distinguished from proving every frame of this
native crash. Process exit is not represented as a newly diagnosed in-game crash.
The three process-lived slots are one bounded allocation each, not per-round
leaks; normal scene ownership releases the handles. Existing runtime/caches use
process lifetime for the same native shutdown constraint.

## Entry and competition boundaries

The server's canPlay/ownEntered/session/context/expiry, eliminated/surrender and
TIEBREAK attempt limits still guard official gameplay. Scene ENTER is not a new
local countdown: it still requires the server response before physics release.
Observer and cleared-map replay retain their existing separate local Free Play
ownership and suppression of official Attempt/progress/result transport. Pre-start
ENTERING/COUNTDOWN preparation and the original authoritative synchronized start
remain. UI/reveal preferences never alter those permissions or timestamps.

Server `buildPublicState` final-window spectator eligibility still requires no
viewer progress and opponent progress during ROUND_SETTLING. That is an existing
server policy, not proof that a locally finished scene already has permission.
The overlay fallback fixes the completion-only wait without weakening that policy.

## Reproduction and tests

The new executable native fixtures run production panel wait/button, board/back,
queued launch and thumbnail-renderer code. They cover pre-start vs PLAYING first
entry, preparing/ready live buttons, queued automatic work cancelled by a board
visit, real selected-image rendering/cache reuse with numeric reels, all four
support/fancy combinations and 100 simulated overlay exit cycles followed by
process shutdown. The existing actual completion callback fixture now tests
pending ACK, remaining start/open authority, valid next start and ACKed fallback.
These Cocos/network boundary fixtures are not physical device verification.
Existing official-vs-observer/replay telemetry, native return, Pause and countdown
regressions remain part of the complete native suite.

## One-push publication and activation

The source push uses `[deploy-lightsail] [publish-r2]`, starting Release, Debug,
server validation and Docker build together on the first push. The Lightsail job
waits at a CI-only barrier before upload/activation until both exact immutable
client channels verify: full SHA-256/bytes, embedded ID/version/channel and all
four native payload names. Corruption fails immediately; a missing channel waits
within the bounded job timeout and then fails closed. The barrier has no DB
queries/timers in the running game server. Pinned host/known_hosts, strict SSH,
concurrency, Singapore-only writer and rollback are unchanged.

Local checks and actual source CI/package/deployment outcomes are appended after
execution; a successful source push alone is not a completed release.

## Local verification before the first source push

- Full native boundary suite: **297/297 passed, zero skips**. Final thumbnail
  renderer/board action additions and updated source safety checks: **9/9 passed**;
  focused production board/queued entry/completion checks: **20/20 passed**.
  The initial run's obsolete global-WeakRef source assertion was updated to the
  new process-lifetime contract; executable shutdown/normal cleanup checks pass.
- Rules **288/288**; server **626 passed / one optional local PostgreSQL pool
  test skipped**; workspace typecheck and server/rules builds passed. The source
  CI will run its isolated PostgreSQL 16 online concurrency/recovery suite.
- Localization **679 UI keys / 48 achievements** and generated font coverage
  passed. Release build policy **2/2**, publication policy **9/9**, new CI client
  publication barrier **4/4** passed. No warnings were suppressed to pass fixes.
- A fresh `git fetch origin main` succeeded and matched the starting main SHA;
  no concurrent main changes were discarded.
- Read-only Production baseline at `2026-10-25T13:55:58.614Z`: epoch still
  `2026-10-24T04:59:47.102Z`; 0020/0029/0030 and native_death present; two current
  accounts, two Solo/four Survival matches, 86/5381 Attempts; 24 unlocks/19 AP
  ledger rows, both AP totals 335, invalid LP ledger rows zero. Current complete
  Solo/Survival profile digests: `e3336a2f0caa139d3ce32be696d7ba6a` /
  `32ba2a9f12eb2e80f83ef0f9f2730150`. These are the user's current records,
  not the older beta.101 observation; no old snapshot is restored. Survival is
  still ON/64 with generation `survival-b0676719:64p`.

## Actual first-build publication and production results

Source/main deployment commit: `f264710dfd7446149e30a93b013fca11112cc0da`.
All four source workflows succeeded on **attempt one**, without an intervening
source fix, second activation push or immutable package overwrite:

- [Server CI 40200044090](https://github.com/ybaf100/Corum-Ranked/actions/runs/40200044090):
  rules 288/288; server 626 passed / one optional local pool test skipped;
  PostgreSQL 16 online/concurrency/recovery 52/52; typecheck/build, relay and
  Apps Script checks; 1050 adaptive NORMAL matches and ten-hour virtual soak.
- [Release 40200044089](https://github.com/ybaf100/Corum-Ranked/actions/runs/40200044089):
  Windows/iOS/Android32/Android64 build, combine/package validation and R2 publish.
- [Debug 40200044140](https://github.com/ybaf100/Corum-Ranked/actions/runs/40200044140):
  final native suite **299/299, zero skips**, policy/shader checks, all four
  platform Debug ON and OFF builds, package and R2 publish. Unchanged optional
  external images skipped by existing policy.
- [Lightsail 40200044088](https://github.com/ybaf100/Corum-Ranked/actions/runs/40200044088):
  pinned SSH, Docker image build, four barrier unit tests, both full package
  verifications, upload and rollback-capable activation all succeeded. Its log
  verified Release at 14:03:36Z and Debug at 14:05:28Z, before reporting the
  actual deployed source at 14:06:10Z. This is completed server activation,
  separately from completed client publication.

Both public `.geode` files were also downloaded completely from this workspace,
independently of CI, verifying descriptor hashes/bytes, embedded ID/version/channel
and all four platform binaries:

| Channel | Bytes | SHA-256 | Public descriptor |
| --- | ---: | --- | --- |
| Release | 8,949,475 | `13ef70e08d7e7d644e3501ddc683701f119f7afcc7448736842f47d52310358e` | [release.json](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.102/release.json) |
| Debug | 9,531,104 | `2c44bdae618947d7c4cd41b5656d7f2cc7a9fb7b231ab5281d1985113fd2d0ba` | [debug.json](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.102/debug.json) |

At `2026-10-25T14:06:45Z`, Singapore `/health` and `/ready` returned HTTP 200;
ready/publicDataReady/rankedReady/databaseReady/databaseConnected/schemaReady
were all true, missingSchema empty. Required client version and both update
channels were beta.102 with the exact hashes/sizes above. Current operator config
reported Solo ON and Survival ON/64. Unauthenticated history detail returned 401.

The post-deploy read-only comparison at `2026-10-25T14:06:46.549Z` matched **every
baseline preservation field** except the observation timestamp, including both
complete profile digests, epoch, migrations/native_death, all account/match/Attempt
counts, 24 unlocks/19 AP rows, AP totals 335 and zero invalid LP ledger rows.
No Production reset, data repair, manual migration or Neon setting change occurred.

Physical iPhone/iPad/Android/Windows touch, live official/observer/replay play,
Solo final-attempt timing, native shutdown with Pause/completion open and real
frame timings have **not** been tested here. Items 4/5 retain the diagnostic limits
listed above; CI success does not turn an unconfirmed action crash or performance
cause into a verified fix. Earlier recovered return/pause/End retry behavior
remains protected by its existing regressions.

## Modified paths

```text
.github/workflows/deploy-ranked-lightsail.yml
AGENTS.md
corum-ranked-mod/changelog.md
corum-ranked-mod/mod.json
corum-ranked-mod/src/EndLevelLayer.cpp
corum-ranked-mod/src/RankedPauseLayer.cpp
corum-ranked-mod/src/RankedRoundPreparation.cpp
corum-ranked-mod/src/SurvivalRuntime.cpp
corum-ranked-mod/src/domain/TranslationCatalog.generated.hpp
corum-ranked-mod/src/ui/RankedSettingsPopup.hpp
corum-ranked-mod/src/ui/SurvivalGameLayer.cpp
corum-ranked-mod/src/ui/SurvivalMapSelectionLayer.cpp
corum-ranked-mod/src/ui/SurvivalMapSelectionLayer.hpp
corum-ranked-mod/src/ui/SurvivalSelectedMapPanel.cpp
corum-ranked-mod/tests/LogEvidenceRegressionNativeTest.mjs
corum-ranked-mod/tests/SpectatorCameraSourceTest.mjs
corum-ranked-mod/tests/SpectatorSceneNativeTest.mjs
corum-ranked-mod/tests/SurvivalBoardNativeTest.mjs
corum-ranked-mod/tests/SurvivalUiNativeTest.mjs
ranked/apps/server/package.json
ranked/apps/server/src/session/client-version.ts
ranked/docs/beta102-log-evidence-fixes.md
ranked/package-lock.json
ranked/package.json
ranked/packages/rules/package.json
ranked/tools/tests/test_published_clients.py
ranked/tools/verify-published-clients.py
ranked/translations/ui.json
```
