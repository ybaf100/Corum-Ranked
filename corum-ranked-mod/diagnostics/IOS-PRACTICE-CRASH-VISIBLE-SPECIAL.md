# Visible/special isolation and world-owned fix candidate

Main is `58d643d21734243e2f3a45e3006d27f178cad83e`, beta.30. This work starts from
diagnostic `e24bbe199d05912b7de5b99e04b653cd3970cc2b`. No main merge, beta.31,
client gate/protocol change or manual Release is authorized before device acceptance.

## Evidence and cause boundary

The user confirmed visible-special-only **CRASH**, transform-setters-only **NO CRASH**.
With probes and legacy B absent in both, setVisible/updateSpecial is the strongest
crashing subset. A particular member, direct body corruption, invalid uniqueIndex,
OOM or count/order/mod interaction has not yet been proven. Monopole Practice 4%
Robot yellow-orb jump remains Repro A. Clear transition remains separate Repro B;
its full report must match signal/thread/stack/runtime UUID before causes are combined.

GD 2.2081 bindings match both Corum signatures. setVisible(bool) is a virtual setter
at iOS 0x22e7a8; updateSpecial(float) is a sampler at iOS 0x21d6d4 (Windows inline).
Neither hook body has to activate spectator code to participate in Tulip's chain.
The source/binding comparison found no proven wrong signature or recursive original
call. Allocation at Pool::getCommonHandler remains a failure site, not a proven cause.

## Actual candidate change

`CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION` is OFF by default and requires diagnostic
mode, both suspect detours compiled out and all other safety/ownership groups retained.
CMake rejects removing physics/result/persistence protection from this candidate.

The candidate retains all five other B detours: update, updateRotation, spiderTestJump,
setPosition and setRotation. Thus no local physics is re-enabled, native Spider/Teleport
proof capture remains, and driver-only coordinate writes remain guarded. All PlayLayer
death/result/attempt/persistence firewalls and the safer storeNewLocalScore hook remain.

Hidden visibility is reconciled by the existing spectator world/camera boundaries
and after vanilla PlayLayer::postUpdate. Waiting for the first remote frame also hides
only m_player1. Ordinary Practice does not run this active-spectator mutation path.
There is no global setVisible detour in the candidate; native visibility writes can
occur inside a world pass and are corrected before the owned frame is presented.

World Driver still calls the original native updateSpecial / Windows inline helper
in publisher-time 10ms steps. It snapshots the 200 Follow-Y entries, index and sampler
accumulator after those steps. World/camera reconciliation restores that authoritative
snapshot after any native sub-step writes. At a Spider/Teleport segment destination,
only this local history resets; interpolation deque/stream identity are preserved.
No trigger engine, physics replay, new native observer or Tulip workaround is added.

Host fixtures verify history and visibility before camera/final frame boundaries.
They do not prove all real GD intra-frame Follow-Y consumer ordering, late callbacks
from other mods or actual device rendering. Include Follow-Y/background/trigger parity
in candidate device regression, not just ordinary crash reproduction.

## Stage 3 packages

Every comparison excludes A/native probes, keeps other non-B groups and pins the same
SDK 5.8.2, device Geode 5.10.1/Tulip 3.1.14, GD bindings and beta.30 mod ID/version.
No new native detours are introduced. Same-ID packages must replace one another with
a full GD restart and the same iPad/level/mod set.

| Package | PlayerObject B detours | World-owned replacement | Use |
|---|---|---|---|
| stage3-visible-special-control | setVisible, updateSpecial | OFF | Rebuild crashing pair control |
| stage3-player-visible-only | setVisible | OFF | Individual member isolation |
| stage3-player-special-only | updateSpecial | OFF | Individual member isolation |
| stage3-full-minus-visible | all except setVisible | OFF | Full-B complement |
| stage3-full-minus-special | all except updateSpecial | OFF | Full-B complement |
| stage3-full-minus-visible-special | update, updateRotation, spiderTestJump, setPosition, setRotation | OFF | Full-B pair-removal control |
| stage3-fix-candidate-debug | same five | ON; Debug harness ON | Functional candidate, not release |
| stage3-fix-candidate-off | same five | ON; Debug harness OFF | Candidate Debug OFF comparison |

The six hook-only isolation packages are for ordinary Practice only. They omit safety
hooks without a substitute. The two candidate packages include the explicit substitute
and retain all other protections, for Practice plus functional regression. Neither is
device accepted yet. Two additional compile-check-beta30-debug-on/off builds keep all
diagnostic switches OFF and the original seven B hooks; production defaults are preserved.

Every artifact contains its named .geode and diagnostic-provenance.json. CI verifies
the full native detour marker set, exact enabled/disabled B list and inclusion of the
world-owned replacement for candidates. It rejects a plain hook-removal package
mislabelled as the candidate. Startup logs report player-registry-match and the
world-owned flag; record complete logs and require no priority failures.

## Device order and interpretation

First reproduce pair control, then test visible-only and special-only with multiple
fixed-condition runs. Keep each crash report and elapsed exposure/attempt count.

| Visible-only | Special-only | Interpretation |
|---|---|---|
| CRASH | NO CRASH | Visible is the minimal crashing single in this setup; test full-minus-visible |
| NO CRASH | CRASH | Special is the minimal crashing single in this setup; test full-minus-special |
| CRASH | CRASH | Independent reproductions/shared runtime interaction remain possible |
| NO CRASH | NO CRASH | Pair interaction or registration/count/order effects remain possible |

Then compare full-minus-visible-special and the candidate Debug build. These retain
the other five B hooks and answer whether pair removal works in the complete client.
Subset-only stability does not imply stability under full B. Candidate OFF/ON changes
the world-owned substitute without changing the five installed B detours.

For the candidate, test ordinary Practice with repeated restarts/animation-heavy
Monopole 4% and ten-minute exposure; separately exercise Clear transition. Also compare
candidate Debug ON/OFF, Corum OFF and the same full mod stack. Follow with Ranked,
Placement timer-zero accepted final Clear, real local saves, RECORD/TEST, live spectator,
Spider repeated boundaries, Follow-Y/background/world/camera and local Safe Mode.
Actual performance/network cadence remains beta.30 (including ACK-bound publisher).

Main can receive a clean final fix only after this identifies a causal single or
interaction and the candidate is accepted on device without feature regression.
CI/host success alone is not a crash fix or device verification.

## Validation and downloads

Local source/native regression: 81 tests passed. Eight strict C++ suites are run
locally and in the diagnostic workflow. Candidate world-owner fixtures run Debug
ON/OFF and Windows inline configurations, including 60/120/144/240Hz and 26 snaps.
They check all 200 history values/index/accumulator, native extra writes, waiting
visibility, inactive scene isolation, exact other-player ownership, segment resets
and unchanged local death/result/persistence authority.

CI run and exact artifact downloads are recorded here when the builds finish.
Stage 3 device outcomes and candidate acceptance remain pending.
