# iOS ordinary Practice crash isolation (beta.30 Stage 2 diagnostics)

This branch is diagnostic only. Main remains `58d643d21734243e2f3a45e3006d27f178cad83e`, `v0.5.0-beta.30`.
There is no beta.31 root-cause fix or device stability claim yet.

The user reports SIGABRT through allocation / `std::vector<tulip::hook::Handler*>::__append`
and `Pool::getCommonHandler`, with an animation callback later in the stack. On the same
iPad/GD/map/mod set, disabling only Corum removes the reproduction. This implicates Corum
as a trigger or necessary condition, without proving a specific hook body corrupts memory.
This is separate from the earlier Placement `saveNewScore` SIGSEGV.

The two named crash report originals have not been received in this workspace. Signal/thread,
addresses, registers, loaded versions/mod lists and per-report scene details cannot yet be
independently compared. Required originals:

- `2026-10-06_07-56-18.626275.log`
- `2026-10-06_05-01-27.095985(1).log`

## Fixed device A/B procedure

Install exactly one `.geode` variant at a time, replacing the same `hwanhee1.corum_ranked`
mod, then fully restart GD. Keep iPadOS/GD 2.2081/Geode 5.10.1, level, Practice mode and
every other enabled mod unchanged. Keep the original Corum-OFF control as well.

For each control/variant, try at least five separate runs with repeated restarts and
the same animation-heavy section. Include a continuous run of at least ten minutes.
Record elapsed time/attempt count to crash or the completed exposure period. A single
non-crash does not establish stability. Save the full startup log (`[CorumCrashDiag]`
variant/hook registry) and any crash report for every failed run.

## Stage 1 completed device evidence

The user tested the six artifacts on the same iPad / Monopole / Practice / other-mod
set. These are authoritative user results; CI success was not used to infer them.

| Stage 1 artifact | Excluded group | User device result |
|---|---|---|
| [baseline-debug](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999/artifacts/11401586763) | none | **CRASH** |
| [no-probes](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999/artifacts/11400754090) | A | **CRASH** |
| [official-debug-off](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999/artifacts/11401576551) | official Debug instrumentation absent | **CRASH** |
| [no-probes-player](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999/artifacts/11401695888) | A + B | **NO CRASH** |
| [no-world-persistence](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999/artifacts/11401237624) | C + D | **CRASH** |
| [no-gameplay](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999/artifacts/11401561675) | E | **CRASH** |

Every crashing variant failed at the same Monopole section, around 4%, where Robot
jumps on a yellow orb (**Repro A**). The decisive comparison is no-probes versus
no-probes-player: with A identically absent, removing B changes CRASH to NO CRASH.
B is the strongest discriminator and a strong reproduction condition. An individual
hook, direct hook-body corruption, invalid uniqueIndex, OOM, count/ordering pressure,
legacy/new cohort or other-mod interaction has not been proven. The reported NO CRASH
has no supplied exposure count/duration; it is not complete stability acceptance.

The user separately observed a **Clear entry crash (Repro B)**. Keep its runs/logs
separate from Monopole 4%. Its Tulip/SIGABRT signature is unconfirmed; compare signal,
thread, vector __append, Pool, GeometryJump, animation callback, exact Geode/Tulip
UUID/offsets and Clear-specific PlayLayer/EndLevelLayer frames using
[the report comparison table](IOS-PRACTICE-CRASH-AUDIT.md#stage-1-device-results-authoritative-user-evidence).
Do not equate it with Repro A or the earlier Placement saveNewScore SIGSEGV.

## Stage 2 compiled-out comparisons

All eight comparison artifacts use Debug ON and exclude A/native probes. C, D, E,
other Corum code, SDK, GD bindings, mod ID and beta.30 version remain identical inputs.
Only the selected B declarations/priority targets differ. No new native detours are
added (including animation/orb callbacks, allocators or Tulip internals).

Every function in this table is a PlayerObject detour:

| Artifact / .geode filename stem | B functions actually selected | Expected total Corum detours |
|---|---|---:|
| stage2-no-probes-all-player | update, updateRotation, spiderTestJump, setPosition, setRotation, setVisible, updateSpecial | 46 |
| stage2-no-probes-no-player | none | 39 |
| stage2-player-legacy-only | update, updateRotation, spiderTestJump | 42 |
| stage2-player-beta29-only | setPosition, setRotation, setVisible, updateSpecial | 43 |
| stage2-player-update-rotation-only | update, updateRotation | 41 |
| stage2-player-spider-only | spiderTestJump | 40 |
| stage2-player-transform-setters-only | setPosition, setRotation | 41 |
| stage2-player-visible-special-only | setVisible, updateSpecial | 41 |

The first two are Stage 1 no-probes/no-probes-player controls rebuilt from the same
Stage 2 source. Legacy is exactly the three pre-beta.29 hooks; beta29 is exactly the
four new global hooks. [player-hook-stage2.json](player-hook-stage2.json) pins all seven
iOS binding addresses, exact enabled/disabled options and CI selections.

Test in this order, fully restarting GD after each replacement:

1. stage2-no-probes-all-player (expected crash control, not a new result).
2. stage2-no-probes-no-player (expected non-crash control, not a new result).
3. stage2-player-legacy-only.
4. stage2-player-beta29-only.

Confirm startup `player-registry-match=true`, `priorityFailures=0`, `debugProbeHooks=0`
and the exact seven `player-selection` entries. A registry mismatch makes that run
an invalid comparison. Save the entire startup registry and per-scene crash originals;
record elapsed exposure/attempt count independently for Repro A and Repro B.
Do not declare stability from one non-crashing run.

| Legacy-only | Beta29-only | Next interpretation / device pair |
|---|---|---|
| CRASH | NO CRASH | Prioritize update-rotation-only vs spider-only |
| NO CRASH | CRASH | Prioritize transform-setters-only vs visible-special-only |
| CRASH | CRASH | Both subsets may independently reproduce, or share another interaction/pressure mechanism; no single-hook conclusion. Subdivide each. |
| NO CRASH | NO CRASH | Old/new interaction or full-B count/registry pressure becomes stronger. Next prepare full-B single-removal complement tests and check registration order/count. |

A stable subset-only package does not exonerate that subset: complement removals
from full B ask a different question. If either control no longer reproduces its
Stage 1 outcome, recheck conditions and controls before interpreting the splits.
The four subdivisions are built in advance so only the implicated cohort's pair
needs the next device round-trip.

CI also compiles `compile-check-beta30-debug-on` and `compile-check-beta30-debug-off`
with CORUM_CRASH_DIAG=OFF and every exclusion OFF. These are default production-source
compile checks, not additional Stage 2 comparisons or a new official release.

Excluded safety groups deliberately make these packages unsuitable for Ranked,
Placement, RECORD/TEST or Live Spectator. Use them only for ordinary Practice isolation.
They reuse beta.30's exact mod ID/version and do not change the client/server gate or
protocol v2. The label/provenance JSON and startup log identify each variant.
Build SDK stays 5.8.2, as declared by the unchanged beta.30 manifest; device runtime
stays Geode 5.10.1. The two versions are distinct. All variants pin the same SDK,
GD bindings and build action, so those inputs do not change between A/B packages.

## Installation boundaries and regression

Stage 1 A/B/C/D are excluded by preprocessing the entire corresponding `$modify` declaration.
Stage 2 additionally excludes individual B method declarations and their priority
names before generated modify registration, retaining the original whole-B switch.
E keeps the PlayLayer type/Fields needed by other groups, but clears public
`ModifyBase::m_hooks` in `onModify`, before Geode's `Mod::claimHook` loop. At that point
`Hook::create` has allocated metadata only; `Hook::enable` / Tulip `createHandler`
has not run. This is not a runtime `disable()` experiment.

No new native hooks are added for diagnostics. Startup reads public `Mod::getHooks`,
target names/addresses/enabled state/priorities once. Practice entry logs saturate at
three per instrumented function. Priority failures are counted rather than concealed.

The inventory and tests pin the existing seven iOS PlayerObject detours, three core
GJBase detours and two persistence detours, and prove Debug OFF/probe-excluded source
contains no native probe modify classes. Preprocessed default Debug ON/OFF runtime
function bodies/Fields match the authoritative beta.30 snapshot. Existing executable
capture, native snap, shared timeline, world-driver/camera, persistence and Placement
stale-state regressions still run. They are host doubles, not iPad native execution.

See [hook-inventory-beta30.json](hook-inventory-beta30.json) for each target's GD 2.2081
iOS binding and pre-beta.29 presence. Exact Geode/Tulip source, ARM64 and ordinary-path
audit findings are being added separately; allocation, corruption and hook-count
causation remain unproven. Stage 1 results implicate B; Stage 2 results and original
report/register evidence are still needed to isolate the cause.

## Stage 1 completed artifact validation

[CI run 37441228999](https://github.com/ybaf100/Corum-Ranked/actions/runs/37441228999) passed native/source regression plus all six iOS arm64 builds.
All packages passed mod ID/version/native binary and embedded Corum modify-marker
exclusion checks. Each artifact contains the labelled .geode plus SHA-256 provenance.
Artifact code commit: `4c455b89cbdd5982c0016a2e46aa035d76f868c8`; later documentation
commits do not change those sources.

Local validation: 73 source/native callback tests and 8 strict C++ executable suites
pass, including Placement final-clear persistence/stale-state, native capture/snap,
world/camera ownership and spectator safety. These fixtures do not run native GD on iPad.
The assistant has not performed device gameplay. The user has completed Stage 1
A/B above. Root cause, minimal causal hook set, Stage 2 results and ten-minute stability
acceptance remain unverified; do not call these builds an iOS crash fix.

See [the full forensic/source audit](IOS-PRACTICE-CRASH-AUDIT.md) for the report comparison
placeholders, exact released Tulip ARM64 offsets, register caveats and hook audit.

## Stage 2 build verification

The diagnostic workflow builds eight iOS arm64 comparison packages plus the two
production-default Debug ON/OFF compile controls. It derives the matrix from the
selection JSON and validates the full set of native SDK modify markers, not merely
update/setPosition or imported binding names. Unexpected native detours, any missing
selected B detour or any retained excluded B marker fail the job. Production compile
checks also require all `[CorumCrashDiag]` code to be absent. Startup independently
checks realized public Mod::getHooks registration and enabled counts; CI binary checks
do not substitute for that device check.

Each Actions artifact contains `<artifact-name>.geode` and diagnostic-provenance.json,
including source SHA, package SHA-256, exact enabled/disabled B targets, compile
exclusions, SDK/runtime/bindings and complete verified native target list.
No beta.31, protocol/schema/gate change, main merge or manual Release is part of Stage 2.
The existing Placement final-attempt/persistence/stale-state, native Spider/Teleport
capture, segment deque, World Driver crossing/Follow-Y, camera ownership, polling,
Safe Mode, Debug RECORD/TEST and wire-version regressions remain in place.

Stage 2 CI run and artifact download links are recorded below after the build finishes.
Stage 2 device outcomes remain to be supplied by the user.
