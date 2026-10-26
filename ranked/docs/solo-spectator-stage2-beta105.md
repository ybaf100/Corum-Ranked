# Solo live spectator Stage 2 — beta.105

Starting authoritative main: `0a053f97d83db0b7a97013ea024ca4abc11083ed`,
beta.104. Fresh fetch and AGENTS.md supersede the older beta.103 handoff.
Branch: `fix/solo-spectator-stage2-beta105-20261026`.
Stage 1 HTTP v2, WorldDriver, Remote Player, safety and authority paths are retained.

## Restoration method and actual scope

| Method | Assessment |
| --- | --- |
| Full native checkpoint serialization | GD game/effect state includes pointer-bearing spawn/tween/group structures and undocumented fields. A safe complete transfer schema is unavailable. No memory blob or native pointer is serialized. |
| Bounded source-history replay through the existing native world | Selected. Reuses original time/position-trigger, command/spawn/follow/dynamic processing with the frozen no-authority dummy. It cannot certify arbitrary input/RNG/item/shader/Cocos action state. |

The server stores at most **4,096** immutable history packets per stream and **16**
streams, with no new background timer or database polling. At ordinary 50ms
capture this covers about 205 seconds; urgent snaps may shorten it. Multiple
readers share history. Idle stream/capacity cleanup runs on actual publish activity.
Legacy subscribers retain the existing 3s/72-packet relay and 350ms join window.

`worldHistory=1` opts into authenticated **64-packet** pages using existing cursor
fields. A restoration descriptor gives availability/reason, start/head sequence,
exact level ID and caught-up state. Sequence zero, the beginning of the source
timeline and contiguous time/identity are required; a hole above 250ms makes
history incomplete. This descriptor is not an engine-state checkpoint.

Client pages are fenced by match, round, Death Match sequence, level, Attempt,
generation and local epoch. Descriptors, page sizes/cursors, keyframes, cameras,
ordering and terminal state are validated. `SpectatorRestoreTimeline` stages only
bounded validated frames. The existing native owner performs a no-save reset for
the new context, applies the frozen dummy and executes native world work in
order. Replay allows at most eight source steps per director frame and adapts
downward from measured step cost toward a 2ms budget. One native call may exceed
that budget; this is not a device FPS guarantee. There is one final camera owner.
Once applied native work reaches the received head, that same source timeline
seeds live interpolation without creating a scene or official Attempt.

Supported path: recorded positions/discontinuities reach the original native
trigger query, command/spawn processing, Follow X/Y, dynamic moves and camera.
Move/Spawn/Rotate/Follow and native persistent effects can execute through their
existing engine paths. They are not replaced by scripted physics.

**Arbitrary accumulated world state is not proven fully restored.** Even complete
late history is labeled **PARTIAL SYNC** after catch-up. Input/touch/RNG/item
results, pointer-bearing engine state, arbitrary external actions, shader/background
internals, full platform and Dual parity are not serialized. Missing beginnings,
expired windows, source-time holes, unavailable camera transforms and a native
clock that does not advance have separate reasons. Stage 1's driver partial-history
guard is exposed. No pose teleport is declared a completed restoration. A new
Attempt receives a clean context/reset; old packets/completions cannot modify it.
Full transferable authoritative engine checkpoints remain Stage 3 work.

## Camera capture and ownership

Optional v2 fields add actual native zoom/angle and observed scale-X/scale-Y /
rotation for the world, in-shader and above-shader nodes, captured with the same
completed pose. Pinned 2.2081 bindings and CCNode accessors are used. Native target
zoom/angle identify ongoing tween capture; instant changes are urgent camera-snap
boundaries. Continuous fields interpolate with the pose timestamp, while mirrored
scale switches atomically rather than passing through zero.

The original CorumSpectatorCamera applies accepted sampled node transforms, then
its existing affine origin correction and native background-art update. It does
not add another camera loop. Wrong-context/rejected frames cannot apply transforms.
Detach restores entry transforms, native zoom/angle, origin and ownership flags.
Legacy v2 and Debug recordings without these fields still decode; missing transform
data is not promoted to complete Stage 2 parity.

This reproduces sampled results of Follow/Free/Static/Offset/Zoom/Rotate/target
changes. It does not prove every native parent/shader/background combination.
Bounded Debug publisher/viewer metrics include source time/sequence, origin,
zoom/angle/node transforms, native-before-correction values and projected screen
origin error. These metrics compile out of Release.

## Network, clocks and safety

HTTP stays serial. Retries preserve immutable bodies and source sequence/time.
Native gameplay elapsed time is used when available; generation restart never
rewinds the Attempt clock or adds paused wall time. Coincident terminal timestamps
remain legal. A full 64-entry publisher queue applies backpressure without another
POST. Publisher retries back off and stop after twelve consecutive failures for
an Attempt. Successful ACK/new Attempt resets the appropriate budget.

Viewer gaps hold the last validated pose/camera without extrapolation, origin
jumps or unnecessary Remote Player destruction. Twelve consecutive HTTP failures
disconnect; explicitly reopening a valid viewer scene rearms recovery. Existing
authoritative Pause reduces polling and Resume cannot advance paused wall time.
Measured interpolation delay may reach 800ms at serial RTT 500ms, with sample/span
caps unchanged. A bad subscriber cursor cannot erase the publisher.

The UI distinguishes WAITING, SYNCING, LIVE, BUFFERING, RECONNECTING, PARTIAL SYNC,
CLEARED and DISCONNECTED. Late replies recheck authority/context/End FIFO; native
completion also requires the exact context and next sequence. Server restart
does not invent history: publisher restarts its relay generation at actual current
time, and the missing beginning is marked partial. No new official Start/End,
input, physics, local save, LP/MMR, achievement/AP or result-writing path exists.

## Verification and rollout

Production native-owner fixtures cover joins at start/10/60/120s, ordered native
time and collision work, one final camera owner, frozen local physics/persistence
and stable Remote Player identity. Their native engine boundary is a C++ double,
not actual Geometry Dash trigger rendering. Actual HTTP decoder/restore/compositor
methods are compiled against pinned real matjson. Network fixtures cover RTT
20/80/150/300/500ms, error budgets, context/authority loss, malformed state and
legacy recordings. Server tests execute the real service/history/parser paths.

Actual full local/CI/platform/package/activation results follow after execution.
Physical GD is unavailable: actual iPad/Windows/Android trigger/camera/shader/HUD
parity and repeated native exits remain unverified.

Publish uses `[publish-r2] [no-deploy]` so both clients, server CI and Docker prepare
run in parallel. Full public bytes/hash/manifest/platform/resource verification
must precede existing pinned Singapore SSH and rollback-capable activation.
Production data/schema, Neon settings, environment/Secrets and rollback policy
are preserved. No destructive or additive database migration is needed.

## Final local verification before publication

- Native full regression: **301/301 passed**, no skips/failures. New tests exercise
  the actual HTTP decoder/restore/compositor methods with real pinned matjson,
  native owner joins at 0/10/60/120s, source-clock and object safety, RTT five-case
  interpolation, terminal/context races, bounded publisher/viewer retries and
  camera ownership release. Older v2/Debug compatibility fixtures were updated
  for new production interfaces and last-good-frame preservation.
- Rules **288/288 passed**. Server **642 passed / 1 optional local pool test
  skipped** (643 total); all 70 runnable suites passed. Typecheck and build passed.
- Real Debug HTTP/persistent PostgreSQL + spectator service focused verification:
  **45/45 passed**. Publication policy **10/10**, packaging **2/2**, full public
  client barrier **4/4** passed. Localization: 679 UI keys / 48 achievements.
- Production read-only baseline at 2026-10-26 12:35:42 UTC still has 2 accounts,
  2 Solo / 4 Survival matches, 86 / 5,381 Attempts, 24 unlocks, 19 AP rows and AP
  total 335; both profile digests and activation epoch are unchanged. Migrations
  0020/0029/0030 and native_death are present; inconsistent Survival LP rows: 0.
  Singapore health/ready are 200 and all ready flags true; required version is
  still beta.104 before the staged rollout. Solo ON, Survival ON/64 unchanged.
- Both beta.105 descriptor and package paths returned 404 before source push, so
  publication can create a fresh immutable version. No existing package is replaced.
- Physical device behavior remains **not tested**. No automated boundary result
  is presented as complete native-engine checkpoint or camera-trigger parity.
