# v0.5.0-beta.29 — hidden spectator world driver

Authoritative starting main: c662f30c75480d899167f3f786802748065b71b2 (beta28).
The connected GitHub main and every locally available baseline blob were checked
before making an isolated checkout. No prior commit was reset. Manifest, npm
workspace/lock and server exact compatibility gate advance together to beta29.
Wire protocol and Debug recording format remain v2; no new wire fields are needed
for this adapter change. HTTP rates, server recording/playback and audio are unchanged.

## Evidence and native paths

The authoritative input is ScreenRecording_10-06-2026 12-12-28_1(1).mp4, 47.96s,
732x512 at 30fps. It shows the moving playback renderer and presentation divergence,
especially after the Spider section. No matching new native-state log or paired
RECORD video was supplied; exact background/zoom/trigger-state differences cannot
be inferred from video pixels alone. This is pre-fix evidence, not beta29 QA.

The beta28 source returned before local PlayerObject::update and acquired the
spectator sample in updateCamera. The separate render-only PlayerObject was never
m_player1/m_player2 or a native collision/trigger player. Native world processing
therefore received the frozen dummy instead of the remote timeline. This is a
confirmed adapter defect; complete native trigger parity still needs device QA.

The inspected official [GD 2.2081 bindings](https://github.com/geode-sdk/bindings/tree/d641858f3e1cd2c6d86013c799f224ebdb8ec1cd/bindings/2.2081)
provide concrete evidence for the player dependency:

- GJBaseGameLayer::getMoveTargetDelta selects m_player1/m_player2 for player-targeted
  move commands and reads getRealPosition; triggerDynamicMoveCommand retains that
  real native player as its target. A separate renderer cannot provide this input.
- Native updateSavePositionObjects reads the real player's inherited m_lastPosition;
  native getOldPosition reads the player's Follow Player Y history.
- PlayerObject::updateSpecial updates that 200-entry Y history at native 10ms cadence.
  It contains no physics, collision, death or persistence work. Calling only this
  narrow native sampler preserves follow history while update() stays suppressed.
- cameraMoveX/Y set forced-camera flags and install camera tweens. The old final
  correction called them and stopped the tweens each frame, overriding native
  trigger mechanics. The adapter now preserves those flags/tweens and corrects
  only final origin, render transforms and native background/ground art.

Full native update/checkCollisions/position-trigger bodies are address bindings,
not available source in this checkout. Native-double trigger observations validate
production ordering and inputs; they do not prove every closed GD trigger path.
Touch/position, Follow Player Y, Move/Spawn/Alpha/Pulse, portal, background and
camera-trigger behavior must be checked in the next real-device run.

## Ownership and lifecycle

One director-frame guard acquires SpectatorLiveRuntime::presentationFrame before
native GJBaseGameLayer::update. The SAME buffered sample goes to three distinct roles:

1. SpectatorWorldDriver writes hidden native m_player1's level-space position,
   rotation, mode, mini, gravity and visual flip. Native setPosition and explicit
   double coordinate mirrors agree. The initial level anchor is not overwritten.
2. CorumRemotePlayer remains a separate visible render-only PlayerObject, outside
   native player/collision registration. It applies the same pose directly.
3. Native world/camera work runs, then publisher camera origin corrects the actual
   final viewport. Repeated camera calls cannot overwrite it in that director frame.

Continuous movement retains previous rendered position. Verified TELEPORT/SPIDER_SNAP
sets mode/gravity before the destination and resets previous/current and Y-follow
history at that destination. Held evidence does not repeatedly reset history.
Follow samples advance by publisher elapsed time, not HTTP arrival or render FPS,
with at most 200 narrow 10ms samples per application. No spiderTestJump or remote
physics replay is used. Native portal state is reconciled before camera and after
world processing; network state remains authoritative presentation input.

ScopedSpectatorWorldDriverWrite has a private constructor, available only to the
validated driver. Outside that scope, native position/rotation writes cannot move
the owned dummy. The driver validates scene, level, exact native player ownership,
context, timestamps and pose; ordinary Ranked, Placement, Practice/test/editor and
unrelated players do not enter this path. Physics/rotation/Spider jumps remain
blocked, as do native duplicate follow-history writes; the player stays invisible.
Only a new authenticated stream/context/epoch resets native world state. Manual
Retry remains blocked. A stopped native lifecycle uses the same guarded world owner
from the scene heartbeat, with no second runtime advance or camera update.

All Phase-1 result/evidence guards remain: exact dummy death, local completion,
New Best, savePercentage/saveNewScore, updateAttempts, achievements/AP, Start/
Progress/End/Clear, Practice authority and LP/MMR/scoring. Driver position may reach
100%; remote UI can show 100%, without local completion or persistence.

## Recording seed and late join

Debug RECORD now accepts the first finite completed native world with m_started,
no reset delay/pause, matching gameLayer and valid vehicle state. A stationary
initial player is valid. Unstarted/default or garbage init remains invalid. The
first accepted seed is timestamp 0; subsequent timing and exact death/Clear timing
remain relative to that seed. The Ranked journal's 500ms settling window is intact.
Server first-frame normalization is unchanged and remains compatible with older v2
recordings. An old beta28 recording whose early gameplay was never captured cannot
gain that missing history: create a NEW beta29 VOICE RECORD for parity QA.

Production late join returns at most the last 3000ms / 72 server packets. The
unchanged client presentation buffer remains 64 samples / 2000ms with measured
100–350ms headroom. It cannot reconstruct an arbitrary earlier 0→60% Attempt from
that recent window. A first sequence >0 logs partial world history once per context.
The driver does not sweep an invented 0→60% trajectory or run unbounded warm-up.
Whether GD's native position-trigger cursor catches up historical X triggers, and
which timed/spawn/touch state is still absent, remains a device experiment. No
full-history warm-up or complete mid-Attempt parity is claimed. If it is absent,
Phase 5 needs an explicit bounded authoritative history/state seed, rather than
pretending the current recent buffer contains it.

## References and license

[AttemptPlaybackMod PlayerObject hook](https://github.com/manshoety/AttemptPlaybackMod/blob/a7dec99d20642f5c425954ce7839b240c9a06302/src/hooks/hook_player_object.cpp)
was inspected for replay position application, native setPosition sequencing and
its follow-player compatibility observation. No LICENSE file was present in the
inspected repository tree, so no source implementation was copied. Corum's scoped
world driver and native history sampler are independent implementations.
[Globed](https://github.com/GlobedGD/globed2) remains the reference for remote
lifecycle, timestamp interpolation and scoped native camera targeting. The visible
renderer remains separate; its existing MIT companion-inventory attribution stays
packaged. No trigger replay engine, background protocol, WebSocket or DB redesign.

## Validation and pending device checklist

Local: 67 native/source regressions, seven strict C++ domain executables, 114 rules
tests and 163 server tests pass. Workspace typecheck/build and both Apps Script
validators pass. The new native test compiles/executes the ACTUAL production world,
camera and player hook bodies and complete driver/remote/camera with observable
native doubles in Debug ON/OFF. It checks Cube→background/follow/camera→Spider
gravity snap→later background→terminal, no sweep, one sample/native camera/final
correction, native-write rejection, stable remote lifecycle and 60/120/240Hz.
Existing real HTTP/PostgreSQL-backed Debug recording/playback, interpolation,
teleport, local safety, normal Ranked/Placement/Practice/FINAL/LAST/Death Match and
audio regressions remain. Automatic native CI status is reported with the commit.
Release workflow is not manually invoked.

Post-fix iPadOS was not available. Next QA must create a new RECORD and compare TEST:

- Initial world-seed log: native elapsed/position/camera are gameplay-ready, before
  any unnecessary motion wait; no lost first 1–2s of native trigger history.
- Follow Player and Follow Player Y objects track remote progression; background,
  color/move/spawn/pulse/alpha/ground presentation matches RECORD at the same time.
- Debug world-driver log (at most 24 initial/mode/gravity/snap transitions): remote
  and driver coordinates/mode/gravity agree before/after world; publisher and final
  native camera agree. Spider destination is exact, hidden dummy never appears,
  no fake sweep, remote recreation, camera stop/flash or snap-only BUFFERING.
- Renderer/world/camera remain smooth on one timeline; VOICE music only; terminal
  holds; New Best/percentage/Attempt evidence stay unchanged; exit audio restores.
- Real production mid-Attempt join: observe historical X/touch/spawn/camera/colored
  state catch-up separately, rather than concluding parity from a moving renderer.

Remaining Phase 5: device-proven native trigger parity and late-join world seed,
full Dual/P2, cosmetics, complex camera zoom/rotation/static/platformer parity and
bounded reconnect/recovery polish. Phase 6 networking/DB optimization is untouched.
