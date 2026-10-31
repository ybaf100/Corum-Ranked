# beta.122 investigation: release DAG, Entropie and CBF collision identity

Start main 5875c91cc3bf64b3eb1cfc3c019dcc5ef4f16b68, beta.121.
ZIP SHA256 e0cae449d02b8f46726abd1af216da5f29d4dd4dec52e0fbed4f7edd29383866.
All checksummed ZIP members verified. Original logs remain outside the repository.

| Evidence verified from original data | Code boundary | Conclusion / limit |
|---|---|---|
| Seven beta.121 failures, four Attempt IDs; five ROOT ROTATE failures at frames52/135/234/256, first difference before[9], raw bottom values213→214,109→123,166→170,2166→2599 | CBF motion() directly compares four lastCollision integers | Integer inequality is certain; actual collision-target equality is unproven. Never erase these checks or infer identity from object type alone. |
| Two restarts fail CBF_INITIAL_COLLISION_LOG_MISMATCH; all stored initial dictionaries have counts[0,0,0,0], initial four IDs-1 | resetReplayInitial validates empty collision logs before restoring the source initial state | Restore owned, fully validated empty initial dictionaries after Native reset; preserve strict validation and reject unknown/nonempty source initial contents. Actual Native restart remains unverified here. |
| Entropie seq1435→1436 is the same Attempt and same packet in two logs; another Attempt1438→1439 has a separate1395/1469-unit Ship relocation, eventNONE | Publisher Native capture→sampling→codec→viewer discontinuity gate | Proof is absent at the viewer; the source Native function and loss stage are still unknown. Add bounded source/event-path diagnostics; retain negative packet rejection. |
| beta.121 Native tests299s; mac Debug starts16:58:48, after tests finish16:58:43, and builds456s; mac OFF362s; Release mac402s | Debug build and OFF jobs need tests | Confirmed avoidable serial dependency. Parallelize preparation/builds while retaining all test/platform gates at publication/activation. |

Jobs/steps snapshots were re-fetched from GitHub API for runs41093892859,
41093893195,41093892958,41093893227 and41092839172. Baseline totals: Validate
325s, Release495s, Debug885s, deploy939s. Cache, compile, queue and publication
waits will be reported separately; no unmeasured speed target is a result.

The bindings expose int lastCollision fields, GameObject.uniqueID and Native
collision functions, but do not contain their proprietary implementation.
Runtime identity normalization must use actual collision dictionary/object
witnesses plus verified map identity/order and reject ambiguous/deleted objects.
Historical raw IDs without source target evidence cannot prove equivalence.

No actual GD/device is installed. Entropie positive fixtures must come from
actual recapture; explicit Native doubles validate contracts only. Preserve the
beta.120/121 input clocks, terminal journals, immutable prefix recovery and all
scoring/reward/history/badge policies.

## Implemented boundaries

### C: collision identity and owned restart

The new identity adapter does **not** infer that the historical integers are
UUIDs. A reference becomes comparable only after the actual Native collision
query, separately for each P1/P2 actor and direction, observes all of: an object in `collisionCheckObjects`, its unique runtime
`m_uniqueID`, the matching directional collision dictionary key, and the same
loaded map-array instance/index/definition. A unique runtime integer alone or
`m_objectID` alone is insufficient. Ordered original object definitions
(type, original position/rotation/scale, groups) form a SHA-256 manifest; the
existing official geometry hash is still checked. Runtime IDs/addresses are
excluded from that manifest. References retain `[mapIndex, objectID]`, backed
by the full ordered manifest. Ambiguous duplicate definitions/IDs, changed
order, deletion/recreation and unobserved keys fail closed. Retained object
references prevent pointer reuse during the owned Scene lifetime and retire
on Scene exit/reset. A new official Source Attempt also discards every prior
identity witness without clearing live Source dictionaries.

All four directional targets remain in CBF Root/cursor comparisons; the raw
values remain in diagnostics, alongside bounded Native witness records. All
coordinates, rotation, velocity, holds, grounding, input, clock and RNG checks
remain. A different actual target yields `CBF_COLLISION_TARGET_MISMATCH` even
when two independent Native-double bodies happen to yield identical poses.
Initial dash/slope/ground/collided/portal references still compare their exact
map index/type and are additionally covered by the new full map manifest.
Nonempty/unresolved initial reference state is never guessed or overwritten.

After a completed owned Native reset, the recorded **empty** P1/P2 collision
dictionaries are restored, then strictly checked before the existing complete
initial physics snapshot is applied. The helper does not erase the four live
scalar IDs to pass a frame. It logs before/expected/after dictionaries and raw
IDs. Nonempty or unknown source logs, foreign Scene ownership and unavailable
dictionaries are refused before mutation.

Format v1 / Native input v2/v3 remain readable. New CBF records add optional
object manifests, semantic references and diagnostic witnesses. Historical
numeric collision files without a source target witness cannot safely be
upgraded from logs alone. They remain on disk/Best, may validate their supported
prefix, and stop explicitly at unprovable identity; the HUD states `COLLISION
IDENTITY UNAVAILABLE`. File parsing/storage/receipt compatibility remains
separate from native playback capability. No initial-state values are invented.

Tests use the production adapter/root/cursor and an explicitly independent
Native movement double, including same target/different runtime ID, a different
same-kind target, missing witness, duplicate definitions/IDs, deletion/recreation,
reordered maps, 280 frames through termination, P1/P2, Cube/Ship, 0.5/1/2/4x,
pause, forward/backward Seek, hook lease failure/restoration and three complete
restarts with stale dictionaries. They are not GD/CBF binary reproduction.
Robot/slopes/platform/orb/portal Native behavior and the four historical Attempts
remain unverified on actual GD.

### B: Entropie evidence capture, not fabricated movement

`SpectatorSourceTrace` records only level143308401, Native time70–73s,
X25800–26000. Per Attempt it limits normal observations to160, critical
observations to32 and ordinary per-path sampling to50ms. It records completed
world/Player updates, collision queries, portal callbacks and setPosition as an
**observation only**, with before/after, velocity, mode/gravity, lastPosition,
teleport flag including latched true, object IDs, Native step, world frame and
callback order. No additional trusted event type or relocation is created.

Capture reports explicit `INVALID_BODY`, `SPIDER_MODE_OR_GRAVITY`, `CHAIN_GAP`
and `INVALID_EVENT_OR_CHAIN_LIMIT` outcomes. Existing eight-event limits,
contiguity and strict movement validation remain. Valid events may carry an
optional bounded diagnostic identity (event/frame/step/time/callback), which
never authorizes a snap. It round-trips through the real shared Native codec,
server parser, Debug recording persistence/reload and live relay. P1/P2 remain
independent. Source callbacks identify the recording/Attempt, and bounded
QUEUED/ENCODED/UPLOAD/RELAY/RECEIVED logs correlate its generation, sequence and
presentation epoch. Debug RECORD's separate upload/persistence path has its own
CAPTURED/RECORD_ACCEPTED/STORED/LOADED diagnostics. Failed decode is reported,
never silently converted to an event-free pose.

The two original packet movements are negative fixtures in the production
viewer decoder. Missing proof still retains the last valid pose and requires
recapture without pretending there is a disconnected session or repeatedly
resetting the same immutable stream. Existing presentation segmentation, P2
isolation, Follow/world/camera/audio ownership and fresh Attempt/epoch recovery
remain unchanged and covered by their regression tests.

**Unconfirmed:** the exact Entropie Native relocation function, a missing flag
edge vs missing callback vs sample/transport loss, and a positive actual-game
fixture. No GD/device is installed here. The protocol fixtures are labelled
synthetic and do not count as positive Entropie reproduction. The user's B4
explicitly permits this bounded diagnostic release until actual recapture is
available. The 400-unit rejection threshold remains unchanged.

### A: explicit release dependencies and provenance

| Stage | Before beta.122 | beta.122 |
|---|---|---|
| Debug tests/build/OFF | Native tests precede all builds | metadata precedes independent tests and all five ON/five OFF builds |
| Publication | separate build runs; Debug OFF gate, no shared full-server dependency | coordinator requires full server/rules/PG/soak validation and both complete build workflows before either channel publishes |
| Artifact inputs | fixed upstream `geode-build-*` names, isolated by run | explicit `corum-platform-{release,debug}-{target}` names, exact channel-specific download and five-platform validation before the same Geode CLI merge operation |
| Server image | separate deploy run, then up to2400s package polling | image prepares in parallel; activation depends directly on both successful publications and rechecks public bytes with no polling |
| Source identity | version/channel only | descriptor + embedded package SHA agree with server image/health SHA and current main |
| Activation | workflow-wide production serialization | only the activation/verification job holds the production lock, cancellation disabled; recheck main after upload, skip superseded source, verify an already active source without restarting |
| Development | heavy full checks only | scoped quick typecheck/Source feedback plus independent full gates, manual full validation retained |

Main pushes are owned by `ranked-release.yml`; duplicate standalone children
skip metadata intentionally. Manual Release/Debug publication retains full
Native/server validation; manual deploy/check-only remains. `[build only]`
continues to suppress R2 only, `[no-deploy]` suppresses automatic activation,
and publish/deploy flags and existing variables retain their meanings. The
Singapore pinned SSH target/environment and existing host rollback entry point
are unchanged. No provisioning, Secrets or server.env changes are made.

The Geode builder is pinned to the **already used** beta.121 commit
`a0f7676e6fcd1a584f6d0c1c800239008412defc`, CLI binary v3.9.0, bindings
`70986055f526da15318779703f0e904be822f162`, SDK from the exact manifest5.8.2,
source-built Codegen unchanged. The channel merge setup helper is pinned to
`9e3c4a0657fca324e0d39d10375b67e01657b775`. No blanket upgrades. Note that the
upstream pinned builder internally references its CLI setup helper at main;
the requested CLI binary version is pinned, but this is not a claim that every
transitive third-party action has been vendored or pinned here.

Official source caching stays enabled. Compiler cache entries use a partition
including SDK/bindings/OS/target/config/ON-OFF/ABI workflow+CMake inputs; compiler
content hashing still applies. Source-only changes do not invent a new full
commit cache namespace. macOS compilation caching remains off: the official
multi-architecture path would download an unverified custom nightly sccache.
Baseline macOS CPM source cache restored27MiB in approximately0.8s; that is not a
compiled-code cache hit. New-run hit/miss and elapsed times must be measured
from actual Actions jobs, not inferred from a green build.

Pinned-checksum actionlint1.7.7 and production-DAG tests enforce missing-channel,
failed-tests/OFF, mixed-SHA, stale-source and marker guards. These checks run in
full CI; the quick feedback path does not replace any release gate.
