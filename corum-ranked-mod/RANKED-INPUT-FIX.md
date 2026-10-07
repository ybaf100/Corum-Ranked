# Ranked Pause input and prebuffered music — beta38

Starting authoritative remote main: `363c1bcf2bded5168868f6936b2abce4b9b82c87`.
Client/workspace/server exact gate: `v0.5.0-beta.38`. Spectator protocol v2 and
Debug recording format v2 are unchanged. Build the package before promoting
this commit to main, so the automatic server deployment cannot precede it.

## Device evidence and correction of the previous hypothesis

The user reports that both symptoms remain after the previous commit: Ranked
Pause never opens (ordinary gameplay works), and music repeatedly stutters
while changing Ranked screens. `Geode 2026-10-07 18.24.38.log` only records
startup; `Geode 2026-10-07 18.19.53.log` contains the actual reproduction.
Both load beta37 / Geode 5.10.1 / GD 2.2081. The reproduction includes the new
`[RankedInput] GO` marker, proving the previous input patch is installed.

Aeoning 89425733 enters natively at 18:23:27; shared GO occurs at 18:23:31.
GO records `paused=false`, `pauseEnabled=true`, UILayer touch=true, menu
touch/enabled=true, UILayer priority=0 and menu priority=-128. Attempts and
End ACKs progress afterward. No `[RankedPause] opened` appears. Thus an
init-time disabled button or the prior proposed equal-priority ordering
cannot explain this recorded GO. The log does not show the actual failed
finger-down/callback or later native button state, so the precise iOS drop
inside touch dispatch / UILayer::onPause is still unproved. Do not label a
host double or the correct GO flags as proof that the old patch fixed Pause.

The exact released bindings (7f6c2a75, GD 2.2081) identify iOS UILayer::onPause
0x49540, doPause 0x49ff0, PlayLayer::pauseGame 0x1227f0 and canPauseGame
0x1227cc. The same binding revision's Windows inline doPause resets touch
IDs/UI state, calls the current PlayLayer's pauseGame and increments the native
pause counter. This is source evidence for the intended native API; it is not
an iOS game disassembly or evidence of which onPause branch failed.

## Scene-owned Pause input

The existing countdown delegate stays owned by the exact Ranked PlayLayer.
During waiting/countdown it swallows input and holds gameplay as before. At GO
it becomes transparent and only claims touches inside the existing native
Pause item's transformed bounds. Every other gameplay touch passes through.
A countdown-held finger cannot turn into a Pause click. Finger cancel/drag-out
unselects without activating. The current, running PlayLayer must match its
weak owner, remain unpaused and satisfy native canPauseGame; the button and its
ancestors must be visible and belong to that scene.

A completed click calls native UILayer::doPause, retaining native PauseLayer,
resume/restart, pause counter and RankedPauseLayer's existing 30-second match
lease, Paperlogy budget text, native X, localized surrender and blocked exit.
While paused it claims no touches, so PauseLayer and surrender dialogs own
input. Exit disables and removes the delegate and weak owner. Ordinary scenes
never create this observer. No new global detour, setter hook, forced-priority
patch, separate drawn Pause button or private hook-runtime change is introduced.

Bounded `[RankedPauseInput] native dispatch paused=... canPause=...` and existing
`[RankedPause] opened` now distinguish a real native opening from GO flags.
This explicitly replaces the fragile CCMenu callback dependency in this owned
scene; it does not claim to have identified an unavailable iOS instruction.

## Music findings and changes

The reproduction records menu seek preparation at 200102us and menu-to-match
at **1600480us**, with match song 786863's 26s offset. Earlier device evidence
also recorded a 1.6s result seek. The configured R3 song 99477 starts at 182s;
there is no R3 transition in this new log, so its device failure remains
unconfirmed. Configured songs, offsets and volume are preserved.

Opening a stream at zero in prewarming did not prewarm its configured offset.
The old mode switch still performed Channel::setPosition. Prewarming now uses
FMOD_CREATESOUNDEXINFO.initialseekposition/initialseekpostype and ACCURATETIME
with CREATESTREAM|NONBLOCKING. Indexing, initial seek and buffering occur before
the competitive transition. Ready paused playback must report the intended
offset (250ms tolerance) before unmute; a wrong offset fails quietly and never
triggers a presentation-time repair seek. There are zero Channel::setPosition
calls in the Ranked audio manager.

Initial seek applies only to the first play of a streaming Sound. Streams are
therefore keyed by resource, not song ID, and consumed once. Gameplay handoff
re-arms consumed buffers before returning to the retained round UI. Modes that
share a song get independent buffers/offsets. Displaced active/fading/loading
Sounds are retained; cleanup releases only stopped, ready/error handles, never
an active/fading/pending or still-loading stream in a transition.

The other confirmed source defect was unconditional active-channel unmute on
refresh/resume, even if that stream was starving. FMOD explicitly documents
that starvation repeats old decode data. Stream health now checks both active
and retiring channels, mutes non-ready/starving streams and unmutes the same
channel on recovery. It does not restart a temporarily muted stream. Sustained
failure uses the existing bounded fallback. Verification waits for recovery.
A 20ms scene-owned audio heartbeat is independent of the 200ms HTTP/UI refresh,
and is unscheduled when that scene is covered/exited. Limited logs record owner,
open state, starvation, buffered percentage, disk busy and result. The previous
log did not record these fields; actual device starvation is a hypothesis to
verify, not a measured fact.

Public FMOD references:
- [Sound::getOpenState — repeated decode data and mute](https://www.fmod.com/docs/2.03/api/core-api-sound.html#sound_getopenstate)
- [Initial seek is first-play only](https://qa.fmod.com/t/how-to-change-position-time-in-programmer-sounds/16716)
- [Stream priming and preparing ahead](https://qa.fmod.com/t/channel-setposition-using-fmod-timeunit-rawbytes/19588/2)

DSP fades, bounded failures/requested-mode fallback latch and cached paths remain.
No full-track PCM allocation, global FMOD buffer tuning, new audio detours or
song/protocol changes are introduced. A blocked mixer/OS/storage path beyond
these public stream states cannot be proven or eliminated by host tests.

## Validation boundary

RankedPauseInputNativeTest executes the actual scene observer methods in Debug
ON/OFF, including native Pause creation via doPause, repeated resume across
three rounds, initial disabled native menu, held fingers, cancel/drag-out,
completion, hidden nodes, foreign scenes, expired owner and teardown. Its native
API boundary is a double, not the iOS game binary. Existing entry, retained UI,
persistence/final Attempt, spectator, camera and Safe Mode regressions remain.

RankedAudioTransitionNativeTest executes the actual production audio methods
with FMOD API doubles. It models first-play-only initial seek, 26/182s buffers,
zero transition seeks, repeated R3 fallback requests, active/retiring starvation,
resume, recovery without restart, sustained starvation, fresh buffers over three
rounds, shared song IDs, wrong offset rejection, retained lifetime, interrupted
DSP fades, failed DSP setup and bounded retries/teardown. These tests do not
decode actual MP3s or measure speaker output.

Acceptance still requires iPad testing with Corum enabled: Pause after GO in
R1/R2/R3, native resume/restart, X and both surrender scopes, round/result input,
menu-to-match/R3/result music and background/resume. Record the new dispatch,
PauseLayer and buffer logs. A passing CI build is not device verification or
proof that the reported audible symptom is gone.
