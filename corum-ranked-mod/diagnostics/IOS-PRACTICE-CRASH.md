# iOS ordinary Practice crash isolation (beta.30 diagnostics)

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

## Device A/B procedure

Install exactly one `.geode` variant at a time, replacing the same `hwanhee1.corum_ranked`
mod, then fully restart GD. Keep iPadOS/GD 2.2081/Geode 5.10.1, level, Practice mode and
every other enabled mod unchanged. Keep the original Corum-OFF control as well.

For each control/variant, try at least five separate runs with repeated restarts and
the same animation-heavy section. Include a continuous run of at least ten minutes.
Record elapsed time/attempt count to crash or the completed exposure period. A single
non-crash does not establish stability. Save the full startup log (`[CorumCrashDiag]`
variant/hook registry) and any crash report for every failed run.

First compare `baseline-debug`, `no-probes`, `official-debug-off`:

| Artifact suffix | Debug harness | Excluded group | Device result |
|---|---|---|---|
| baseline-debug | ON | none | pending |
| no-probes | ON | A: native instrumentation only | pending |
| official-debug-off | OFF | official Debug code absent | pending |
| no-probes-player | ON | A + B: native probes + PlayerObject | pending |
| no-world-persistence | ON | C + D: GJBase world/camera + persistence | pending |
| no-gameplay | ON | E: PlayLayer + Gameplay UX + EndLevel + app audio hooks | pending |

If removing A is stable, isolate its individual probe functions next; also compare the
original beta.30 Debug artifact to the instrumented baseline. If both first controls
still crash, compare A+B vs C+D and E. Split the implicated half into single groups
on this diagnostic branch. If both halves affect reproduction, repeat to distinguish
hook pressure from an interaction; do not name a single guilty hook from that result.

Excluded safety groups deliberately make these packages unsuitable for Ranked,
Placement, RECORD/TEST or Live Spectator. Use them only for ordinary Practice isolation.
They reuse beta.30's exact mod ID/version and do not change the client/server gate or
protocol v2. The label/provenance JSON and startup log identify each variant.

## Installation boundaries and regression

A/B/C/D are excluded by preprocessing the entire corresponding `$modify` declaration.
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
causation remain unproven until crash originals and device isolation results arrive.
