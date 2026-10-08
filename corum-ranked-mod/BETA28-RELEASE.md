# v0.5.0-beta.28 — final spectator viewport and publisher-clock presentation

Authoritative starting main: bb35fc76a5934a6d8405545a38180281e0d1cd6d (beta27).
Client manifest, server exact gate, npm workspace versions/lock and compatibility
tests advance to beta28. Protocol and Debug recording format remain v2. Server
capture/persistence/playback clock, audio and HTTP publisher/poll rates are unchanged.

## Device evidence and root cause

Geode 2026-10-06 10.59.06.log and ScreenRecording_10-06-2026 11-01-04_1.mp4 show
successful RECORD/TEST, first normalized elapsed 0, a separate moving Remote Player,
matching recorded/relayed camera and working gameplay audio. The video shows the
remote leaving the viewport and a 0% vanilla progress bar. No previous 729-unit
rejection occurs in this recording. The exact logged seq18–22 coordinates are
preserved in tests/fixtures/debug-spectate-ipados-beta27.json, without synthetic events.

The old camera hook applied spectator state before calling vanilla; the viewer
also called vanilla camera manually. That allowed the dummy/native camera path to
own the final transform, while a target-acquired log only described policy state.
A native double executing the production adapter and hook reproduces this ordering
failure, then checks the final world-space-to-screen transform in the fixed path.
The supplied device evidence has no before/after native-camera instrumentation:
actual iPadOS overwrite and the fixed viewport still require the next device test.

The previous receive-time interpolator retained only the last two poses from an
HTTP batch, lerped for 50ms and then held until the next response. Publisher sample
intervals were discarded, causing a stop-and-go presentation under batched HTTP.

## Camera and progress

- One outer camera hook owns a rendered frame. It obtains one runtime sample,
  applies the dedicated Remote Player, executes native camera once, then applies
  the final recorded viewport. A director-frame guard prevents heartbeat/native
  lifecycle duplicates; heartbeat fills a stopped vanilla lifecycle only.
- Viewer polling/HUD no longer call camera or apply another raw/latest sample.
  Valid publisher camera has priority. Its origin is applied to camera state and
  the actual object/shader-layer affine transforms after native calculation;
  native background/ground-art update keeps native zoom/world behavior.
- Missing camera uses a camera-only scoped remote target. CCNode position and
  native coordinate mirrors are saved/restored exactly; PlayerObject's gameplay
  setPosition override is bypassed. Invalid camera and WAITING hold the safe
  viewport. Local dummy and remote renderer remain separate objects.
- Progress changes only the fill sprite/label. Ranked opponent telemetry has
  priority; Debug classic uses rendered x/length and timestamp levels use rendered
  time in native 240Hz units. Remote Clear can display 100% without local completion,
  journal progress, New Best, percentage saves or scoring. The timestamp fallback
  is relative to stream start; exact native time offset/complex platformer progress
  without authoritative telemetry is still a presentation limitation.
- Debug-only before/after vanilla/final logs compare packet, native origin and
  object-layer/world transform for at most eight distinct initial/seq18–22 samples.

## Buffer, measurements and starvation

All accepted samples enter one deque keyed by publisher elapsedMs/sequence.
Continuous position, shortest-angle rotation and camera share the same rendered
sample; modes/size/gravity/flip/visibility/terminal change at sample boundaries.
The remote and camera adapters apply that sample directly without another lerp.

The buffer holds the first pose visibly during warm-up, then advances its publisher
clock at 1x, independent of response timing or render FPS. Verified snap resets
history and exact-applies pose/rotation/camera; terminal exact-holds only when its
publisher timestamp is reached. No unbounded extrapolation or catch-up teleport.

Limits: 64 samples / 2000ms span; render budget 100–350ms, based on EWMA response
arrival plus observed sample cadence (at least two samples). On first response the
estimate uses RTT plus the existing post-ACK 80ms poll gap. Logged RTT is 124/108ms,
mean 116ms across TWO logged responses only. That predicts an initial budget around
238–254ms, not a measured whole-session cadence. Existing code schedules polling
80ms after an ACK, so possible serialized intervals are about 188–204ms plus render
scheduling. The attached log does not measure actual arrival intervals/typical batch
size; only the first applied batch of one packet is logged. New once-per-second
Debug metrics report RTT/arrival means, packets/response, samples/span/budget,
render/latest publisher time and starvation count for actual device measurement.

Short starvation holds the last safe sample. Persistent starvation (250ms) shows
BUFFERING and resumes at 1x after renewed headroom; overflow enters the existing
safe fallback. Context/attempt/generation/epoch reset, verified snap and exit clear
history. Long gaps/reconnect recovery remain deferred.

## References and license

Current [Globed camera](https://github.com/GlobedGD/globed2/blob/da8920062f6ae8a0357f3a0f53e0d431d23bc6d0/src/core/hooks/GJBaseGameLayer.cpp)
and [Interpolator](https://github.com/GlobedGD/globed2/blob/da8920062f6ae8a0357f3a0f53e0d431d23bc6d0/src/core/game/Interpolator.cpp)
were inspected for temporary native camera targeting, timestamp bracket selection,
latency modes and canonical progress. Corum independently implements the scoped
fallback and HTTP publisher-clock buffer; actual publisher camera remains primary.
[AttemptPlaybackMod](https://github.com/manshoety/AttemptPlaybackMod) remains a
record-time/frame-lookup/native capture reference. Its inspected tree has no LICENSE;
no source was copied. Existing Globed MIT companion attribution remains packaged.
Official Geode/GD2.2081 bindings were inspected, not modified.

## Validation and next iPadOS QA

Local validation: 65 native/source tests, seven strict C++ domain executables,
114 rules tests and 162 server tests pass. Workspace typecheck/build and both
Apps Script validators pass. Automatic four-platform native CI is reported with
the final commit. Native doubles execute the actual camera hook/complete renderer/adapter,
seq18–22 viewport transforms, duplicate lifecycle guard, affine parent/scale/rotation,
missing/invalid/waiting camera, early-return/throw restoration and 50%/100% UI safety.
Timestamp-buffer tests cover 3-packet batches 120ms apart, irregular 100–130ms and
serialized 188–204ms responses, 60/120/144/240Hz, snaps, terminal, starvation and bounds.
The existing real HTTP/PostgreSQL/server-clock test feeds actual wire replies to the
production decoder/remote/camera and verifies death/Clear plus unchanged Ranked data.

On the same iPadOS device, TEST the same server-backed recording on beta28:

1. After first normalized packet, see the remote immediately and retain correct
   framing while movement continues; no dummy-camera flash or escape off the right.
2. At seq18–22, compare final native cameraX with 125.80777, 141.38675, 156.96573,
   172.54471, 190.72018; confirm actual viewport motion and player-camera x offset165.
   Values between boundaries should interpolate on the same publisher timeline.
3. Compare the three bounded [SpectatorCamera] stages and actual object-layer
   transform; a target-acquired log alone is insufficient evidence.
4. Inspect aggregate buffer metrics under real HTTP; no repeated movement/pause,
   player-camera phase lag, duplicate camera calculation, jitter or long blank start.
5. Progress follows the remote; final UI100 is allowed while local dummy stays
   hidden/frozen, attempts/New Best/saved percentage/Ranked evidence remain unchanged.
6. Verified teleport snaps exactly; terminal pose/camera holds until normal exit.
7. Hear VOICE only, retain normal lobby return/cleanup; RECORD gameplay, ordinary
   Ranked/Placement/Practice camera/progress/FPS remain unchanged.

No post-fix iPadOS capture has been run here. Device visual smoothness/framing,
background/culling and camera-trigger behavior remain pending this checklist.
Phase5 still owns full Dual/P2/cosmetics, advanced loss/reconnect recovery, complex
platformer camera, full camera zoom/rotation and all-device E2E. Phase6 owns WebSocket,
network/poll/publisher/DB/transfer optimization. No manual Release or Web changes.
