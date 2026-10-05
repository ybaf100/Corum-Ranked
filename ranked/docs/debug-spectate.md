# Server-backed Debug Spectate (beta22)

This Phase-3 QA harness is not a local replay. The sole map is **VOICE / LiteN1ng / Easy Demon / 88350599** (`DebugSpectatePolicy.hpp`, server `debug-spectate.config.ts`). There is no map selector, synthetic level or production Ranked Match. Phase 4 game modes, gravity/mini/dual and full terminal effects remain out of scope.

## Enable only in an authorized debug deployment

- Server default/example: `ENABLE_DEBUG_SPECTATE=false`. Enable explicitly with `true` and a separately configured `DEBUG_SPECTATE_PASSWORD` (use a long random secret); never commit the deployment secret. Production/official operation must leave this flag OFF.
- Client CMake `CORUM_RANKED_DEBUG_SPECTATE` defaults OFF. Automatic Debug CI sets ON; official manual-package workflow explicitly sets OFF. Debug-only implementation files and calls are not linked into official builds.
- Creation requires a current-version Ranked session, debug client header and password. Subsequent calls require the exact recording/playback scoped token, debug header and unexpired server scope (15 minutes). These tokens are not Ranked sessions/match tokens and cannot use rating/result/Attempt APIs.
- The client uses the existing Ranked server base URL. This patch does not configure live deployment flags/secrets or certify a deployed-device run. Startup's additive migration runner applies `0011_debug_spectate.sql`.

## API and isolation

All paths below begin `/api/ranked/debug/spectate`. `x-corum-debug-spectate: 1` is required. Creation additionally uses `Authorization: Bearer <Ranked session>` and `{password, levelId: 88350599, formatVersion: 1}`. Scoped operations use `x-debug-spectate-token` instead of Ranked match/session authority.

| Method | Path | Responsibility |
| --- | --- | --- |
| POST | `/recordings` | Create isolated audit metadata and recording scope |
| POST | `/recordings/:id/frames` | `{formatVersion: 1, frames: [{sequence, elapsedMs, keyframe}]}`; up to 16 canonical production keyframes |
| POST | `/recordings/:id/finalize` | `{completed}`; true requires death or finished terminal; retries preserve the first outcome |
| POST | `/playbacks` | Latest completed supported VOICE recording; metadata/scoped token only, **no frame file** |
| GET | `/playbacks/:id/spectator-live` | Production protocolVersion/attemptId/generation/afterSequence query and live response; additive `playbackComplete` |
| POST | `/playbacks/:id/close` | Stop scoped playback and clear production relay namespace |

`debug_spectate_recordings` + `debug_spectate_frames` are independent of Ranked FK/profile/match/outbox writes. The service never invokes Match/Queue/Placement/Achievement evaluation. Recording identity is audit-only. Creation stores an incomplete row; frames stay in bounded server memory (max 12,001 frames / 10 minutes) and finalize bulk-inserts them and metadata in one transaction. Abandoned/restarted-server incomplete records are not replayed. Completed records survive server/database restart. Active recording/playback admission is bounded, including concurrent creation.

## RECORD

Debug area → RECORD SPECTATE → server admission → canonical online metadata proof → native LevelInfo download/song gate → native Play. This is normal vanilla gameplay, **not spectator/Practice**; legitimate vanilla progress can save. Ranked transport/scoring is disabled for the entire debug scene. The same native initialization-reset settling window as Ranked avoids recording scene-setup resets; an early death/Clear still records a terminal. Live captures use production 50 ms cadence, bounded client queue and HTTP batches, with terminal captured immediately even during an in-flight request. ACK loss keeps the canonical sequence; retry does not remove frames. Finalize follows acknowledged upload, not local completion inference. Manual restart/quit/Practice cancels an unfinished recording; after death/Clear it drains the completed terminal. One attempt is one recording; start RECORD again from Debug for another fixture.

## TEST

Creation selects latest completed VOICE data **on the server**. Playback is armed while metadata/download/song preparation occurs. Exact PlayLayer entry enables Phase-1 local dummy and installs the Debug context/HTTP-route adapter on **SpectatorLiveRuntime**; it does not alter RankedRuntime Match/profile data. The first real in-scene GET returns WAITING and starts the server replay clock. Subsequent timer/poll advancement feeds only time-reached keyframes to the existing SpectatorLiveService buffer. Production protocol/context/generation/sequence/reset/keyframe validators, interpolation, CorumRemotePlayer and CorumSpectatorCamera all remain in use.

Local `m_player1` stays a hidden/frozen/noclip dummy with no evidence/New Best/percentage saves; it never receives remote position/rotation. Waiting/timeout holds the last safe viewport. Dead hides the remote; finished freezes exact terminal pose. Terminal remains available until scope exit/expiry and is not extrapolated. A bounded-buffer sequence gap uses production fallback and a new Recorded Publisher generation; the next network poll resets through normal validation. Quit/context invalidation/expiry closes polling, camera ownership and remote nodes; local safety stays latched through vanilla quit saves. Playback terminal is held until user exits, matching authoritative-result waiting in production.

## Verification boundaries / device checklist

Automated coverage includes a listening Nest HTTP server (not only controller calls), disk-backed PostgreSQL via PGlite with database + application restart, server clock/WAITING/terminal/gap/auth/capacity/disabled behavior and unchanged snapshots of **every Ranked table**. Strict C++ tests compile the complete Debug client runtime with SDK transport doubles, actual production safety/scene/decoder callbacks with Debug ON/OFF, and the complete production Remote Player + camera with native lifecycle doubles. Official-OFF preprocessing checks UI/API exclusion. These are **not an actual Geode/GD device E2E**.

On an authorized debug server with the flag/secret configured and both current-version client/server:

1. Open RECORD; verify VOICE metadata/download/song and actual normal gameplay. Die once and record a second fixture ending in Clear when feasible; confirm terminal/count/duration persisted, latest selection and no Ranked data change.
2. Restart the server. Open TEST; verify WAITING precedes HTTP stream acquisition, a separate Cube appears, and camera follows its x/y while dummy input/collision/99%/completion cannot award/save anything.
3. Verify death-hidden and Clear-frozen terminal, no infinite motion; quit/back cleans camera/remote/network; run TEST again without stale pose.
4. Interrupt packets, restore, test generation/context reset; verify safe viewport and production validators. Test a missing recording and disabled server. Compare LP/MMR/AP/achievements/Placement/history and dummy vanilla best before/after.
5. Actual A-playing/B-finished two-client spectator E2E remains separate: natural local End → server spectator.active → same scene → dedicated remote/camera; confirm FINAL/LAST/DM legal local Attempts and Placement/user Practice are not interrupted.

No Release workflow is manually invoked by this task. Native platform compile/CI and device results are reported separately.
