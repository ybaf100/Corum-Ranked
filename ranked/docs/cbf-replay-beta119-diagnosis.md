# CBF native-boundary replay: beta.119 investigation

Start main: 383ef8ac85590f3f846cd1fd33533b1fc02ce609 (beta.118).
User ZIP SHA256: 346f3536bbd723f36f3b24f76716c80f6edc8edd9bfcf59cb655bd98b365d59e.
All 11 listed checksums were verified. Original logs remain outside the repository.

| Evidence | Confirmed source path | Conclusion / remaining uncertainty |
|---|---|---|
| beta.118 primary 19:55:15 line 698; active=false/tick=0 afterward | Recorder::begin -> replayPhysicsEnvironmentProblem -> CBF_ACTIVE_TIMING_UNSUPPORTED | Confirmed admission failure; no recording began. Separate from playback DESYNC. |
| beta.116 primary 23:16:41 line 821: frame93, dt .004166666883975267, source step364 / playback0; both initial step0 | CBF moves queued buttons to private inputVector, dispatches handleButton from PlayerObject split loop. Old recorder captured queue removal only. | Confirmed missing capture path. Exact Native counter mutation is not proven by this log alone; no guessed counter offset will be used. |
| same log lines856/857: frame92 x matches, Cube y108.64212799 vs105, rotation2.702913 vs0 | Old files lack actual handleButton/substep data and initial velocity/holds. | Missing jump application is observed. Input loss vs initial state must be tested separately. These are repeated playback attempts, not two new source recordings. |
| same log lines898/899: frame2 Ship rotation6.482552 vs -3.216647/-6.496874; x/y agree | CBF splits rotation and restores pre-step lastPosition; old initial has no shipRotation. | Rotation order/initial state are plausible causes; the old log does not establish which is first. |
| beta.117 primary 07:33:16 line633: frame35 Ship y107.761627 vs105 | Same old queue-only source path | Independent source Attempt supports missing applied input evidence; beta.118 fixes are not treated as disproven by an older file. |

The extended 23:16:41(1) file contains the complete original. Deduplicated source
cases: 3 Attempts in that pair (5 playback failures), plus 1 Ship Attempt in
07:33:16. Entropie supporting live-spectator failures are outside this task.

## Verified external implementation and licensing

CBF commit 9fa7c6f590945d048b8e83de71eaba30dcb0c64e declares version1.5.0,
GD2.2081 on all target platforms, MIT license (2025 theyareonit).
`buildStepQueue` consumes the GD queue, derives substep fractions from its own
frame clock; `popStepQueue` dispatches the previous segment's input;
PlayerObject update performs split Native updates, collision checks, rotation,
collision-log cleanup and ground repair. The outer rotation restores the saved
pre-step lastPosition. Windows additionally changes Native ship timing and
Slerp's coefficient. macOS builds the input plan in GJBaseGameLayer::update
because getModifiedDelta is inlined. These differences require explicit handling.

ToastyReplay 016a5c8219e39c732583f655583ceee1982a3168 (PolyForm Strict with
redistribution restriction) and GucciBot f6ad752d31bf0ca7e954961d7756e2fdbe9da9cf
(GPL3) were read for timing/state categories only. No implementation is copied.
Geode v5.8.2 public Hook/Mod interfaces expose getHooks, getDisplayName,
getPriority, isEnabled, enable/disable, getAddress and getBinaryPath. No invented
original-function API or fixed binary offset will be used.

## Implementation decision

Keep beta.118's v2 Vanilla/CBS path, initial physical validation, preInputs,
receipts and storage safety. Add an explicit v3 CBF native-boundary tape:
record actual inner Native update/rotation/collision/input order and dt, with
before/after evidence and source clock/queue handoff. Replaying executes Native
methods, never source coordinates. CBF's collision-log/ground/lastPosition side
effects must be reproduced from the local pre-step state, with evidence checks.

During a replay-owned Scene only, lease the **enabled CBF physics hooks** through
public Geode APIs so CBF cannot run a second private-clock simulation. Restore
every leased hook before ordinary gameplay. User settings remain unchanged;
official capture continues through the actual enabled CBF implementation. An
acquisition/restoration failure fails closed. Windows ship correction uses CBF's
published hook target discovered from its Hook object, with timing recorded at
the actual Native boundary; no hardcoded executable address is introduced.

Default CBF ON is mandatory. COS/PB/unknown versions remain distinct unsupported
profiles until proven. Changes during recording/replay invalidate that Attempt's
capture rather than fabricating timestamps. SDK/platform compilation and Native
contract tests are separate from actual GD/device validation, which has not yet
been performed in this environment. No production changes have been made.
