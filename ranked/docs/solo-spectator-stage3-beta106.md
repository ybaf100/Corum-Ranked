# Solo live spectator Stage 3 — beta.106

Baseline: authoritative remote main `8de578ccb328621517665ad4f5c1ab589ca14177`
(beta.105), freshly fetched on 2026-10-26. The user's beta.104 reference was
older than the deployed Stage 2 source. Stage 2 history, camera, source clock,
network recovery, authority handoff and all read-only guards are retained.

## Implemented production paths

### Dual poses and independent renderers

Optional protocol-v2 `keyframe.player2` carries a complete second body: position,
rotation, mode, Mini, gravity, flip, visibility, dead/finished, observed visual
emission mask and native discontinuity endpoints. Both bodies belong to one
packet's publisher elapsed time, sequence, Attempt, generation and viewer epoch.
Single-player v2 packets remain valid. A partial/recursive body, invalid mode,
NaN/infinity, oversized coordinate, conflicting terminal state or invalid visual
mask is rejected by both the HTTP parser and client decoder. Legacy dual-only
packets cannot silently enable P2; the viewer reports DUAL UNSUPPORTED.

`CorumRemotePlayer` keeps a stable primary renderer and lazily owns one independent
secondary renderer. Neither is registered as native m_player1/m_player2, an input
owner or collision object. Mode, icon, gravity and Mini changes are conditional,
not per-frame object recreation. P2 is removed on Dual exit and both avatars,
companions and snapshots are released on context/scene teardown. Tests repeat
single/Dual transitions and drain retained/autorelease ownership to zero.

P2 Teleport/Spider boundaries use an independent presentation segment. They do
not discard P1's native crossing history. Both bodies remain on the same
publisher clock; P2 is never interpolated through a discontinuity.

**Dual rendering is distinct from complete Dual world parity.** The verified
P1-only WorldDriver/filtered trigger query is preserved. Native P2 is hidden and
its physics, writes, collision queries and death callbacks are suppressed only
under spectator safety. A remote P2 never gains trigger authority. Player2-only
activation semantics have not been certified, so a Dual stream is explicitly
PARTIAL SYNC (`DUAL_PLAYER2_TRIGGERS_UNVERIFIED`). No engine pointer/checkpoint
serialization or guessed native-memory layout was added.

### Actual native cosmetics

A bounded immutable cosmetic descriptor carries all eight mode icon IDs, primary,
secondary and glow RGB, glow enablement, and native trail resource ID for each
avatar. The source is the publisher's own GameManager mode selections and its
actual PlayerObject colors/glow, not the viewer's account or a username lookup.
Descriptors are sampled at most once a second and revisions change only when
values change. Standard GD Dual uses the same selected mode icons with each
native player's own colors/glow; arbitrary third-party icon replacement is not
serialized.

Descriptors are emitted once per revision/generation, outside ordinary pose
keyframes. Existing immutable HTTP batch/ACK retries preserve their bytes. The
relay retains a validated descriptor with the exact stream and supplies it on
reset/revision mismatch; `cosmeticRevision` prevents repeated subscriber copies.
History carries sparse descriptor changes on their original sequence. Client
samples share immutable descriptor ownership, so a future appearance does not
replace older historical poses. New Attempt/generation resets the cache. Missing
or late descriptors retain the previous safe/default appearance without resetting
the WorldDriver. Server RGB/IDs/revisions have strict type and size limits; actual
native mode/trail resource counts enforce local fallback before applying an ID.

Native PlayerObject frame, mode, color and glow functions are reused. Unchanged
appearance/mode does not reload a sprite or re-run mode initialization. P2 uses
its own descriptor slot. Debug RECORD keeps sparse descriptors inside the existing
JSONB keyframe, and TEST relays them through the same parser/render path. Old
single-player format-v2 recordings remain compatible; no DB migration is needed.

### Effects and ownership

Native Cube/Ship motion streaks and Wave HardStreak are driven manually by
validated publisher time, never PlayerObject::update or actual gameplay input.
Native observed emission bits control ground, trailing, Ship/UFO click, Robot,
Swing, landing and dash emitters; no input-dependent condition is invented.
Companions are detached into the owned effect root, unscheduled and have native
actions stopped. Emitters have finite imported capacity; oversized modded
presets are refused. Wave history is bounded at 128 points. Ghost presentation
uses eight retained sprite snapshots per avatar with source-time fade, without
unbounded GhostTrailEffect actions/callbacks.

Duplicate/held timestamps, Pause and buffering create no additional samples or
particle updates. Discontinuity, large time gap, death/Clear and teardown reset
trails/emissions; history catch-up explicitly disables effects. A new Attempt
cannot inherit old trails. Glow is native icon art and independently follows the
cosmetic descriptor. Exact native particle RNG, arbitrary third-party presets,
full composite ghost shader output and every native effect parameter are not
synchronized. This is bounded visual reconstruction from observed emissions,
not a claim of pixel-identical particle playback.

### HUD, update branch text, and cost

Spectator name/percentage, stream state and round/time are separate persistent
labels. Percentage uses the rendered pose's source percentage; buffering does not
replace it with a newer unrelated server progress sample. WAITING, SYNCING,
LIVE, BUFFERING, RECONNECTING, PARTIAL SYNC, CLEARED, DISCONNECTED and unsupported
legacy Dual are distinguishable. Existing changed-text checks and the 100ms HUD
refresh are retained; no per-frame HUD node rebuild. Long names/status and the
round clock have bounded width, and panel placement updates with viewport size.

Update channel selection now invalidates its display key immediately, renders the
selected branch and retires old target/error/download metadata before refreshing
admission. Debug password verification is retained. No stale channel package can
be downloaded while the replacement metadata is pending.

Debug-only, rate-limited diagnostics record renderer/effect and HUD work time,
visual/companion counts, pose structure bytes, actual serialized response bytes,
RTT/arrival/buffer metrics and source sequence/time. Release omits these logs.
The server now copies bounded history once per accepted publish batch rather
than once for every packet in that batch; readers still use the existing capped
history/relay buffers and HTTP adaptive polling. No background DB polling,
checkpoint timer or new transport was added. A benchmark of representative
production-shaped JSON packets is recorded below; it is not a physical FPS or
resident-memory measurement.

## Debug middle-join testing from a chosen percentage

The Debug TEST popup accepts `TEST START % (0-100)` (up to two decimal places).
The server chooses the first stored native progress sample at or beyond the
requested threshold, preserving normalized publisher time rather than assigning
a new zero to that frame. Its clock starts at this existing elapsed point on the
first authenticated in-scene subscriber request. Earlier samples enter the same
bounded production history relay, and a nonzero-start Debug viewer requests
`worldHistory=1` and executes the existing native restoration path. The selected
threshold, actual source elapsed time and sequence are returned as metadata.
Large histories retain the existing PARTIAL SYNC/window-expiry behavior.

0% retains ordinary v2 playback. Legacy recordings without stored progress are
still playable from 0%; a nonzero request explains that a new recording is needed.
An unreached threshold and malformed/out-of-range inputs are rejected instead of
estimating progress from elapsed duration, level names or a final X coordinate.
Password, opt-in, session/scope tokens, expiry and read-only protections remain.
The isolated HTTP/PostgreSQL tests cover initial 50% join, complete prehistory,
continued original time, terminal/100%, invalid inputs, old recordings and
unreached progress; native tests exercise the actual runtime admission and
history-enabled viewer handoff.

## Verification and delivery policy

The user's latest instruction disables Actions and explicitly limits delivery to
code on main, ignoring workflow results. Accordingly this patch uses
`[no-deploy] [build only] [skip ci]`; no new R2 publication or Singapore activation
is requested. The deployed beta.105 and its immutable packages are preserved.
Actual local counts and the final source SHA are appended after completion. No iPad, Windows or Android physical game is available in this
environment. Native boundary doubles exercise production implementations and
lifetime/control flow; they do not certify native engine ABI rendering, trigger
parity, actual FPS or screen/touch behavior. Platform CI compilation and package
inspection are separate checks.

Production protection: no new migration, DB reset/delete, Neon configuration,
operating-setting, Secret or rollback change. Before/after read-only snapshots
compare existing accounts, complete profile digests, matches/Attempts,
achievement epoch/unlocks/AP and applied migrations. Only Singapore Lightsail is
activated after complete verification of both immutable R2 channels.

Stage 4 remains Survival survivor/POV selection. Outstanding technical limits
also include exact P2-only world triggers, complete transferable engine
checkpoints, arbitrary accumulated engine state and device-level particle/camera
parity. These are not represented as completed by this release.

Representative JSON packet sizes measured by the actual Stage 3 test fixture:
Single 339 bytes; Dual 527 bytes (+188); Dual with one descriptor 804 bytes.
These are one sample's measured JSON sizes, without HTTP headers, batch savings
or optional camera/event fields. They are not device FPS or resident-memory data.

## Changed files

- `corum-ranked-mod/changelog.md`
- `corum-ranked-mod/mod.json`
- `corum-ranked-mod/src/PlayLayer.cpp`
- `corum-ranked-mod/src/RankedPopup.cpp`
- `corum-ranked-mod/src/SpectatorLiveRuntime.cpp`
- `corum-ranked-mod/src/SpectatorLiveRuntime.hpp`
- `corum-ranked-mod/src/debug/DebugSpectatePolicy.hpp`
- `corum-ranked-mod/src/debug/DebugSpectatePopup.cpp`
- `corum-ranked-mod/src/debug/DebugSpectateRuntime.cpp`
- `corum-ranked-mod/src/debug/DebugSpectateRuntime.hpp`
- `corum-ranked-mod/src/domain/SpectatorCapturePolicy.hpp`
- `corum-ranked-mod/src/domain/SpectatorCosmetics.hpp`
- `corum-ranked-mod/src/domain/SpectatorInterpolationBuffer.hpp`
- `corum-ranked-mod/src/domain/SpectatorProgressPolicy.hpp`
- `corum-ranked-mod/src/domain/SpectatorPublishQueue.hpp`
- `corum-ranked-mod/src/domain/SpectatorRemotePolicy.hpp`
- `corum-ranked-mod/src/domain/SpectatorRestoreTimeline.hpp`
- `corum-ranked-mod/src/spectator/CorumRemotePlayer.cpp`
- `corum-ranked-mod/src/spectator/CorumRemotePlayer.hpp`
- `corum-ranked-mod/src/spectator/SpectatorCosmeticCapture.cpp`
- `corum-ranked-mod/src/spectator/SpectatorPlayerCapture.hpp`
- `corum-ranked-mod/src/spectator/SpectatorPoseCodec.hpp`
- `corum-ranked-mod/src/spectator/SpectatorWorldDriver.cpp`
- `corum-ranked-mod/src/ui/RankedModUpdateManager.hpp`
- `corum-ranked-mod/tests/DebugSpectateDomainTest.cpp`
- `corum-ranked-mod/tests/DebugSpectateRuntimeNativeTest.mjs`
- `corum-ranked-mod/tests/ModUpdateNativeTest.mjs`
- `corum-ranked-mod/tests/RankAssetPackNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorCameraOwnerNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorCaptureNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorLiveViewerNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorLocalSafetySourceTest.mjs`
- `corum-ranked-mod/tests/SpectatorNativeHookTest.mjs`
- `corum-ranked-mod/tests/SpectatorRemoteNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorRemoteSourceTest.mjs`
- `corum-ranked-mod/tests/SpectatorStage2NativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorVisibilityNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorWorldDriverNativeTest.mjs`
- `corum-ranked-mod/tests/helpers/SpectatorNativeDouble.hpp`
- `ranked/apps/server/package.json`
- `ranked/apps/server/src/debug-spectate/debug-spectate.controller.ts`
- `ranked/apps/server/src/debug-spectate/debug-spectate.service.ts`
- `ranked/apps/server/src/match/spectator-cosmetics.ts`
- `ranked/apps/server/src/match/spectator-live.controller.ts`
- `ranked/apps/server/src/match/spectator-live.service.ts`
- `ranked/apps/server/src/session/client-version.ts`
- `ranked/apps/server/test/debug-spectate.http.integration.test.ts`
- `ranked/apps/server/test/spectator-stage3.test.ts`
- `ranked/docs/live-spectator-protocol-v2.md`
- `ranked/docs/solo-spectator-stage3-beta106.md`
- `ranked/package-lock.json`
- `ranked/package.json`
- `ranked/packages/rules/package.json`

## Actual local validation (2026-10-26)

- Complete native Node regression suite after n% entry: **301 passed, 0 failed**,
  no skips. It compiles/runs production native adapters against observable SDK
  boundaries. Debug start-percent domain C++ test separately compiled with
  `-Wall -Wextra -Wpedantic -Werror` and passed.
- Shared Rules: **288 passed**. Complete server suite: **652 passed, 1 optional
  pool check skipped**. A subsequent actual HTTP/PostgreSQL + Stage 3 run passed
  **23/23**, including the new sparse-cosmetic cursor-recovery case (22 overlap
  with the full run, one additional regression). No tests were removed or weakened.
- Workspace typecheck and build: passed with beta.106 metadata. Localization:
  all **679 UI keys / 48 achievements** generated/validated. Existing public-client
  barrier unit tests: **4/4**; these are local policy tests, not publication.
- Native tests cover independent P2 modes/Mini/gravity, codec validation,
  discontinuities, repeated single/Dual teardown, unchanged local physics/saves,
  sparse appearance, native effect clocks, source-time restoration and actual
  Debug admission/viewer handoff. Debug HTTP tests use isolated persisted
  PostgreSQL and assert Ranked records unchanged. Sparse descriptor recovery
  also preserves appearance when a new relay generation begins at a later frame.
- Four platform Release/Debug builds, compiled beta.106 packages, GitHub Actions
  outcomes, R2 publication and production activation: **not run/checked**, per the
  user's latest code-only instruction. Physical Geometry Dash, real device FPS
  and exact P2-only trigger/effect parity remain **unverified**.

Delivery is one normal main ref update from the verified baseline, with
`[no-deploy] [build only] [skip ci]`. Existing beta.105 production/packages and
all operational data/settings are preserved. No workflow or deployment approval
request is needed or made.
