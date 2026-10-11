# beta55: native clear classification, history Details and map difficulty

The supplied beta53 log (2026-10-11 10.51.13) records BLOZ, match
`8353b5f9-aea9-46d9-83b1-235bec075fe0`, round 3, ending at 99.120%,
99.109% and 99.117% with `cleared=false`; the server accepted those non-clears.
The user confirmed that the first two showed the native completion screen and
the third completed as the round time expired. This is a client terminal
classification failure, not a server rejection of `cleared=true`.

The old Corum `destroyPlayer` callback closed the Attempt before native GD ran,
without checking whether GD actually killed the scoring player or had already
completed the level. The extracted production callback reproduced a false End
when its native boundary refused the collision. That regression failed on the
old source and passes with this patch. The log does not identify the precise GD
internal caller/guard that caused each callback; no iOS disassembly was obtained.

- Capture death progress/pose before native effects, but journal only a confirmed
  death of player 1 or the active dual player. Ignore completion/ignored/stale
  cleanup calls. Preserve the original native call and anti-cheat exception.
- Existing native end-animation hooks protect their completing Attempt until
  genuine completion. An expired round clock cannot turn dual-avatar retirement
  into a non-clear. Reset retires this per-scene completion state.
- Existing completion hooks remain idempotent. Native completion state backs up
  EndLayer, Replay/reset and quit; percentages alone never grant a clear.
- Preserve native score persistence, Placement, spectator no-authority fences,
  recording/transport formats and the device-accepted Pause path. No new native
  detours, hook priorities, private global flags or scoring-rule changes.

History Details used one default screen-sized menu at the bottom of the scroll
content. Geode v5.10.1 `GenericContentLayer` culls direct children by their bounds
when scrolling; that menu falls out of the viewport at the top of a long list.
Each match now has a 29px row menu and retains its original history index. The
compiled production render method is tested at top/middle/bottom across up to
30 matches, preceding Placement rows and multiple window sizes, using SDK
culling semantics rather than claiming device rendering verification.

Difficulty labels now show the original GD label (for example Easy Demon)
instead of GDDL. The square still uses the GDDL tier color and a white number.
Server metadata recovery also queries GDDL when GD's catalog request leaves a
map UNKNOWN; previously that condition skipped GDDL entirely. The exact ID,
rating, original demon difficulty, creator and measured duration can recover
independently, and partial refreshes retain cached values. Length fallback runs
independently of a failed GDDL request. Catalog enrichment stays bounded and
stops repeating failed GD batches to retain time for GDDL in a large catalog.
In-flight immutable match snapshots and gameplay authority retain their existing rules.
The public GDDL response for Light it up (`56427638`) was read successfully:
Rating 3.816901408450704 (tier 4), Easy, 80.8024380216931 seconds.

Client manifest, server exact gate and npm versions are beta55. External image
pack inputs are unchanged. Apps Script redeployment is not required.

Validation: 125 rules and 286 server tests, typecheck/build and 149 source/native
boundary cases passed. The final metadata suite (10 cases), completion safety
suite (13 cases) and 9 strict C++ suites passed after the focused changes.
Native GD/Cocos/FMOD are boundary doubles in host tests, not
device acceptance. Workflow results are intentionally not polled after pushing.
