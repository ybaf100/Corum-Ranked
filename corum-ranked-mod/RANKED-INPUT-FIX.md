# Ranked Pause, retained round input and music transition hotfix

Starting remote main: `c01980c1ca0fd008960a3a92e3d7b2e024b4ad69`.
Client/server version remains `v0.5.0-beta.37`; spectator protocol and Debug
recording format remain v2. Identify this patch by its commit, not an earlier
beta37 artifact. A package built from the previous commit does not contain it.

## Device evidence

`Geode 2026-10-07 15.18.23.log` loads beta37 on Geode 5.10.1 / GD 2.2081.
The user's report is that Pause still does not open, and round/result UI stops
responding from round 2. The log confirms shared countdown GO for Scrap round 1
at 15:27:38 and VOICE round 2 at 15:33:15. Both log `paused=false`,
`pauseBeforeHold=true`, and `pauseEnabled=true`.

Round 2 gameplay and Attempt End ACKs continue, and `MATCH_RESULT` is received
at 15:38:33. This is not evidence of a stopped match poll or a still-disabled
Pause item. It also does not prove that the native Pause callback received a
touch. That log has no touch-handler priority/claim snapshot.

## Source findings and repair

1. The entry hold disabled UILayer touch, then re-registered its swallowing
   gameplay delegate at GO, after the child Pause menu was already registered.
   Cocos handlers with equal priority are ordered newest first. An enabled item
   can therefore lose all touches to a later swallowing delegate. The host
   fixture reproduces this ordering failure; the device's exact priorities were
   not recorded, so this is not proof of that exact iPad ordering.
   Keep UILayer touch registered during the hold. The existing countdown blocker
   consumes touch, keyboard/keypad and Pause remain locked, native updates remain
   held, and button state/queued inputs are flushed at GO. At GO restore the
   native Pause menu's enabled/touch flags and place only that child menu before
   its input delegate if needed. Preserve an already-higher priority and leave
   other menus and force-priority ownership unchanged.

2. Pointer-only CCMenu cleanup does not cancel `TrackingTouch` or the dispatcher's
   claimed touch. Disabling/re-enabling a menu while a touch is active can leave
   it rejecting new touches; a late finger-up can also reach a rebuilt UI.
   Use one scene-scoped cancellation helper before replacing items, covering or
   closing the Ranked scene, and transferring result rank-reveal ownership.
   Unselect the live item, clear its pointer, return to `Waiting`, and discard
   only that menu's claims. No delegate removal/re-registration is needed for
   this cancellation. Restore input flags before base `onEnter`, allowing normal
   child registration order. A covered rank reveal keeps its actual owner pointer
   rather than orphaning the child while clearing the pointer.

3. Explicitly dismiss the countdown's touch/keypad blocker on successful GO and
   canceled scene exit before removing it. This covers both entry lifecycles;
   the old log does not prove a leaked blocker on the user's device.

4. Replace the hand-drawn white surrender flag with GD's native
   `GJ_closeBtn_001.png` X button. Round/match choices, Korean translations,
   Paperlogy font, match-wide 30-second pause budget, and the blocked manual exit
   route remain unchanged.

No new native detours, UILayer hook, animation callback hook or global CCMenu
hook is introduced. Ordinary non-Ranked gameplay never enters this entry repair.
The beta37 score-hook removal, exact-level persistence boundary, local spectator
authority firewall, final Attempt lifecycle, Spider segment buffer, capture,
World Driver, camera and transport are unchanged.

## Music transition evidence and repair

The same device log records the menu-to-match seek from 15:27:17 to 15:27:19:
`audio-seek-ready:1601240us` for song 786863, starting at 26 seconds. The result
track seek takes 1600659us. Resume recovery also logs failed non-blocking streams
with result 28 and repeated channel-restart warnings while a replacement is
pending. Result 28 is FMOD_ERR_INTERNAL in the SDK header; the log does not
establish the cause of that error. There is no Round 3 music transition in this
attached log, so it cannot establish the device's precise Round 3 failure path.

The production config read during this investigation enables audio with 1.8s
fade-in/out and sets Round 3 song 99477's start at **182 seconds**. Keep these
configured songs and offsets. Do not mistake a completed file download for a
ready compressed stream at that offset.

Source defects are reproducible without guessing a new server or audio format:

- Timeout only covered a pending Channel with a non-READY Sound. Initial async
  open, playSound NOTREADY, and repeated seek NOTREADY could wait indefinitely.
  One 3-second preparation deadline now covers all these paths, including
  starving streams. A candidate remains paused/muted until it is ready.
- A timeout changed the effective mode but not the UI's requested mode. The
  next 200ms refresh could undo the fallback and retry the failed Round 3 track.
  Track failures by resource key for the current requested mode. Keep its
  effective match/menu fallback until the requested mode actually changes,
  config changes, or gameplay/close hands off ownership. Bound verification and
  stopped-channel recovery retries, and skip recovery while a candidate is
  already pending. Do not release a still-opening Sound on the timeout path;
  its release can wait for decoder work on the main thread.
- The old channel was stopped before confirming new-channel unpause, and
  fadeInSeconds was never activated. Accept the ready new channel first, retain
  the outgoing channel for fade-out, and schedule both envelopes on FMOD's
  parent DSP clock. Fades now run between UI ticks. Separate setVolume's base
  volume from the 0-to-1 fade multiplier; a zero base would remain silent. On an
  interrupted fade, use its actual DSP-clock gain and replace future points.
  A failed DSP setup clears partial points and uses the bounded tick fade.
  Stop outgoing/active/pending channels on gameplay handoff, close, and Sound
  teardown, and pause the outgoing channel on application suspension too.

FMOD's published API support explanation confirms that fade-point gain and
get/setVolume are independent: [FMOD support thread](https://qa.fmod.com/t/22995).
All changes use public FMOD APIs already declared in Geode 5.10.1's SDK. No
FMOD global buffer tuning, full MP3 decoding in memory, new native audio hooks,
vendored Geode source, server protocol or song/download changes are introduced.
Streams intentionally sharing one Sound/song ID still cannot play two instances
from that single stream buffer; the existing stop-before-reseek rule remains.

## Validation and device follow-up

`RankedInputNativeTest.mjs` executes the actual helper and retained scene
callbacks against Cocos touch/claim doubles in Debug ON and OFF. It reproduces
pointer-only cleanup and equal-priority ordering failures, then checks canceled
finger-up, item replacement, three scene push/pop cycles, result clicks, existing
modal priority, and entry blocker retirement. `EntryGameplayNativeTest.mjs` now
checks an actual dispatched Pause click and stable UILayer registration, in
addition to shared start authority, input/audio hold, animation, stale context,
and ordinary gameplay. Existing local safety callbacks also exercise entry
blocker cleanup through spectator and real scene teardown.

`RankedAudioTransitionNativeTest.mjs` compiles the production audio class and
callback bodies in Debug ON/OFF against public FMOD doubles and a controlled
steady clock. It checks the 1.6-second menu/match seek, the 182-second Round 3
seek timeout followed by 300 identical UI requests, initial-open and NOTREADY
timeouts, starving/recovering buffers, unpause failure with the old BGM kept,
interrupted and partial DSP fades, bounded no-progress retries, single pending
candidate, and complete gameplay/application/close/Sound teardown ownership.
These tests verify transition logic, not actual MP3 decoding or iPad audibility.

Source/native callback suites: 95 pass. Strict C++ suites: 9 pass. Workspace
typecheck/build pass. These are host and source validations; they do not replace
an iOS SDK compile or iPad testing.

The previous GitHub Actions run `37581234074` failed before any job steps and
produced no execution logs. Its underlying error has not been confirmed through
the connected API. Do not relabel it as a compiler failure or a successful build,
and do not change the live exact client gate before a new package is available.

Recheck on iPad with the same mod set: open/resume Pause after GO in rounds 1 and
2, tap the X and both surrender scopes, reach the next round/result and click
their controls, hold a finger through a UI rebuild/scene transition, and confirm
waiting/countdown still prevents gameplay. New bounded logs record
`[RankedInput] GO` menu/input priorities and menu flags,
`[RankedInput] lobby entered` menu state, and `[RankedPause] opened` when the real
Pause setup runs. Recheck menu-to-match, Round 3, results, and minimize/resume
audio too; failures now log a bounded key/song/phase/result instead of silently
retrying the same track. Device success and the exact device failure paths
remain unverified until this package is tested.
