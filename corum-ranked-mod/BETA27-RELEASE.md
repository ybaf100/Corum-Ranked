# v0.5.0-beta.27 — post-entry Spectator hotfix

Authoritative starting main: d1c8f6d01aab758e63c82f01f578bdb272e7072d (beta26).
Manifest and server exact compatibility gate advance together; protocol and Debug
JSONB format remain v2. No manual Release or force push.

## Evidence and scope

Geode 2026-10-06 00.39.11(1).log and ScreenRecording_10-06-2026 00-41-15_1(1).mp4
confirm successful native Play entry, leading playback elapsed near 1239–1338ms,
and repeated sequence20 distance729.7268 rejection of recording ef17251a-8b90-46d6-96c1-dee58db07f1e.
The earlier recording rejects at sequence21 distance728.71313. Coordinates, modes,
camera and native events are absent. The exact native cause cannot be recovered
from these files. The regression fixture preserves observed sequence/distance and
explicitly identifies its synthetic coordinates/events. No old data is rewritten
or given fabricated teleport authority.

## Changes

- Ranked-owned gameplay audio handoff occurs after successful native init for
  Ranked, Debug RECORD and TEST. LevelInfo/failed init keep lobby ownership. The
  existing retained Ranked onEnter/refresh/audio-mode lifecycle restores current
  lobby music, with channel idempotency. No broad stopAllMusic is added.
- Server playback validates original DB duration and terminal, then subtracts
  first elapsed time from every relayed frame. First poll can emit t=0; relative
  movement/death/Clear timing and gameplay speed are preserved.
- Independent native frame observation waits for native attempt time and two
  stable moving frames; completed world update samples pose/rotation/actual camera
  at the existing 50ms HTTP cadence. Ranked journal settling remains unchanged.
- Native portal/rising teleport flag and Spider callbacks create bounded endpoint
  evidence. Verified snaps clear interpolation and exact-apply current pose/camera
  without Attempt/generation/epoch reset. Unverified 400-unit violations reject.
- Native camera origin travels through capture/HTTP/JSONB/server clock/relay/shared
  codec/renderer. Normal camera interpolates; native camera relocation and terminal
  exact-hold. Missing/nonfinite/huge camera drops only camera to pose-follow.
- Remote Player is allocated on the first valid packet and retained across
  waiting/fallback. Native spectator updateAttempts is guarded alongside existing
  death/completion/New Best/percentage/evidence/transport isolation.
- Debug-only sequences18–22 log position deltas, timing, mode/state and camera;
  verified-snap/history-reset logs are transitions, not per-frame production logs.

## References and licensing

[AttemptPlaybackMod](https://github.com/manshoety/AttemptPlaybackMod): independent
P1/P2 tracks, native update/rotation capture, attempt-time alignment, frame state,
pooling and safe replay lifecycle. Its current recursive main tree has no LICENSE;
no implementation was copied. Its own camera warnings reinforce using publisher
native camera, not replay-camera code.

[Globed](https://github.com/GlobedGD/globed2): persistent render-only remote objects,
separating interpolation from discrete snaps and live lifecycle. No new source was
copied. Existing MIT companion cleanup attribution remains intact.

Current Geode 5.8.2 / GD 2.2081 bindings were inspected for cameraPosition,
PlayerObject update/updateRotation/spiderTestJump, native teleport flag and
GJBaseGameLayer teleportPlayer/update. SDK itself was not modified.

## Validation

63 native/source tests pass, including Debug ON/OFF audio and native capture
callback execution; six C++ domain executables pass with strict warnings.
114 rules and 161 server tests pass; typecheck, workspace build and both existing
Apps Script validators pass. The HTTP/PostgreSQL test hands actual normalized
server replies into the production C++ decoder, complete Remote Player and native
camera adapter, for both death and finished terminals; no Ranked tables change.
Automatic GitHub native platform builds/CI are reported separately after push.
Native doubles do not certify actual device physics/rendering/audio.

## Next iPadOS test

RECORD expected order (existing LevelInfo entry markers remain):

```text
[DebugSpectate] PlayLayer entered level=88350599
[DebugSpectate] Ranked gameplay audio ownership acquired
[DebugSpectate] RECORDING active
```

TEST expected markers:

```text
[DebugSpectate] PlayLayer entered level=88350599
[DebugSpectate] Ranked gameplay audio ownership acquired
[SpectatorLive] first normalized packet sequence=0 elapsedMs=0
Spectator Remote Player created
Spectator camera target acquired
[SpectatorLive] verified discontinuity type=TELEPORT
[SpectatorLive] interpolation history reset for snap
```

Snap markers occur only when a verified native event exists, and need not be
TELEPORT for every map section. Batch/HTTP latency can make the first observed
packet sequence greater than zero; its elapsed times still use the normalized axis.

1. Record VOICE again on beta27 through a real terminal, then TEST that recording.
2. During LevelInfo hear lobby BGM; after entry hear VOICE only. On return hear the
   current Ranked lobby mode once; on full GD menu exit hear normal menu music.
3. Confirm no recording-leading blank delay; HTTP RTT/poll latency remains allowed.
4. Compare actual jumps/portals and camera origin, no mid-Attempt Remote Player
   destroy/recreate; inspect sequence18–22 and snap logs for native cause.
5. Confirm exact terminal pose/camera and playback-complete HUD, hidden/frozen
   dummy, no TEST attempts/New Best/percentage/evidence/Ranked score changes.
6. Old beta26 TEST can now supply forensic x/y/state data, but lacks original
   camera/native-event proof. Do not count synthetic regression as device success.

## Deferred

Phase5: full Dual/P2/cosmetics/effects, jitter/loss/reconnect rewrite, complex
platformer camera, camera zoom/rotation and all-device E2E. Phase6: WebSocket,
DB/network/poll/heartbeat/pool optimization and load tests. No Web repo changes.
