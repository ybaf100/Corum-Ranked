# Survival clear dashboard, Free Play and Debug Force Clear (beta.72)

Base main: `ee12eff4723def928828f90bf10dd9ff2fc8f3ba`, including the user's
merge of the beta.71 round-handoff hotfix. The earlier unfinished worktree
remains intact; this implementation incorporates its changes on the latest main.

## Clear acknowledgement and dashboard

A genuine native clear submits the existing idempotent ATTEMPT_END with 100%.
Vanilla completion still runs. The client considers a normal round cleared only
when the server reports an ended 100% Attempt, BEST=100 and no active Attempt.
It waits for native completion setup before queuing an owned PlayLayer Quit.
That guard also applies when the round boundary arrives during the real clear
animation; a normal round timeout cannot retire the clear scene early.
The native completion overlay owns its Menu exit when present. No scene is
destroyed from levelComplete/showCompleteText callbacks.

An acknowledged normal clear blocks automatic Survival re-entry, but leaves the
server clock, ranking and qualification unchanged. Death Match retains its
three Attempts and .001% precision; the normal-clear/Free Play shortcut is
disabled there.

The additive live projection exposes `bestProgress`, `currentProgress`,
`attemptActive`, `cleared`, `eliminated` and `qualified`. `standings` keeps its
existing ranking/cutoff semantics. `roundPlayers` also contains current-round
forfeits for the dashboard without adding them to HUD ranking calculations.
NOW is the last accepted progress of an open Attempt, zero after it ends.
An optional display-only `currentProgress` telemetry field can report a backwards
movement without lowering the existing highest-progress scoring payload. BEST
retains the normal integer or Death Match score. The optional per-Attempt NOW
field persists in the existing JSON state; older states fall back to their open
Attempt's highestProgress. No migration is required.

Cards retain nickname/icon/badge objects within a round. A change in destination
stops only the tagged move action, then starts the existing .25s SineOut move
from the current rendered position. BEST/NOW changes alone do not move a card.
Elimination background updates independently. Next-round identities retire the
previous roster. Authoritative revision/server-time guards reject stale replies.

## Free Play and scene ownership

The dashboard enables Free Play only after the server's normal-round clear ACK.
It retains the dashboard scene, pushes the normal GD menu and keeps the global
Survival runtime polling. General GD PlayLayers attach the very same
`addSurvivalHud()`/`SurvivalHud` implementation, with unchanged layout, fonts,
colors, markers and animation. SurvivalHud.cpp is unchanged from main, including
the user's round label position. HUD records come exclusively from Survival;
the ordinary native progress fill continues to describe the ordinary map.

Free Play does not arm a Survival launch. Its deaths/retries/Practice/clears
therefore follow the existing vanilla paths and do not submit Survival events.
Ordinary GD music gets gameplay audio ownership. Pause ownership uses the
official Survival scene flag, not merely a matching level ID.

Match/round/map-generation changes, a new completedRound or authoritative
terminal state request one return. A local zero timer does not. The global
heartbeat also operates in menus and LevelInfo. It defers during native scene
transitions and validates WeakRef, current PlayLayer, running parent and scene
ownership again on the main thread. A native free-play Quit goes through the
normal completion/Pause cleanup and intercepts only that exact native return;
the coordinator is then restored from the scene stack. There is no synthetic GD
clear, FORFEIT or Survival Attempt for the ordinary level. Existing completed
round presentation and the beta.71 BGM policy run after returning. Free Play ends
at the round boundary; app restart safely recovers to the normal dashboard.

## Debug Force Clear

The Pause button and client invocation compile only with
`CORUM_RANKED_DEBUG_BOT_MATCH`. It is available in an entered, prepared, playing
dedicated Debug match's normal round and uses the shared Corum confirmation.
No button/call is offered for public online games, Release or Death Match.

`POST /api/ranked/survival/debug/matches/:id/force-clear` accepts requestId,
roundKey, mapGeneration and levelId. The server requires enabled Debug settings,
a live Debug-channel session and the persisted password-authorized match owner.
It validates the authoritative context and uses a dedicated trusted reducer
command to end the current Attempt at 100%, or create/end one if necessary.
Public event DTOs do not accept that command, and the reducer rejects online and
non-Debug use. No native levelComplete, stars, orbs, coins or GD achievements are
granted. Exact request IDs retain command-ledger idempotency across process
restart. Existing LP rules are untouched.

The client serializes the request behind its existing events, retries transient
errors with the same ID, rejects old context callbacks, and can recover a lost
success reply by polling. Only the server-confirmed clear triggers the normal
dashboard/Free Play path. Pause is resumed through its existing owner before
deferred native Quit.

## Validation and remaining device work

Rules/server suites, real PostgreSQL 16 HTTP tests, strict domain builds and
native regression harnesses cover accepted BEST/NOW, unchanged ranks, persisted
clear records, Force Clear authentication/idempotency and stale contexts, scene
transition deferral, menu/native return, repeated callback ownership, retained
cards and mid-movement retargeting. Build/test counts are recorded in the PR.
Android64 ON/OFF uses the actual Geode 5.8.2/GD 2.2081 bindings. The local SDK
dependency workaround is the same `-include type_traits` build flag used by
previous local builds; no vendor source was edited.

Host tests simulate native scene/HTTP boundaries, not real GD execution. A device
must verify real 100% completion saves, completion/Pause/Practice Free Play
return, nested native popups, background/foreground, audio fades and identical
HUD appearance. Windows/iOS SDK builds and combined .geode packaging remain CI
checks. No operational DB/R2/Render/Apps Script changes are part of this work.

Measured local validation: rules 237 passed; native regressions 187 passed;
real PostgreSQL 16 HTTP/concurrency/recovery 44 passed; Android64 Debug flags ON
and OFF compiled and linked; ten strict C++ domain binaries, release tooling
16 tests, relay Worker 13 tests and Apps Script checks passed. A contended
parallel server run exceeded existing five-second test wall limits (Debug full
game and Solo Death Match); it did not show a gameplay assertion failure. Debug
integration uses the same 30-second wall budget as online integration, and the
server suite is rechecked separately after compilation. Final server/CI results
are recorded in the PR. No device playback or Windows/iOS build is certified.

The separated final server run passed all 530 tests in 51 files (153.73s).
The final native completion-boundary checks passed 6/6 after the full 187/187
native run. No scoring or production timer adjustment was needed for the
contended test wall-time failures.
