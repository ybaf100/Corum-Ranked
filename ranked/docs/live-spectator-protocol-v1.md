# Corum Ranked Live Spectator Protocol v1

Status: **Phase 1 local-dummy safety implemented in beta17; remote presentation is deferred to Phase 2. Native-device gameplay quality is not yet certified.**

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

`Finished` and spectator eligibility remain separate concepts. A retryable death, an active Attempt, a valid pending Start lease, or a legal Final/LAST ATTEMPT Start keeps the viewer non-spectating even if the most recent visual Attempt has ended. In LAST ATTEMPT, the trigger side may spectate while the target side retains its legal play right.

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
inputEdges: array (reserved in protocol v1; each edge has elapsedMs, button, down, player)
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

Every new Attempt or generation must begin at sequence `0`. A timed-out or invalid stream is deleted, so its publisher receives a conflict on a non-zero continuation and starts a fresh generation. Viewer callbacks are also bound to the current match/round/Death Match/level context; a response completing after that context changes is discarded before its packets are parsed.

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

Buffers are removed when the authoritative Attempt ends, the runtime slot changes, Practice becomes active, the round or match leaves a live spectator context, the generation changes, the stream times out, or a sequence/context validation fails. The relay exposes only protocol packet fields; its internal server receipt timestamp is not serialized.

## Client behavior

The normal path keeps the already-authorized canonical PlayLayer alive after the viewer's final legal Attempt. Once the server returns `spectator.active`, that same scene becomes read-only spectator presentation:

- local Ranked Attempt Start/Progress/End/Clear transmission is bypassed;
- spectator Practice toggling is ignored;
- only the exact local dummy's death is suppressed; it cannot create result evidence;
- the local player remains a hidden LOCAL dummy, never a remote pose renderer;
- at 99% its own physics update freezes; independent end-check/animation/completion guards prevent vanilla Clear even after a large-step or direct-callback escape;
- exact scene-level `savePercentage` and `saveNewScore`, and local `showNewBest`, are blocked independently of Practice mode, including synchronous quit/reset saves;
- opponent name and `LIVE SPECTATOR` remain visible through the Ranked HUD;
- if live packets are unavailable, the pre-existing `LIVE PROGRESS n%` spectator UI remains the fallback.

Re-entry after reopening the Ranked popup uses a separate read-only launch authority. It reuses the exact same `RankedLevelPreparation` online metadata proof, current context/generation, exact level ID, geometry readiness, effective-song readiness (including the RobTop fast path and Jukebox path), modal hold, and verified native LevelInfo Play item. It does **not** call `armCurrentLevelForGameplay()` and latches spectator API suppression during `PlayLayer::init`, before the first update or quit callback.

No raw saved-level fallback, synthetic `GJGameLevel`, direct `PlayLayer::scene`, stale LevelInfo reuse, or metadata-proof bypass was introduced. The existing vanilla settings/options popup remains available and continues to hold automatic Play while open.

## Phase 1 safety and regression boundary

`SpectatorLocalSafety` is a per-scene latch. Activation requires existing server spectator permission, no live local visual/transport/armed Attempt, a non-test Ranked scene, and neither Placement nor user Practice. A state refresh cannot turn a converted scene back into a scoring player. Pending handoff blocks evidence/new transport but does not enable noclip before server permission.

All public runtime Start/Progress/End entries check the dummy/handoff firewall before armed-start or missing-start recovery. Practice presence is also excluded. Already-observed Start/End FIFO entries keep their idempotent delivery/ACK semantics; the firewall does not erase real gameplay evidence. AP/title/achievement evaluation, winner, rating, LP/MMR, server spectator eligibility and Placement state machines are unchanged. No new DB schema or gameplay telemetry was added.

The 99% limit is local `PlayerObject::update` suppression, not a world/PlayLayer pause or `onQuit`. The normal gameplay tick and polling HUD continue. Both completion callbacks and `checkForEnd`/end-animation entry points are guarded, and exact-object persistence sinks remain blocked even if the local progress counter crosses 100%. The temporary spectator Practice frame flag has been removed; user Practice keeps its existing separate transitions.

Regression coverage includes strict C++ policy/lifecycle adapters, source-hook wiring, and the actual production death/completion/save/physics callback bodies compiled and executed against lifecycle doubles. A scoped real-gameplay End lets the final genuine record finish saving before dummy conversion; subsequent dummy saves are blocked. Native platform compilation is performed by automatic Debug CI, not claimed to be an actual GD lifecycle/device run. Device QA must still verify collision/reset/dual/platformer/large-step/quit/reconnect behavior and normal versus spectator New Best persistence.

Every patch, including a minor hotfix, increments the beta version and synchronizes the mod manifest with the server's exact client-version gate. Release workflow remains manual-only and is not invoked by this work.

## Current unsupported / unverified scope

The following are not declared complete:

- exact remote physics/input replay for arbitrary Geometry Dash levels;
- dual-player, platformer, gravity/mode transitions, camera triggers, and velocity correction beyond the reserved protocol fields;
- opponent cosmetic reproduction in the remote ghost;
- CorumRemotePlayer / separate PlayerObject rendering, remote x/y/rotation, camera follow, remote game mode, cosmetics and interpolation (Phase 2); Phase 1 does not display a live remote ghost;
- native input suppression beyond the non-authoritative dummy/evidence firewall; no unverified GD input hook was invented;
- WebSocket transport; the current implementation intentionally uses the verified Geode HTTP API;
- native-device confirmation of spectator popup/reconnect entry, including arbitrary-level compatibility;
- measured real-device spectator latency/correction quality;
- confirmation that the earlier Round 3 ~5 second hitch is fully fixed on affected Windows/Android hardware. The FMOD seek source-level blocking candidate remains fixed, but affected-device native tracing is still required.

## Diagnostics

Client fallback reasons are explicit protocol strings such as:

- `UNSUPPORTED_PROTOCOL_VERSION`
- `PACKET_MATCH_MISMATCH`
- `PACKET_ATTEMPT_MISMATCH`
- `PACKET_CONTEXT_MISMATCH`
- `STREAM_CONTEXT_MISMATCH`
- `STALE_GENERATION`
- `SEQUENCE_GAP`
- `STREAM_TIMEOUT`
- `POSE_DISCONTINUITY`
- `NETWORK_ERROR`

Generation restart after an HTTP `409` is logged. Stream reset/wait/fallback diagnostics include match context, Attempt, generation, sequence, and request RTT. Applied-packet diagnostics include packet count, publisher elapsed time, request RTT, and maximum keyframe correction; rejected large corrections log their measured distance. These values diagnose transport/correction behavior but are not a certified end-to-end latency measurement.

## Native-device verification procedure

Run on affected Windows and Android devices, with a second account as the opponent:

1. Verify ordinary retryable death never enters spectator mode.
2. Verify Final/LAST ATTEMPT pending Start lease preserves the legal start.
3. Exhaust the viewer's legal Standard start rights, then keep the opponent active; confirm same-scene live pose begins and no local Attempt API is emitted.
4. Close/reopen the Ranked popup while spectator authority is active; confirm canonical LevelInfo metadata/song preparation enters the read-only PlayLayer without an Attempt Start.
5. Enter spectator before the opponent starts; confirm waiting UI, then automatic transition when the opponent starts.
6. Repeat Death Match with viewer Attempts 1, 2, and 3; live spectator must become eligible only after authoritative Attempt 3 is completed.
7. Toggle Practice on the active player; verify no live packets are exposed and the other side sees fallback/waiting only.
8. Introduce network loss long enough to exceed 1,500 ms; verify immediate progress-UI fallback and no crash.
9. Reconnect/new Attempt; verify old round/Death Match/Attempt/generation packets are not rendered.
10. Complete the round/match; verify normal authoritative result UI wins over the live relay.
11. Record actual end-to-end latency, interpolation quality, correction frequency, pose discontinuities, and transition-trace timings on Windows, Android32, and Android64.
12. Separately reproduce the historical Round 3 hitch on affected hardware and collect native timing evidence; do not infer its resolution from spectator behavior.
