# Ranked Pause result verification — beta41

Starting authoritative main: `e15a601906f89bcf055999f8d91c99875efc19f5`
(beta40). Client/workspace/server exact gate: `v0.5.0-beta.41`.

## New device evidence supersedes the beta40 Pause prediction

`Geode 2026-10-07 23.00.17.log` loads beta40 / Geode 5.10.1 / bindings
7f6c2a75 / GD 2.2081. The user confirms that surrender now returns immediately.
At 23:01:00 the authenticated round surrender and queued scene return complete
in the same second; native Round 2 entry follows normally. Preserve that fix.

Round 1 (93217777) opens native Pause and reports button dispatch paused=true.
Round 2 (56455318) GO is unpaused/enabled with UI/menu priorities 0/-128 and
observer -500. At 23:01:19, :20, :22 and :23, the button dispatch returns with
paused=false, canPause=true and uiOwnerMatches=true. No Round 2 native Pause
opening is recorded. This rejects both the old priority-only explanation and
beta40's assumption that activating the native button necessarily opens Pause.

The earlier test invented an internal `nativeRequest` prerequisite and then
always made a native button request succeed. Neither behavior came from iOS
disassembly. It missed this device failure; those predictions are superseded.

## Verify the result and use the exact scene's native entry point

The scene-owned observer preserves the installed native button callback. Only
if it returns unpaused does the observer recheck the exact current/running
PlayLayer, unchanged UILayer ownership, canPauseGame, visibility, touch position
and released entry gate. Then it resets native UI button state and makes one
direct `PlayLayer::pauseGame(false)` request. This is the public native entry
point used by Geode 5.10.1 `PopupManager::update` for gameplay popups:

- [Exact released Geode implementation](https://github.com/geode-sdk/geode/blob/7e41336/loader/src/ui/PopupManager.cpp)
- [Exact released bindings](https://github.com/geode-sdk/bindings/blob/7f6c2a75/bindings/2.2081/GeometryDash.bro)
- [Windows-only doPause counter/reset reference](https://github.com/geode-sdk/bindings/blob/7f6c2a75/bindings/2.2081/inline/UILayer.cpp)

A successful button request has no direct retry. A callback that completes,
detaches or changes the current scene cannot trigger the direct request. A
native refusal stays unpaused: no direct m_isPaused write or replacement Pause
menu is introduced. Native PauseLayer/customSetup/resume/restart, Corum's
match-wide 30s lease and authenticated surrender UI remain the owners.

The direct successful request increments m_pauseCounter only when the button
callback did not already increment it. There is no counter clamp/reset or
pauseBufferTimer modification. Five bounded request logs per scene distinguish
buttonPaused, nativeFallback and final paused, and record callback target/
selector presence, the base global owner and counter/buffer observations.
No native diagnostic detour, global input reset or other-mod patch is added.

## What is and is not established

The broken boundary is device-observed: the installed button/UILayer dispatch
returns without entering native Pause for a live Round 2 scene. The precise iOS
instruction or hook-chain participant that causes this no-op is not recovered.
Do not claim that WeakRef cleanup, a stale global pointer, pause buffering or a
specific other mod is its proven cause. The new metadata makes those narrower
hypotheses distinguishable without changing global hooks.

The revised executable regression makes Round 1 native callbacks succeed and
Round 2/3 callbacks return without pausing, matching the device's observable
result. It fails on unmodified beta40 and passes with result verification. It
also covers healthy callback single invocation, retained target/selector,
already-incremented counter, unchanged buffer timer, native refusal, foreign
UI ownership, callback-side completion/detach/replacement, held/cancelled touch,
modal input and teardown in Debug ON/OFF. It does not emulate undiscovered iOS
instructions or prove device success.

Existing concession return, Placement final Attempt/persistence, capture/snap,
World Driver/camera, Safe Mode, audio and retained UI tests remain. Spectator
protocol v2 and Debug recording v2 are unchanged. No PlayerObject hook is added;
setVisible and global saveNewScore/storeNewLocalScore remain absent.

Acceptance still requires the same iPad/mod-stack R1 Pause -> surrender ->
automatic return -> R2/R3 Pause/resume/restart run. The native opening marker
and final request paused=true must agree. If nativeFallback=true still returns
paused=false, the failure is now narrowed past the UILayer/button dispatch to
the native PlayLayer entry point; do not mark that device scenario successful.

## Prior beta40 investigation (device results now recorded above)

# Ranked Pause and authenticated concession lifecycle — beta40

Starting authoritative main: `1f2471f45a78f1c71a357e3d097b11c0b7b90f47`
(beta39). Client/workspace/server exact gate: `v0.5.0-beta.40`.
Spectator protocol v2, Debug format v2, scoring, match-wide 30s budget,
Placement final-attempt rules, Spider segment boundaries and safety stay intact.

## Device evidence and limits

`Geode 2026-10-07 20.37.26.log` actually loads beta39 / Geode 5.10.1 / GD
2.2081. At 20:38:06 round surrender POST returns 201 and the server enters
ROUND_RESULT. The client immediately logs `Ignoring premature Ranked state
transition while visual attempt is alive`. The server enters ROUND_PREPARE at
20:38:11; the native child does not return until the user's exit at 20:38:23.
Round 2 GO has enabled/unpaused input, UI/menu priorities 0/-128 and observer
priority -500. Five observer logs AFTER `UILayer::doPause()` report paused=false,
canPause=true. There is no Round 2 Pause opening log. This disproves a claim
that beta39's remaining failure is merely missing touch delivery or priority.

The trace establishes the failing boundaries, not the exact iOS native return
instruction. Released 2.2081 bindings expose iOS UILayer::onPause at 0x49540,
doPause at 0x49ff0 and PlayLayer::pauseGame at 0x1227f0. The doPause body in the
bindings inline source is Windows-only: it is not iOS disassembly. No actual
iOS GD executable/register trace is available here. The native Pause double
previously opened a menu unconditionally once canPause succeeded. Successful
host tests therefore did not establish device success.

## Corrections

The scene observer activates the existing native Pause BUTTON, preserving its
installed target/selector and native onPause path. It no longer calls internal
doPause directly or builds a replacement Pause menu. Existing scene/visibility/
canPause/held-touch checks and scoped priority remain. Ordinary gameplay has no
observer. Bounded logs identify the level, native result and UILayer ownership.
No new PlayerObject/GJBaseGameLayer/UILayer hot detour is added.

Geode 5.10.1 WeakRefPool retains native objects and WeakRef pointer assignment to
nullptr only drops the controller. Pause fields and surrender Popup formerly
retained each other through WeakRef, and cleanup relied partly on destruction.
Popup owner and Pause modal references now have explicit destructible optional
ownership. Closing/exiting a Popup releases its owner and its own force priority;
Pause resume retires its modal, scheduler and lease BEFORE native onResume.
Detached dialogs cannot block a fresh surrender click. Scene observer and round
return owners move their old WeakRefs into a scoped destructor rather than using
the problematic nullptr assignment. Native cleanup no longer depends on a later
round discovering the previous objects. This is a source-confirmed lifecycle
correction, not proof of one native iOS guard or a specific other-mod culprit.

An authenticated surrender ACK latches an exact match/round/deathmatch terminal
command independently of a public-state snapshot. Missing/stale polls cannot
revoke it. Late ACKs cannot replace a newer notice, roll back the public round,
or retire a new round's Attempt. MATCH concession ends the entire match. A
confirmed server concession also latches the command for both players.

PlayLayer handles that command before the normal alive-Attempt protection in
postUpdate as well as the spectator heartbeat. Pause's active budget watcher
resumes its native menu then requests the same handoff, even if the native
PlayLayer heartbeat is paused/unscheduled. The helper marks the visual Attempt
ended and queues an idempotent return outside the native callback stack. It only
exits the exact current scene. There is no second Attempt End POST. A normal
timer=0 final Attempt remains alive until actual death/Clear; it does not create
a concession command.

Pause remaining time uses the Corum font on a dark plate at the upper left.
The retained Ranked UI shows a non-interactive Corum-font opponent concession
notice in Korean/English only when the authenticated side is the opponent and
that current context was conceded. ROUND notices stop at the next round; MATCH
notices persist through match results. Existing surrender controls keep the GD
X sprite and localized Corum confirmation UI. Font charsets include the new
Korean text; CI regenerates and validates SD/HD/UHD bundles.

## Regression and device acceptance

Executable source/native-boundary tests cover R1/R2/R3 button callback dispatch,
internal-step versus public-request semantics, timeout/concession resume,
explicit modal/owner cleanup, deferred exactly-once scene return, stale scene
rejection, concession ACK after round advancement, preservation of a legitimate
new Attempt, newer opponent notice and lease budget. The public request fixture
models a native precondition; it is explicitly not a recovered iOS instruction.
Existing Placement, real Clear persistence, entry, downloads, capture/snap,
World Driver, camera, Safe Mode and audio regressions remain.

Prior spectator tests hard-coded gameplayConceded=false; the revised suite
executes the actual handoff helper and drains its deferred callback. Full
source/native tests, strict C++ suites, workspace/server tests and CI validate
the code. iOS Debug ON/OFF builds are required before main promotion. None of
these are device verification. With the same iPad/mod stack, acceptance still
requires repeated R1 surrender -> automatic return -> R2/R3 Pause, manual
resume/restart, timer exhaustion and opponent/match surrender. Do not describe
the remaining iOS native guard as proven or these device scenarios as passed.

## Prior beta39 investigation (superseded predictions)

# Ranked round input and music preparation — beta39

Authoritative starting main: `d104f2bbcf779320e4a8ee0a560d9cb604725ebf`
(beta38). Client/workspace/server exact gate: `v0.5.0-beta.39`.
Spectator protocol v2 / Debug recording v2, scoring, Placement final-attempt
rules, persistence safety and Paperlogy assets are unchanged. Build an installable
package on a work branch before promoting the same commit to main.

## Authoritative device evidence

`Geode 2026-10-07 19.53.40.log` loads beta38 with Geode 5.10.1 / GD 2.2081
(loader 7e41336, bindings 7f6c2a75). The user reports working Pause in Round 1,
failed Pause in Round 2, and silent Ranked lobby/wait screens. This supersedes
beta38's unverified predictions; its successful CI was not device verification.

Round 1 Aeoning 89425733: GO at 19:54:17 reports UILayer priority **0**, Pause
menu **-128**. Native Pause opens at 19:54:18; a round surrender is accepted at
19:54:24, then the retained Ranked scene resumes at 19:54:27. Round 2 entropie
143308401: GO at 19:54:44 reports UILayer **-502**, Pause menu **-503**. All GO
flags are enabled/unpaused, but no second native Pause opening is recorded.
The scene observer introduced in beta38 registered at fixed **-500**, behind
both native delegates. Repairing only the menu priority did not repair that
observer. This explains a concrete dispatch failure missed by the previous test
that invoked observer callbacks directly without dispatcher ordering.

The exact Geode 5.10.1 `Popup.cpp` registers force priority in `Popup::init` and
unregisters it in `Popup::~Popup`, not onClose/onExit. `WeakRefPool::manage`
retains managed objects; collection is checked when references lock/destruct.
The surrender Popup has a weak PauseLayer owner, and PauseLayer fields have a
weak Popup. After closing/detaching these objects, destructor cleanup can remain
delayed. This is a source-supported way a closed Corum modal can affect later
registrations, consistent with the observed shift. The log does not enumerate
the native force-priority dictionary, so it does not establish its exact owners
or an iOS instruction-level trace.

BGM at 19:53:48 and 19:53:52 fails menu song 224958 (2s), and at 19:53:56 fails
match song 786863 (26s), always **phase=initial-position result=0**. FMOD_OK is 0:
Corum rejected a successful API result because the first paused-channel position
was outside its guard. The old log omitted the observed position, so its exact
value cannot be recovered. Initial-seek success was assumed in the old host
FMOD double, which initialized Channel.position from Sound.initial unconditionally.
The new tests include zero/stale position reports rather than assuming this.
The previous audible stutter is not measured in this silent beta38 run.

## Scoped input correction

The scene-owned countdown/Pause observer is preserved. During countdown and at
GO its own handler is aligned ahead of BOTH the exact scene's UILayer and Pause
menu. Only that handler's priority changes; there is no arbitrary lower fixed
constant, new global hook, shared input reset or force-priority dictionary patch.
GO now logs the observer's actual priority.

The observer still claims only the native Pause button's transformed bounds;
while paused it returns false, leaving native PauseLayer and modal input alone.
Current/running owner, visibility, native canPauseGame, held/cancelled fingers and
scene teardown checks remain. It calls native UILayer::doPause; native resume,
restart, 30-second match pause lease, localized Paperlogy surrender and blocked
manual exit remain. Ordinary gameplay creates no observer.

Surrender Popup onExit explicitly unregisters ONLY its own modal force priority,
even if destruction is deferred. Its native close/back route goes through the
existing idempotent dismiss/unschedule path. Base Popup exit/destruction remains.
No external modal registration is removed.

## Prepared channels, not a Sound-only position assumption

Retain asynchronous CREATESTREAM / NONBLOCKING / ACCURATETIME and initial seek
on independent per-resource Sounds. While Ranked UI is visible, also warm paused,
volume-zero, muted channels for upcoming modes. If the actual position differs,
submit one successful Channel::setPosition in milliseconds on that silent
candidate. Wait for a later audio heartbeat, READY/non-starving buffer and actual
position. A successful playSound followed by stale getPosition is pending work,
not an immediate terminal failure. NOTREADY is pending as well.

A mode switch consumes the exact prepared channel once. A ready/prewarmed
transition does not call playSound or seek again. Revalidate the offset before
unmuting/unpausing, schedule the DSP fade, then retire the audible predecessor.
The predecessor is never sought or flushed. While preparation is unfinished it
continues playing unless its own buffer is starving. The existing 20ms audio
heartbeat handles preparation and active/retiring starvation mute separately
from HTTP/UI polling. Configured songs/offsets/80% relative volume remain.

Initial menu playback can wait for its asynchronous seek; no earlier Ranked
channel exists there. Each preparation and requested switch has a bounded 3s
budget. Errors/timeouts retain the existing requested-mode fallback latch and
bounded playback retries; repeating the same UI request cannot continuously
reopen/reseek the failure. New requested modes can retry failed preparation.
Logs now record requested/observed offset and actual seek result.

Gameplay, close and config teardown stop ALL paused standby channels in addition
to active/pending/retiring owners. Even a paused first play consumes the Sound's
initial seek, so stopped/consumed streams are replaced before reuse. Gameplay
handoff only reopens asynchronous Sounds; it does not leave standby channels
running under vanilla gameplay. Active/fading/loading Sound release remains
protected. No full-track PCM allocation, global FMOD tuning/detour, manual mixer
update or song/protocol redesign is introduced.

Public FMOD sources:
- [Initial seek is first-stream-play only](https://qa.fmod.com/t/how-to-change-position-time-in-programmer-sounds/16716)
- [Async seek and stale getPosition until completion](https://qa.fmod.com/t/which-functions-return-fmod-err-notready-when-in-fmod-openstate-setposition/14496)
- [Prepare another stream ahead; MP3 priming matters](https://qa.fmod.com/t/channel-setposition-using-fmod-timeunit-rawbytes/19588/2)
- [Stream starvation / repeated decode buffer and mute](https://www.fmod.com/docs/2.03/api/core-api-sound.html#sound_getopenstate)

## Validation and device boundary

RankedPauseInputNativeTest compiles actual observer and priority helper methods
with Cocos boundary doubles in Debug ON/OFF. It dispatches through registered
handlers, reproduces the old -500 route missing Pause against -502/-503, and
covers fresh R1/R2/R3 scenes, stronger later priorities, no registration churn,
resume, disabled/TrackingTouch menu, held/cancelled/dragged fingers, completion,
foreign/expired owners and teardown. RankedPauseNativeTest executes the actual
modal onExit with delayed destruction and verifies only its force owner is
removed, across three rounds and repeated cleanup.

RankedAudioTransitionNativeTest compiles the actual class/callbacks. It retains
all prior crossfade, lifetime, starvation, resume, retry and fallback tests and
adds offsets 0/2/26/29/182s with initial getPosition=0, stale async seek, no
repeated accepted seek, ready-channel transfer without replay/seek, permanent
seek timeout and standby teardown. Existing entry, retained UI, persistence,
final Attempt, spectator snap/world/camera and Safe Mode regressions remain.
These API doubles do not run the iOS game or decode actual MP3 audio.

Acceptance still requires iPad testing with the same mod stack: Round 1 Pause,
round surrender, Round 2 and Round 3 Pause/resume/restart; match surrender and
results; lobby/wait/R3/result BGM and app background/resume. Capture the actual
observer priority, native Pause opening, requested/observed offset, playback
verification and buffer health logs. CI success is not device verification.
