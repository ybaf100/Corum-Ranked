# Survival round handoff hotfix — beta.71

Base: `bd92313e9fd1e1ff4b731f3d549244ac64dd10a5` (`main`).
Branch: `fix/survival-round-handoff-beta71`.

## Diagnosis

The Debug event controller had no `RankedDomainError` HTTP boundary. After the
engine had already persisted `ROUND_RESULT`, another same-round `PROGRESS` or
`ATTEMPT_END` raised `INVALID_MATCH_TRANSITION` and Nest returned HTTP 500.
Online events already translated that exception to 409. A regression using the
real Nest Debug controller and PostgreSQL-compatible storage reproduced 500
before the fix and 409 afterward.

The client retries transport/500 failures using an exact event ID in a FIFO.
`ROUND_RESULT` retains the finished map's round key and map generation, so its
old context-only cleanup did not remove a retried PROGRESS. A later
`PRESENTATION_COMPLETE` could remain behind it indefinitely. This explains the
reported ~0.6s retries and long result barrier without changing any round timer.

## Event handling

- A Survival-scoped exception filter returns 409 for transition/attempt-window
  conflicts and 400 for invalid map/attempt inputs. Unexpected exceptions and
  internal configuration errors remain 500. Solo error handling is untouched.
- Every accepted server snapshot and every FIFO pump discards PROGRESS for a
  round that is no longer PLAYING, and removes packets belonging to a different
  server round/map generation. This uses server state, never the local 0s clock.
- A canceled in-flight packet advances an event request generation. Its delayed
  callback cannot observe an older snapshot, clear a newer request's busy flag,
  or consume the new front event.
- Current-playing-round Start/End packets retain their IDs, FIFO order and
  retries. A finished-round End is not indiscriminately discarded; the server
  explicitly accepts or rejects it. 400/409 retire that event and request a poll;
  network/500 failures retry the exact request.
- Presentation completion keeps the existing per-context latch and idempotent
  event ID across transport retries. Old-context requests are retired after a
  new authoritative round arrives. The persisted `completedRound` and existing
  five-second roster/elimination presentation remain unchanged.

The reducer's existing timeout-before-telemetry behavior is preserved. A packet
that itself crosses the deadline can commit the timeout without applying its
late score; subsequent late packets conflict. No rule/reducer, bot algorithm,
progress precision, reward calculation or settlement ledger changed.

Online presentation already advances through its server-owned five-second
barrier. Dedicated Debug retains its explicit completion barrier and recovery.
A proposed 30-second missing-ACK fallback was asked separately; it is not applied
without a decision, because changing that timing was explicitly restricted.

## Audio and terminal UI

During a completed-round presentation, the local participant's server
`qualified`/`forfeited` result determines elimination. Merely displaying the
elimination animation no longer selects defeat music for a survivor.

| Screen/outcome | Existing audio mode/resource |
| --- | --- |
| R1/R2 preparation, R1→R2 and R2→R3 survivors | `Round12` / `match` |
| Confirmed eliminated participant | `ResultLose` / `result_lose` |
| Qualified R3 result, FINAL/TIEBREAK map selection, download and entry | `Round3DeathmatchWait` / `round3_deathmatch_wait` |
| Real PlayLayer | Existing native GD map audio handoff |
| Server-confirmed winner | `ResultWin` / `result_win` |
| Server-confirmed final loser | `ResultLose` / `result_lose` |

Final winner IDs are decoded before LP/result publication; a settled result's
server `won` flag stays authoritative. LP delta is not used to select music.
The existing manager owns fades, prepared-stream fallback/retry, volume,
background recovery and native audio release. It is unchanged. Repeated mode
snapshots do not restart a track. A saved old completed round cannot select the
next final track outside its presentation.

FINAL_RESULT, resultPending, eliminated participants and CANCELLED cannot open
a new preparation window or advertise another map. The elimination animation
can display RESULT, then the existing settled LP or settlement-pending message.
Only a nonterminal continuing round displays NEXT MAP SOON. Final result motion
and acknowledgement remain the existing implementation.

## Validation

- TypeScript workspace/type-test checks and server/rules build.
- Rules: 235 passed. Server: 524 passed across 51 files, including Debug and
  Online HTTP conflicts, all existing round/Death Match, recovery and LP tests.
- Real PostgreSQL 16: 42 Online HTTP/concurrency/recovery tests passed in an
  isolated local schema. Includes late progress/end rejection, next-round
  creation and the separately requested Common reset/re-login regression.
- Native regression includes 183 cases and all three new production
  transport/audio/status harnesses. The affected suite also passed 11/11.
  No production C++ was replaced by mock data.
- Strict domain binaries, packaging/publisher/asset tooling tests (16), GD relay
  Worker tests (13), and Apps Script compatibility/configuration checks passed.
- Android64 with actual Geode 5.8.2/GD 2.2081 bindings and NDK r28c: Debug flags
  ON and OFF compiled and linked. This is not a GD/device run or combined
  Windows/iOS package verification. Those hosts/devices are unavailable locally.

Production-config HTTP lookup timed out, so current operational per-song gains
and audible playback were not independently verified. No operational volume,
DB, R2, Render or Apps Script deployment was executed. Device checks still need
the full R1→R2→R3→FINAL/TIEBREAK flow, Pause/background audio, result fades and
the confirmation layout. GitHub Actions status is recorded in the PR/final
report after publication, not asserted here in advance.

## Work preservation

`SurvivalHud.cpp` is byte-identical to the user's `bd92313e9` version, including
`top-5` for the round label. PlayLayer, RankedAudioManager, Solo Live Spectator,
Survival reducer/bots/settlement and the R2 updater were not edited by this
hotfix. The separate unfinished FREE PLAY/R2 worktree remains preserved at
`/workspace/Corum-Ranked-survival-beta71` on `feat/survival-free-play-beta71`.
It must incorporate this hotfix before publication and use a fresh immutable
version rather than republishing beta.71 objects with different bytes.

No new DB migration or route. The additive client projection is `ownWinner`;
the Admin response adds `accountsDeleted` as documented in
[the Common reset note](admin-beta71-common-reset.md).
