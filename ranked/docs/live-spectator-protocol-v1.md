# Corum Ranked Live Spectator Protocol v1

Status: **implemented MVP, native-device gameplay quality not yet certified**.

This document describes the live gameplay spectator relay added after `v0.5.0-beta.14`. It is deliberately non-authoritative: Ranked scoring, clears, wins, anti-cheat decisions, Attempt ownership, and match transitions continue to use the existing HTTP Attempt lifecycle only.

## Transport and authority

Protocol version: `1`.

The current cross-platform MVP uses authenticated short-poll HTTP rather than introducing an unverified Geometry Dash/Geode WebSocket ABI. The relay is memory-only and ephemeral. No spectator packet is written to PostgreSQL or used by MatchService scoring/result code.

Routes:

- `POST /api/ranked/matches/:id/spectator-live/publish`
- `GET /api/ranked/matches/:id/spectator-live`

Both routes use the existing Ranked session bearer token and `x-match-token`. The server resolves the caller's viewer side through the existing match access path. A subscriber is accepted only when the viewer-relative public state says `spectator.active === true`.

The publisher is additionally checked against the server runtime state's active authoritative Attempt ID for the caller's side. Practice presence suppresses the stream before packets enter the relay.

## Eligibility

Standard rounds do not duplicate client-side eligibility logic. They consume MatchService's existing viewer-relative `spectator.active`, which already accounts for active Attempts, pending Start leases, Final/LAST ATTEMPT start windows, and target-side LAST ATTEMPT rights.

Death Match adds the equivalent viewer projection:

- viewer has `attemptsCompleted >= 3`;
- viewer has no active Attempt;
- opponent either has an active Attempt or has completed fewer than 3 Attempts.

When the opponent still owns an unused Attempt but has not started it, the spectator remains eligible in a waiting state. Once both sides have completed three Attempts and neither has an active Attempt, spectator eligibility ends and authoritative result handling takes over.

## Packet schema

Publisher packets contain:

```text
protocolVersion: number
matchId: string
roundNumber: number
deathmatchSequence: number | null
attemptId: string
generation: positive integer
sequence: non-negative monotonic integer
elapsedMs: non-negative integer
inputEdges: array (reserved in protocol v1)
keyframe:
  x: number
  y: number
  rotation: number
  dead: boolean
  finished: boolean
  velocityX?: number
  velocityY?: number
  mode?: string
  gravity?: number
  cameraX?: number
  cameraY?: number
  dual?: boolean
  player2X?: number
  player2Y?: number
  player2Rotation?: number
```

The shipping client currently publishes only ABI-confirmed pose fields (`x`, `y`, `rotation`, `dead`, `finished`). The optional mode/gravity/velocity/dual/camera fields and input-edge capture are intentionally **not fabricated** until the relevant Geometry Dash/Geode fields and cross-platform semantics are verified. The server schema reserves them for a compatible protocol-v1 extension.

## Stream identity and stale-data rejection

A stream is bound to:

`match ID + round/deathmatch slot + sender side + authoritative Attempt ID + generation`.

The server and client reject or discard:

- another match;
- another round/Death Match sequence;
- another Attempt ID;
- stale generation;
- reverse/duplicate sequence;
- a publisher jump larger than 8 sequence numbers;
- unsupported protocol version;
- invalid/non-finite pose;
- client pose discontinuity over 400 world units;
- relay timeout.

An Attempt or generation change resets the client buffer. Old Attempt packets cannot be applied after reconnect.

## Buffer and cadence

Server limits:

- retention window: **3,000 ms**;
- maximum packets per stream: **72**;
- stream timeout: **1,500 ms**;
- maximum accepted publisher sequence gap: **8**.

Client target cadence:

- pose publish opportunity: 50 ms;
- spectator poll: 80 ms;
- polling-fallback/recovery poll: 150 ms.

HTTP requests are serialized, so actual cadence is network/RTT dependent and is not claimed to equal a fixed 20 Hz on real devices.

Late join returns the bounded active-Attempt buffer. The client consumes buffered keyframes in sequence order, approaches the current pose, then continues with incremental packets. Position and rotation use short interpolation. A large discontinuity is treated as an unsafe stream and falls back rather than snapping through unknown level state.

## Client behavior

The normal path keeps the already-authorized canonical PlayLayer alive after the viewer's final legal Attempt. Once the server returns `spectator.active`, that same scene becomes read-only spectator presentation:

- local Ranked Attempt Start/Progress/End/Clear transmission is bypassed;
- spectator Practice toggling is ignored;
- local collision/death callbacks do not create a result event;
- the local player object is used only as a remote pose renderer;
- opponent name and `LIVE SPECTATOR` remain visible through the Ranked HUD;
- if live packets are unavailable, the pre-existing `LIVE PROGRESS n%` spectator UI remains the fallback.

No raw saved-level fallback, synthetic `GJGameLevel`, direct `PlayLayer::scene`, or metadata-proof bypass was introduced. Ranked Level Prepare remains authoritative for normal gameplay entry.

## Current unsupported / unverified scope

The following are not declared complete:

- exact remote physics/input replay for arbitrary Geometry Dash levels;
- dual-player, platformer, gravity/mode transitions, camera triggers, and velocity correction beyond the reserved protocol fields;
- opponent cosmetic reproduction in the remote ghost;
- automatic reconstruction of a new spectator PlayLayer after reopening/reconnecting from the Ranked popup. The safe current behavior there remains polling spectator fallback because no new raw/synthetic scene entry path was added;
- WebSocket transport; the current implementation intentionally uses the verified Geode HTTP API;
- measured real-device spectator latency/correction quality;
- confirmation that the earlier Round 3 ~5 second hitch is fully fixed on affected Windows/Android hardware. The FMOD seek source-level blocking candidate remains fixed, but affected-device native tracing is still required.

## Diagnostics

Client fallback reasons are explicit protocol strings such as:

- `UNSUPPORTED_PROTOCOL_VERSION`
- `PACKET_MATCH_MISMATCH`
- `PACKET_ATTEMPT_MISMATCH`
- `STALE_GENERATION`
- `SEQUENCE_GAP`
- `STREAM_TIMEOUT`
- `POSE_DISCONTINUITY`
- `NETWORK_ERROR`

Generation restart after an HTTP `409` is logged. Device validation should capture the existing transition trace together with these reasons, relay sequence/generation, server response time, and observed visual correction behavior.

## Native-device verification procedure

Run on affected Windows and Android devices, with a second account as the opponent:

1. Verify ordinary retryable death never enters spectator mode.
2. Verify Final/LAST ATTEMPT pending Start lease preserves the legal start.
3. Exhaust the viewer's legal Standard start rights, then keep the opponent active; confirm same-scene live pose begins and no local Attempt API is emitted.
4. Enter spectator before the opponent starts; confirm waiting UI, then automatic transition when the opponent starts.
5. Repeat Death Match with viewer Attempts 1, 2, and 3; live spectator must become eligible only after authoritative Attempt 3 is completed.
6. Toggle Practice on the active player; verify no live packets are exposed and the other side sees fallback/waiting only.
7. Introduce network loss long enough to exceed 1,500 ms; verify immediate progress-UI fallback and no crash.
8. Reconnect/new Attempt; verify old generation/Attempt packets are not rendered.
9. Complete the round/match; verify normal authoritative result UI wins over the live relay.
10. Record actual end-to-end latency, correction frequency, pose discontinuities, and transition-trace timings.
11. Separately reproduce the historical Round 3 hitch on affected hardware and collect native timing evidence; do not infer its resolution from spectator behavior.
