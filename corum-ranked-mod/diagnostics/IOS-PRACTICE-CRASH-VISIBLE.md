# PlayerObject::setVisible isolation and minimal visibility fix

Starting remote main: `58d643d21734243e2f3a45e3006d27f178cad83e`, v0.5.0-beta.30.
Starting diagnostic: `ed225350e359ff34aa41277a0a68d4455a0626c5`.
Changes stay on diagnostic/ios-practice-crash-beta30. Main, version/client gate,
protocol v2 and Debug recording format v2 remain unchanged until device acceptance.

## Authoritative device result

The user confirmed the individual Stage 3 outcomes:

| Package | B hook | User iPad result | CI packaged unique native targets |
|---|---|---|---|
| stage3-player-visible-only | setVisible | **CRASH** | 38 |
| stage3-player-special-only | updateSpecial | **NO CRASH** | 38 |

Both exclude A/native probes and retain identical non-B groups. This identifies
setVisible as the smallest known crashing **B subset in this fixed setup**. It no
longer requires updateSpecial or the other six B hooks to reproduce. Equal packaged
marker counts weaken a simple Corum-hook-count-only explanation; per-target index,
registration order, interveners and interactions with retained hooks/mods remain open.
NO CRASH is the reported result; exposure duration/count and full startup logs are
not supplied, so this is not ten-minute stability acceptance.

Full-minus-visible and functional candidate outcomes were not supplied. Do not infer
them from the singles. Repro A remains Monopole Practice ~4%, Robot yellow-orb jump.
Clear transition is still Repro B with an unconfirmed matching crash signature.

## Source diagnosis and forensic limit

The implicated Corum detour is CorumRankedSpectatorDummyPlayer::setVisible(bool).
GD 2.2081 declares it virtual, iOS binding 0x22e7a8. The Corum signature and priority
target match the pinned bindings. Its body resolves the current PlayLayer, queries
freezeLocalDummy on a true visibility write, then explicitly calls the base native
setter once. Ordinary Practice still traverses the installed detour even when no
spectator is authorized. The setter therefore places Corum safety/runtime/position
queries inside native visibility/animation lifecycle without needing Ranked activity.

No wrong signature or direct recursive original call was established. The earlier
reported SIGABRT/vector growth/Pool::getCommonHandler remains a runtime failure site.
The original reports/native GD binary needed to recover uniqueIndex or prove stub
relocation, registry/heap corruption or exact machine instruction failure are absent.
Do not turn the device target isolation into a claimed allocator or hook-body bug.
No Tulip clamp, catch, private patch, handler-space change or new observer hook is used.

## Minimal fix implementation

The new opt-in CORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY compiles out **only setVisible**
using the existing method/priority exclusion. Native calls in ordinary Practice no
longer enter Corum's setVisible detour; no runtime no-op branch substitutes for this.
The other six B detours, including updateSpecial, stay selected.

Only an authorized, prepared spectator scene reconciles m_player1 visibility. Existing
world/camera owners and the post-vanilla PlayLayer::postUpdate boundary hide it, including
the wait for a first rendered remote frame. Native writes within a pass may temporarily
change visibility; the owned presentation boundary corrects them before camera/final
frame presentation. No new native detour or per-frame diagnostic log is installed.
Ordinary Practice/test/retired scenes cannot enter this visibility mutation path.

The native updateSpecial guard is unchanged and still prevents non-driver Follow-Y
sampling. World Driver cpp/header/layout and its publisher-time 10ms/Windows-inline
behavior are unchanged in this configuration. The previous two-hook candidate's
200-entry snapshot/replacement is **not enabled**. Spider/Teleport proof, segment deque,
render timestamp, driver coordinates/crossing, camera ownership, adaptive polling,
ACK-bound production publisher and every death/result/persistence/Attempt firewall
remain beta.30. Normal/Ranked/Placement saves and timer-zero final Clear remain allowed.

CMake refuses the fix if setVisible is retained, updateSpecial is removed, the two-hook
replacement is simultaneously enabled, or any physics/world/result/persistence group
or other B safety hook is removed. All fix switches default OFF for unchanged beta.30
compile controls. This is a functional fix candidate for the identified target,
pending full-client iPad acceptance; it is not a release or beta.31.

## Builds and device order

| Package | B hooks | Visibility owner | Purpose |
|---|---|---|---|
| stage4-all-player-control | original seven | OFF | Same-run crash control, A excluded |
| stage4-visible-fix-debug | all except setVisible | ON | Minimal functional fix, Debug ON, A excluded |
| stage4-visible-fix-off | all except setVisible | ON | Same minimal fix, Debug OFF |
| compile-check-beta30-debug-on | original seven | OFF | All diagnostic switches OFF |
| compile-check-beta30-debug-off | original seven | OFF | All diagnostic switches OFF |

Startup must show player-registry-match=true, priorityFailures=0, setVisible registered=0,
updateSpecial registered=1 and world-owned-visibility=true for either fix package. CI
checks complete generated hook metadata and that ownership tag; it cannot run startup
registration or GD on iPad. All packages share the same beta.30 mod ID: replace the old
.geode, fully restart GD, and preserve logs/provenance before changing variants.

Try control then visible-fix-debug then visible-fix-off with identical iPad/GD/Geode/map/
other mods. Use multiple Monopole 4% Practice restarts and at least ten minutes; record
exposure and test Clear separately. Keep Corum OFF as the same-stack comparison.
Then validate Ranked, Placement timer-zero final Clear/local save, RECORD/TEST,
production live spectator, Spider/Teleport, Follow-Y/background/world/camera, hidden
dummy visibility and zero local result/persistence authority. Frame-owner host tests
do not prove late native/other-mod actions cannot alter visibility before drawing.
Candidate acceptance requires actual device checks before main/beta.31 promotion.

## Validation and artifact downloads

Local validation passed all **85 source/native tests** and **eight strict C++ suites**.
The executable world/camera fixture runs original, two-hook and visibility-only paths
under Debug ON/OFF and the Windows inline configuration (nine configurations). It
checks native helper protection, the same publisher timeline at 60/120/144/240Hz,
26 repeated snaps, hidden dummy before camera/final frame, zero local physics, separate
remote identity and preserved history/camera ownership. The actual result/persistence
callback fixture also compiles the visibility-only path; ordinary/local real Clear
is allowed and spectator death/result/save remains blocked. These are host doubles,
not real GD execution or iPad verification. Default beta.30 source bodies/layout,
version, protocol and Debug OFF probe exclusion regressions remain passing.

[CI run 37481669894](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894)
**passed** at source `30a560baa2124e7b8242316422389265b51de4a0`: 85 source/native tests,
eight strict C++ executable suites and all five iOS arm64 builds. No workflow rerun
was needed. All complete native marker sets match the selected profiles. The two
fixed packages each retain six B detours (updateSpecial included) with setVisible
absent and the explicit visibility replacement tag present. They contain no Follow-Y
replacement tag. Original-default compile controls retain seven B hooks and contain
no diagnostic code. The companion Debug validation workflow also passed; its usual
official package build is intentionally skipped on diagnostic branches.

| Download artifact | Verified unique native targets | Role |
|---|---:|---|
| [stage4-all-player-control](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894/artifacts/11422150542) | 44 | Original full B, A excluded; not the fix |
| [stage4-visible-fix-debug](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894/artifacts/11422340415) | 43 | setVisible absent, updateSpecial retained, Debug ON |
| [stage4-visible-fix-off](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894/artifacts/11421059364) | 43 | Same minimal fix, Debug OFF |
| [compile-check-beta30-debug-on](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894/artifacts/11420679933) | 53 | Original unchanged default, diagnostic OFF |
| [compile-check-beta30-debug-off](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894/artifacts/11422480334) | 44 | Original unchanged default, diagnostic OFF |

Every artifact has a named .geode and diagnostic-provenance.json recording source/
package SHA-256, exact enabled/disabled B hooks, SDK/runtime/bindings and ownership
selection. Downloads expire on 2026-10-20. Later documentation-only commits do not
change these artifact sources. Main remains `58d643d21734243e2f3a45e3006d27f178cad83e`,
v0.5.0-beta.30. No beta.31, main merge, force push or manual Release was performed.
The assistant has not run GD on iPad. Stage 4 device results and functional acceptance
remain pending; successful CI alone does not establish that the crash is fixed.
