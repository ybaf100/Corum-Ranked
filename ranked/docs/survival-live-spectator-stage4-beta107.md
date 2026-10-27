# Survival live spectator Stage 4 — beta.107

## Authoritative baseline and rollout

Fresh remote main on 2026-10-27: `c3ad1d4b6352aaa72bbd587bc6fd26a0d1c8dc5f`.
Contrary to the older handoff, beta.106 had already been published on both R2
channels and activated in Singapore. Public health/readiness/version and the
previous successful Actions runs were checked before this change. All Stage 1–3
Solo code, including Debug recorded n% entry, is retained in beta.107.

This release follows the requested staged procedure: `[publish-r2] [no-deploy]`
source push starts separate Validate, Release, Debug and Docker preparation runs.
Release/Debug publish after their required build/package/Debug-OFF guards. Verify
both immutable downloads before manual `mode=deploy` or the existing authorized
`[deploy-lightsail]` activation marker if workflow dispatch is unavailable. Keep
pinned SSH, Singapore-only host checks, deployment concurrency and rollback.
Actual source/build/publication/activation results are reported separately; a run
being created is not completion. No DB migration, reset, schema/Neon change,
Secrets change or retired-platform deployment is part of this patch.

## Three independent actions

Elimination choices use the original server-clock five-second lock. WATCH opens
a survivor selector; FREE PLAY uses the previous local Observer route; VIEW
RESULTS opens the original personal result/LP flow. All three choices have
separate callbacks. Legacy saved value `1` remains local eliminated Free Play
(`Observe` is retained as a compatibility alias); personal result remains `2`.
New live selection is value `3` and `LiveSpectating` is appended to the flow enum.
`SurvivalFreePlayState::observer` still means local eliminated Free Play only.

`SurvivalSpectatorRuntime` owns target admission, the current scope, selection
requests, deferred mode changes and its native-scene lifecycle. Live entry never
sets `m_freePlay.active`; local Free Play never connects a viewer stream. Watch,
status, exit and Free Play controls are available from live presentation. Free
Play has a Watch navigation action only for eliminated users. In either direction
the previous PlayLayer exits through its existing exact-scene return transaction
before the next mode starts. Failed native scene creation also releases the armed
read-only admission. A mode's completion cannot retire a replacement
scene. Exiting live returns to the retained Survival coordinator, not out of the
match and not into automatic Free Play.

## Server authority and shared visual protocol

New production routes under `/api/ranked/survival/online/matches/:id/spectator-live`:

- `GET targets`: current survivors and native-play availability.
- `POST publish`: authenticated official human publisher, 1–16 v2 packets/batch.
- `GET ?targetId=...&roundKey=...&mapGeneration=...&levelId=...`: selected stream.
- `POST leave`: retire this viewer's demand.

The SessionGuard and dedicated service verify the session, online admission's
session and participant, match membership, human kind and current snapshot on
**every request**. Viewers must be eliminated with an authoritative elimination
time at least five seconds ago. Targets must be other living humans in the same
match. Round key, map generation, ordinal, exact level ID, latest official
Attempt and relay generation/sequence must match. A newly invented local UUID is
not publishing authority. The client latches Start ACK only from an accepted
response and active pose capture requires that exact currently local Attempt.
Late packets cannot replace a newer official Attempt. Recently ended final
packets may drain for 1.5 seconds only while that Attempt is still the latest.
Authentication, feature OFF, target elimination and expired contexts fail closed.

`SurvivalSpectatorLiveService` has no score, LP, AP or engine dispatch port. Its
SQL is read-only authorization. Existing online event responses add only a
memory-only demand bit; scoring events keep their original FIFO, receipt-time
rules and engine. `spectator-live.protocol.ts` shares schema validation without
loading Solo MatchService. `spectator-relay.ts` shares the existing bounded
history/append/read logic; Solo service retains its authority adapter and public
exports. No fake Solo state, local replay masquerading as live, or bot-generated
pose is used.

## Native scene and visual reuse

A separate instance of the existing `SpectatorLiveRuntime` accepts a typed
Survival authority context. Its requests, sequence, generation, interpolation,
history restoration and presentation epoch are independent of Solo/Debug.
Target/round/map/session changes invalidate old callbacks. Raw session tokens are
not included in presentation context keys/logs. Scene setup uses the existing
verified Saved geometry, song/SFX preparation, retained texture cache and
`RankedRoundPreparation` native launch/return ownership.

The exact live PlayLayer latches SpectatorLocalSafety before vanilla init/reset.
It never arms official Survival entry or emits ATTEMPT_START/PROGRESS/ATTEMPT_END;
input/evidence/save/death/clear filters stay active through exit. The existing
CorumRemotePlayer, WorldDriver, filtered native triggers, source-time visual
effects, camera and Dual visuals are shared. PLAYING entry has no new readiness
or GO countdown and does not alter the server round start/end time.

Death/Clear hold their terminal display until the same target's next Attempt.
Target elimination, server authorization loss, round/map change, final result or
cancellation closes the scene. The coordinator preserves personal settlement,
shows authoritative round/elimination/podium results and offers updated targets.
Different target selection uses a clean native world and rejects late responses
from its predecessor. The UI refreshes visible targets once/second, stops that
refresh when closed, keeps nodes until roster/page identity changes, and has
separate profile and Watch touch areas. It uses dense 24-card pages in the early
64-player field and 16-card pages otherwise; names shrink within fixed bounds.

## Demand, limits and known visual limits

The existing online-state response supplies publisher demand. With no viewers,
there is no additional native pose capture/upload. A subscriber starts/renews an
8-second in-memory demand lease. Multiple viewers share one source buffer.
Limits are **8 streams total, 4/match, 256 viewer leases, 2,400 history packets,
72 live packets/stream, 2,048 rate bookkeeping buckets, 16 packets/batch, 8 KiB/validated packet, 4 MiB serialized
history/stream and 24 MiB serialized history total**. Capacity exhaustion is
explicit; no unbounded fallback buffer is allocated. Serialized-byte accounting
is incremental, not a full-history JSON serialization every 50 ms. Cleanup is
request-driven; no new timer or idle PostgreSQL polling loop exists. Subscription
polls retain adaptive backoff and a Survival floor of 20 ms. Publishing failures
retain the existing bounded budget and never block scoring.

These are measured serialized-byte limits, **not a certified JS resident-memory,
FPS, CPU or production capacity guarantee**. Authenticated active requests still
query PostgreSQL for authority; this patch does not claim zero database use while
spectating or demonstrate Neon Compute suspend.

A first viewer arriving after native gameplay began cannot reconstruct trigger
history that was never captured on demand. History gaps/window eviction/stream
restart are explicit PARTIAL SYNC. The inherited P2-only trigger parity remains
`DUAL_PLAYER2_TRIGGERS_UNVERIFIED`; no complete native checkpoint, arbitrary
shader/particle RNG parity or fabricated bot world is claimed. Bots are listed
as unavailable. The independent Debug RECORD/TEST v2 path stays available;
Survival tests use real authenticated human publishers plus validated pose
fixtures, never simulated bot percentages as native frames.

## Validation evidence and practical limits

New checks exercise the production service/controller, real PostgreSQL/PGlite
HTTP handlers and reducers, actual Geode JSON, production transport and native
runtime methods. They cover admission/lock/target denial; immutable batch retry;
Start ACK; terminal/next Attempt and stale Attempt rejection; target change;
round/session loss; Free Play separation; fresh generations after relay restart;
16/64 fields; shared viewers; global/per-match/history budgets; idle query count;
and unchanged official match/Attempt/LP/AP snapshots through visual requests.
The complete pre-existing native, rules, server and PostgreSQL suites are retained.

A separate real-Geode-JSON regression exposed empty batch presence in the shared
publisher: default `Value` is `{}`, so the absence sentinel is now explicit null.
The same real publisher test runs with both the existing boundary double and
pinned SDK JSON, including ordered ACK/terminal and Survival route/context.

Local/Actions test counts and platform/package outcomes are recorded in the
completion report after execution. Geometry Dash/iPad/Windows physical gameplay,
UI touch/viewport inspection, real multi-device streams and device FPS/heap have
not been certified by automated tests. Those are explicit remaining checks.

## Local frozen-source results (2026-10-27)

- Native: 303 passed, zero failed/skipped. Rules: 288 passed.
- Full server run: 661 passed, one optional pool check skipped. Subsequent focused
  production HTTP concurrency case: one additional case passed (the other 57 in
  that file were filtered, not re-executed). A final bounded-rate regression was
  then added and passed with the focused budget suite; Actions runs the full set.
- Workspace typecheck/build and release-barrier unit checks passed. No existing
  test was deleted or its behavioral assertion removed; legacy Solo harnesses
  gained an inactive Survival authority adapter, with new tests covering the
  active production Survival runtime and shared transport.
- Isolated PGlite HTTP concurrency measurement: 16 players, 80 visual frames,
  eight concurrent publish/subscribe/official-PROGRESS batches; total 206.09 ms,
  observed batch p50 23.89 ms / p95 29.20 ms, 33,487 serialized relay bytes.
  This measures a local deterministic fixture, **not production RTT, capacity,
  Neon egress, resident heap or physical-game FPS**. The separate 64-player HTTP
  fixture passed 160 validated frames without changing official match/LP/AP.
- Four-platform packaged build/publication and Singapore activation outcomes are
  verified separately by the staged rollout; no physical-device success is implied.
