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
