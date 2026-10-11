# beta54: Admin common song volume

- Open Debug → Admin as `hwanhee1`, enter the existing debug password and press
  **Song volumes (all players)** / **곡별 공통 볼륨**.
- Select a configured song with `<` / `>`, adjust 0–100% and press Save.
  One setting is stored per GD song ID, shared by all configured uses of that
  song. Unsaved slider changes do not affect other players.
- The server writes `ranked_audio_volumes` (migration 0019). These operational
  settings survive restarts, source-sheet refreshes and player/database resets.
  No Apps Script redeployment, new password or environment variable is needed.
- `/api/ranked/config` exposes `client.audio.resources[].volumePercent` with
  no-store caching. Existing five-second session heartbeats carry `audioVolumes`
  so connected clients receive updates without a new polling channel. Network
  delays can extend delivery time. Reads/writes use the existing server-checked
  Admin account, password and current-session restrictions.
- Common gain multiplies the existing GD music gain, Ranked 80% mix and personal
  menu slider. It only affects Ranked-owned lobby/pre-game/result music channels;
  native PlayLayer music, effects and GD settings are unchanged.
- Gain-only updates preserve playing/prepared streams, seek position and DSP
  ramps. Both the incoming and fading-out tracks use their own current gain.
- Client manifest/server exact version gate and npm packages are beta54. New
  Korean label glyphs are included in all menu font weights. External image-pack
  inputs are unchanged; no external asset pack is produced for this patch.

Validation uses PostgreSQL (PGlite), compiled Corum callbacks/classes and FMOD/
Cocos/network boundary doubles. It does not establish native device acceptance.

Local checks: 125 rules tests and 284 server tests passed; typecheck and build
passed. All 148 source/native-boundary cases passed, including focused reruns
after fixing the DSP-gain update and adding seven Korean glyphs to each menu
font weight. Final modal tests cover server load/save errors, retry, exact
session ownership, pending request cancellation, Debug OFF and Android strings.
