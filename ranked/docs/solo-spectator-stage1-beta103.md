# Solo live spectator Stage 1 — beta.103

Starting authoritative main: `53a00958da3bb01d19fe235c61962770e4296d53` (beta.102).
Work branch: `fix/solo-spectator-stage1-beta103-20261025`.
A fresh remote fetch, repository instructions and actual beta.102 publication /
Singapore activation runs were checked before work. Existing Survival, profile,
history, achievement, LP/MMR and native ownership fixes remain in place.

## Findings and implementation

### Native world and triggers

The existing hidden WorldDriver already writes the remote pose and GD's 10ms
Follow Player Y history. It was retained. Native `PlayerObject::update` remains
blocked for the dummy: enabling its real physics/input would invalidate the
spectator safety model. The prior world owner nevertheless advanced base-world
updates using render dt, even when no accepted pose/time advanced, and had no
explicit once-only trigger-query fallback for the frozen player path. Its Debug
native-path probes were scoped only to TEST playback. These are distinct issues;
no physical-device trace is claimed to prove every GD trigger was previously
skipped.

The existing world owner now:

1. Obtains one validated v2 presentation per director frame and checks its exact
   match/round/Death Match/map against the retained native scene before any reset,
   pose write or Remote Player update. The existing Attempt/generation/epoch
   fences and decoder validation remain.
2. Applies remote pose/state and consumes its publisher-time delta once. Repeated,
   held, rejected or absent poses do not advance this native base-world path by
   renderer wall time. Boundary fixtures verify 30/60/120/144/240Hz integrated
   time, Follow-Y history and source-position trigger crossing.
3. Borrows unpaused state only around the native world call, restoring the local
   Pause flag on exit. Frozen local physics, input, completion/death and scoring
   firewalls remain. It uses the original `GJBaseGameLayer::update` implementation;
   command, spawn, follow and dynamic-move processing are not reimplemented.
4. Lets a native collision query run once if GD calls it, and supplies one fallback
   query if it does not. The scoped query forces ignoreDamage and filters native
   collision candidate vectors to `GameObject::m_isTrigger`. It temporarily clears
   the dummy's presentation-hidden eligibility flag while leaving its actual node
   invisible. It never unfreezes PlayerObject update or invokes triggerObject by
   hand. Native canBeActivatedByPlayer/activation-ID and spawn/multi-trigger rules
   still decide effects. Ordinary gameplay, other players and editor layers
   delegate unchanged. Portals/rings/items/solids/hazards are not locally replayed
   as free gameplay; game mode, gravity, Mini, flip and snaps come from the pose.
5. Reconciles native pose writes and retains the existing single camera owner,
   native camera calculation and final recorded-viewport correction. The Remote
   Player remains a separate renderer outside native collision lists.

Teleport/Spider segment changes reset previous-position and Follow-Y history at
the destination, preventing an artificial swept path across the relocation. A
first sample after the Attempt start, or an unobserved publisher delta above
250ms, is marked **partialHistory**; a large gap does not replay an invented
crossing or bulk native catch-up. This is a safe limitation, not complete history
reconstruction. Full dual poses are rejected by the driver and existing decoder /
renderer fallback. None of these paths creates an official Attempt.

The pinned GD 2.2081 bindings were inspected at
`geode-sdk/bindings@70986055f526da15318779703f0e904be822f162`, including hookable
checkCollisions, collisionCheckObjects, playerTouchedTrigger,
canBeActivatedByPlayer, triggerObject and command/spawn/follow/dynamic functions.
Inline Windows/iOS playerTouchedObject is still not detoured. Bindings establish
available interfaces/fields, not a source-level decompilation of GD's engine.

### Diagnostic evidence boundary

Debug native instrumentation now also covers authorized real live spectating.
Bounded one-second windows (maximum 120 per scene) distinguish received/missing
pose, applied driver, rejected context, executed/held native world, native level
clock advance/hold and partial history. The existing counters distinguish collision
queries, touched triggers, activation queries/allowed decisions, actual
triggerObject calls, native activated IDs, commands, spawns, follow and dynamic
moves. A native-world call whose level clock does not advance is reported
separately. Release does not compile these native probe hooks/counters/logs.

The executable fixtures call production owners/adapters with native boundaries
modelled in C++; their synthetic trigger crossings and clock updates are **not**
proof that an actual Move/Spawn/Rotate map ran on Geometry Dash. The real Debug
counters are intended to establish precisely which engine paths execute on a
device without assuming that pose movement alone proves trigger parity.

### Authoritative handoff and retirement

Standard final-window spectator eligibility previously depended on absence of the
viewer's optional runtime progress and presence of opponent progress. An active
Attempt without a current progress-cache entry could therefore be misclassified,
and an active opponent with expired telemetry could be omitted. A shared server
`standardSpectatorEligible` helper now uses valid committed Attempt open/ended
state plus the existing three-Clear/Last Attempt rule. It is used by public state
and Start/End/progress ACK snapshots. It reads no additional DB table and changes
no scoring/settlement rule.

ACK snapshots now explicitly carry `spectatorActive` and the canonical round
`stateVersion`; public currentRound carries the same revision. The client no
longer synthesizes entitlement from a Last Attempt label/trigger side. It applies
server projections and rejects an older same-round ACK or poll, a phase rewind,
and an ACK for another round before changing presentation/deadlines. Native entry
and stream use wait until the local Start/End FIFO has drained. Already-observed
real events still drain through their existing idempotent transport.

A converted native scene is reset once, inside the existing no-save/authority
scope, so prior local trigger history cannot contaminate the viewer. This is a
dummy-only reset after End ACK, not a restarted real Attempt. Reentrant native
world updates during that reset are rejected. Existing prepared-scene guards
prevent duplicate conversion. LevelInfo and the lobby already route through
canEnterCurrentLevelAsSpectator and retain their exact map/resource/session gates;
they now inherit the ACK fence instead of a second entry implementation.

Late viewer responses additionally recheck stage, placement, live toggle, authority
and the End FIFO before decoding. Existing request generations and stream
Attempt/generation/epoch validation reject old responses/packets. Context changes
reset native world/visual/camera ownership; authority loss or match/round exit
retains the existing percentage/result return and exact-object cleanup. A remote
finished/dead pose is visual state, not local Clear/death or server entitlement.
Normal valid Solo attempts after a Clear remain permitted by the rules.

## Supported scope and limits

Stage 1 targets single-player automatic position/spawn/follow/move/rotate world
processing with the existing eight-mode renderer, gravity/Mini/flip, normal camera
and verified Teleport/Spider snaps. The code preserves native effect semantics;
it does not serialize arbitrary trigger results or replay input edges.

Full dual, arbitrary mid-Attempt world-history catch-up, advanced camera/platform
parity and new Survival opponent selection remain Stage 2. Input-dependent/touch
mechanics cannot be claimed equivalent without input/world history. A viewer
joining late starts with partial history and does not reconstruct earlier trigger
state. These limitations remain read-only and cannot affect official records.
Real Move, Spawn, Follow X/Y, Rotate, background, portals and camera behavior still
require Geometry Dash device verification. Automated native boundaries passing is
not a statement that every iPad/Windows trigger has been observed working.

## Release procedure

The source push uses `[publish-r2] [no-deploy]`. Both immutable client channels and
server/rules CI start immediately. A CI-only prepare-image job in the existing
Lightsail workflow builds Docker in parallel, with no production environment,
SSH, Secrets or activation. The actual deployment gate is unchanged and rejects
no-deploy even when auto-deploy is on. Tests cover both independent gates.

After full public package verification, the existing explicit Lightsail activation
trigger builds the current main image and uses pinned Singapore SSH/known_hosts,
StrictHostKeyChecking, both-client verification, concurrency and rollback. No
Production account, LP/MMR, match/Attempt history, achievements/AP, config, secret,
migration or schema is reset or changed by this work. Actual source/build/package/
deployment results are appended separately after execution.

## Local verification before source publication

- Full native regression: **300/300 passed**, Debug ON/OFF native boundary
  compilation included. The preceding full run had one unchanged asynchronous
  Survival thumbnail-provider fixture failure (299/300); its isolated unchanged
  four-test run passed, followed by a fresh complete 300/300 pass. This is recorded
  as a timing-sensitive test observation, not a proven spectator defect.
- Rules: **288/288 passed**. Server: **630 passed, 1 optional local PostgreSQL
  integration test skipped**, no failures. Typecheck and workspace build passed.
- Localization generation/font coverage: 679 UI keys / 48 achievements passed.
  Release build policy 2/2, R2 publication policy 9/9 and full-public-client barrier
  4/4 passed. git diff --check passed.
- Production read-only baseline at 2026-10-25 15:11:29 UTC: 2 accounts, 2 Solo /
  4 Survival matches, 86 Solo / 5,381 Survival Attempts, 24 unlocks, 19 AP ledger
  rows and total AP 335. Achievement activation epoch remains
  2026-10-24T04:59:47.102Z; migrations 0020/0029/0030 are present; native_death
  exists; no inconsistent Survival LP ledger row. Survival remains enabled/64.
- Physical Geometry Dash, iPadOS, Windows/Android input, native trigger timing and
  repeated scene exits/process termination are **not device-verified** here.
  Platform/package/activation outcomes below must be based on actual Actions and
  public downloads, not these local compiler-boundary tests.
