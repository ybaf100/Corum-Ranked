# v0.5.0-beta.30 — persistence, snap timeline and HTTP viewer hotfix

Starting remote main: `5be30c1c72ff5c222233f8bbe2232820dfd4b41f` (beta29).
Spectator protocol **2** and Debug recording format **2** are unchanged. No
server transport schema, scoring rules, Placement timer rule or Web project
changes. The version manifest, Ranked workspace/lock metadata and exact server
client gate are synchronized to beta30.

## Placement persistence and stale state

The supplied iOS arm64/JIT-less stack faults in
`CorumRankedSpectatorProgressSave::saveNewScore`, on the real completion path
through `GJGameLevel::storeNewLocalScore` and `PlayLayer::levelComplete`. The
global seven-argument `gd::string` by-value detour is removed. The GD 2.2081
binding exposes `storeNewLocalScore(int,int)` (iOS `0xacf54`) and the narrower
detour blocks only `this == spectatorScene->m_level`. Genuine non-Ranked,
Ranked, Placement and Debug RECORD gameplay still delegates to vanilla.

This avoids that nontrivial argument thunk; it does **not** establish the exact
ABI failure mechanism or prove the supplied device crash fixed by execution.
There is no CBF/server attribution. `savePercentage`, local completion, New Best,
attempt-count/death guards and Ranked transport suppression remain independent
firewalls. The store boundary alone is not assumed to cover every persistence
entry point or achievement path.

The accepted final Attempt remains active after Main Time and the extra window
expire: `MAP_PLAYING -> MAP_SETTLING -> same Attempt Clear -> MAP_RESULT`.
The PostgreSQL integration regression rejects a new Start after expiry, permits
that active UUID's 100% Clear, retries its identical End event idempotently and
checks one ended persisted row. The host hook fixture checks actual production
Clear/store callback bodies for normal local, Ranked, Placement and spectator
scenes. These host doubles do not execute GD or prove iOS ABI correctness.

Placement End ACK captures and remembers its exact UUID on accepted or
`ATTEMPT_ALREADY_ENDED` responses. `parsePlacementState` rejects an entire
snapshot containing that closed UUID, including stale score/result/timer fields,
and rejects older same-run stateVersion. A distinct active UUID at the same
version still restores normally. UUIDs remain remembered for the runtime
lifetime so an old in-flight response cannot revive an evicted tombstone.
`PlacementRuntimeNativeTest.mjs` executes production Start/End callback and
state-decoder bodies under `GET starts -> End ACK -> late GET response` ordering.
One Start and one logical End per Attempt are preserved; transport retries keep
the existing event ID and server deduplication semantics.

## Spider and teleport segments

Old behavior: every verified snap cleared the complete interpolation deque and
jumped the rendered clock to packet arrival's destination. A frequent Spider
click therefore discarded accumulated delay/headroom and made later starvation
likely, including samples earlier in the same HTTP response.

New behavior: each buffered sample has a validated client `segment` ordinal.
It increments on a verified boundary; it is not serialized or another copy of
wire `pose.discontinuity`. A/B remain in segment 0 and C/D/E remain in segment 1.
Normal interpolation runs A->B, holds B until C's timestamp, switches **all**
position/rotation/camera/mode/gravity/mini/flip state to C, then resumes C->D->E.
A same-timestamp terminal inherits the destination segment. Every packet in a
batch remains queued, subject to existing time/size limits and normal pruning.

`SpectatorRemoteFrame.presentationSegment` carries that client ordinal through
the shared renderer/camera frame to `SpectatorWorldDriver`. Only native crossing
and Follow Player Y history reset at a new rendered segment. A stalled render
frame that lands after C still detects the segment and cannot sweep source to
destination. Native `updateSpecial` remains sampled on publisher time in 10ms
steps, with the beta29 Windows inline compatibility path and full physics block.

Regressions cover single/multiple-in-batch snaps, teleport evidence, discrete
state, same-timestamp death/Clear and at least 26 repeated 150/200ms Spider
boundaries at 60/120/144/240Hz. The original 110ms RTT + 80ms viewer gap fixture
has zero starvation, no snap clock reset, no remote identity change, unchanged
stream generation/epoch and persistent camera ownership. These are deterministic
host results; actual Spider animation/camera smoothness on iPadOS is unverified.

## Native trigger investigation: device work remains

The beta29 world update already calls `GJBaseGameLayer::update`, while exact
dummy `PlayerObject::update` is suppressed. It is **not established** that an
additional `checkCollisions` call is missing. Official 2.2081 bindings provide
native addresses/signatures, not the bodies/caller graph for these functions:

- `checkCollisions(PlayerObject*,float,bool)` iOS `0x1e72e8`
- `collisionCheckObjects(PlayerObject*,gd::vector<GameObject*>*,int,float)` `0x1e7f38`
- `canBeActivatedByPlayer` `0x1e8d08`, `playerTouchedTrigger` `0x1e92dc`
- `checkSpawnObjects` `0x1ebe3c`, `processCommands` `0x1ff4f8`
- `processPlayerFollowActions` `0x1f85b8`, `processDynamicObjectActions` `0x1f7180`

`playerTouchedObject` is inline on iOS/Windows: its supplied body registers an
activated-object/player pair in `m_gameState.m_activatedObjectIDs`; it is not
itself a trigger replay engine. `getMoveTargetDelta` directly reads native
players' real positions. Saved-position/follow work reads `m_lastPosition`.
Portal helpers can mutate mode/gravity, activated portal and native camera state.
Consequently an unconditional extra collision pass could duplicate activation
or execute rings/pickups/damage/portal side effects. No such pass is added.

Debug TEST now scopes pass-through probes to its exact scene/player, logs at
most one window per second and 120 windows per scene, and observes blocked
PlayerObject update, collision, object/touch trigger, activation query/result,
teleport, Spawn, commands, player follow, dynamic action and base
`EffectGameObject::triggerObject` calls. Repeated base-trigger calls within one
frame are counted; legitimate multi-trigger semantics must be checked before
interpreting them as duplicates. Specialized trigger subclasses may bypass the
base probe. Unsupported iOS/Windows inline touchedObject detours are excluded;
their native activated-ID map size is included in the log. Official Debug OFF
contains no instrumentation hooks.

The host native hook probes preserve original results and delegate once. The
world fixture checks continuous crossing, snap destination history, later
position trigger, background/follow ordering and separation of the visible
remote player, with zero local physics/result/persistence authority. Its native
world observer is a **synthetic ordering double**, not GD's trigger engine.

**Player-position/touch/background world parity is not claimed fixed.** Actual
missing interaction and the minimum native restoration need Debug TEST traces
from GD 2.2081. Compare nonzero commands/follow against checkCollisions/object
and touch counts before adding a pass. This beta delivers the safe instrumentation
and segment crossing fix, not an unverified physics workaround.

Binding evidence: [geode-sdk/bindings 2.2081 at d641858](https://github.com/geode-sdk/bindings/blob/d641858f3e1cd2c6d86013c799f224ebdb8ec1cd/bindings/2.2081/GeometryDash.bro).
The official inline `GJBaseGameLayer`/`PlayerObject` helpers were also inspected.
AttemptPlaybackMod was an architecture reference only; no source was copied.

## Transport: Debug TEST versus production

Viewer HTTP stays serialized with one in-flight request. Gaps are selected
**after** the response is decoded, using the current buffer and RTT:

| State | New ACK-to-next-request gap |
|---|---:|
| Healthy, sufficient headroom | 40ms |
| Healthy, headroom below delay budget | 20ms |
| Starving or <= RTT+20ms refill headroom | 5ms |
| Waiting or rendered terminal | 100ms |
| Valid response in fallback | 150ms |
| Consecutive network errors | 150/300/600/1000ms, capped |

Old gap was 80ms after ACK. A simulated constant RTT of 110ms verifies arrival
intervals of 150/130/115ms versus 190ms; continuous 50ms published samples would
average approximately 3.0/2.6/2.3 packets per response versus 3.8. These figures
are calculated/host timing results, **not new iPadOS measurements**. Actual RTT,
arrival, packets/response, delay and starvation remain in the bounded Debug
buffer metrics, together with the selected poll gap. HTTP transport and publisher
cadence still set practical latency limits. Faster polling is not the Spider fix:
the snap stress also passes with the unchanged old 80ms gap.

Production `publish()` is still **ACK-bound**:
request -> busy -> ACK -> next eligible at ACK+50ms. At RTT=110ms its effective
cadence is 160ms/sample, not 50ms. The executable production publisher callback
test fixes five successive sequence/timestamp values at 0/160/320/480/640ms,
forbids a second in-flight POST and preserves terminal ordering. No publisher
parallelism, batch schema or WebSocket redesign is included.

- Debug RECORD: actual gameplay -> independent 50ms capture -> bounded HTTP batch
  upload -> server -> PostgreSQL recording.
- Debug TEST: PostgreSQL -> server playback clock -> production relay packet
  shape -> HTTP viewer -> shared interpolation/remote/driver/camera path.
- Production: actual player -> ACK-bound HTTP publisher -> server memory relay
  -> HTTP viewer. Smooth TEST alone cannot establish production smoothness.

Phase 6 should examine persistent push/batching with ordered sequence and Attempt
generation semantics. Phase 5 still includes actual world parity/native late-join
history, Dual/P2, cosmetics, complex cameras/reconnect and device stability.

## Validation and device checklist

Run the strict C++ domain suites and all Node native/source regressions, including
new Placement, native instrumentation and viewer polling tests, plus server
PostgreSQL tests, workspace typecheck/build and version compatibility. Automatic
Debug CI builds Windows/iOS/Android and runs both Debug ON/OFF host variants.
Do not manually execute the Release workflow.

There is no iPadOS device available in this execution environment. Final device
acceptance remains: timer-zero final Attempt Clear saves/transitions without
crash, repeated Spider snaps without starvation/camera divergence, genuine GD
position/touch/background/follow triggers, and zero spectator local results/saves.
CI or host doubles alone do not satisfy that acceptance condition.
