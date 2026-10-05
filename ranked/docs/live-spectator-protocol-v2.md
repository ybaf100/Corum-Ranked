# Live Spectator Phase 4 — protocol v2 (beta25)

Exact client/server gate: **v0.5.0-beta.25**. Production protocol **2** and Debug
recording format **2** require complete gameplay presentation state. V1 is rejected;
an absent/unknown mode never silently becomes Cube. The old v1 document is historical.

Existing authenticated HTTP publish/subscribe routes, scoped Debug routes, 50ms
publish opportunity, 80ms viewer polling, 150ms fallback polling, 3s/72-packet
buffer and 1.5s timeout remain. Actual publish cadence depends on HTTP RTT. No
WebSocket or general DB/polling optimization is included.

## Required keyframe

```json
{
  "x": 100,
  "y": 80,
  "rotation": 90,
  "mode": "CUBE",
  "mini": false,
  "upsideDown": false,
  "flipped": false,
  "dual": false,
  "dead": false,
  "finished": false
}
```

- Mode enum: CUBE, SHIP, BALL, UFO, WAVE, ROBOT, SPIDER, SWING.
- Every discrete field is a required boolean. Dead and finished cannot both be true.
- Coordinates/rotation are finite, with |x/y| ≤ 10,000,000 and |rotation| ≤ 100,000.
- `upsideDown` is native gravity; `flipped` is native main-layer visual orientation.
- `dual` is observed from native game state. Phase 4 renders one opponent object;
  dual enters explicit `DUAL_UNSUPPORTED` polling fallback rather than false parity.
- Existing optional velocity/camera/player2 reserved fields remain unused. Input
  edges are not replayed; there is no opponent physics or portal reconstruction.

The existing envelope retains match/round/Death Match sequence/Attempt/generation,
sequence and elapsedMs. Stale identity/generation, reversed time/sequence, corrupt
state and unsupported protocol fail closed. Terminal streams cannot resume in the
same Attempt; a fresh server Attempt/context re-acquires the visual and camera.

## Native capture and rendering

`SpectatorPlayerCapture` reads GD 2.2081 PlayerObject mode flags, vehicle size,
gravity and main-layer flip. The exact bindings were inspected at
`geode-sdk/bindings@7f6c2a75742856de88dad354e576dcff8a28e881` (Geode 5.8.2).
Both production publish and Debug RECORD use this function and shared pose codec.

CorumRemotePlayer owns a separate render-only PlayerObject. Mode changes reuse
native toggleFly/Roll/Bird/Dart/Robot/Spider/SwingMode plus default frame helpers;
mini uses togglePlayerScale/updatePlayerScale, including Ship gravity refresh.
Gravity is a visual flag/main-layer orientation, not remote flipGravity physics.
Only transitions initialize modes/size. Received rotation is applied directly.
The object never joins collision sections, m_objects or local player pointers.
Owned native trails/particles stay isolated and are cleaned up before teardown.

Dead applies a 250ms presentation-only fade at the exact terminal pose; it never
calls local/native gameplay destruction. Finished remains visible/frozen with a
CLEARED HUD cue. New Attempt/context recreates a clean visual with opacity/state
reset and camera re-acquisition. Phase-1 dummy guards, Phase-2 separation and
Phase-3 native X/Y camera ownership remain unchanged.

## Terminal delivery and authority

A real death/Clear captures terminal immediately. A pending Start ACK supplies
the real Attempt ID; a pending live ACK preserves contiguous sequence delivery.
The terminal then bypasses the normal 50ms delay, through its own bounded HTTP
request/retry state. Dummy/Practice/Placement do not publish these packets.

The server may accept only the exact authoritative just-ended Attempt's matching
dead/non-Clear or finished/Clear presentation for 1.5s. This handles End-vs-visual
HTTP ordering without reopening gameplay rights. Exact terminal ACK-loss retries
are idempotent. New Attempt, Practice, changed round/map/DM/match or timeout removes
old presentation. No visual packet changes score, Clear evidence, LP/MMR, AP or
Achievement state; the existing Attempt API remains the result authority.

## Interpolation and discontinuities

Position/rotation keep bounded interpolation. Discrete fields use the latest
accepted state, never numeric interpolation. Terminal poses are exact. The normal
400-unit correction check remains. A mode change or same-Spider gravity flip can
snap with |Δx| ≤ 400 and |Δy| ≤ 2400; large accepted snaps clear the previous lerp
sample. Unbounded/unknown teleport corrections still fall back. Complex teleport
and camera trigger parity requires device/map investigation in Phase 5.

## Debug compatibility and remaining limits

VOICE 88350599 remains fixed. Format-v2 keyframes bulk-persist in existing JSONB;
latest-completed selection filters exact format and revalidates frames. Old v1
data is left intact but excluded. TEST continues DB → server clock → production
relay → HTTP → decoder/interpolation → Remote Player → camera; no local replay.

The configured map pool stores metadata/IDs, not a reliable dual/teleport feature
inventory. Absence of dual cannot be certified from it. Player2/dual presentation,
cosmetics/full effects, advanced interpolation/jitter/reconnect, complex camera
and platformer parity and cross-platform device E2E remain Phase 5. Phase 6 retains
WebSocket/general performance work. Native-double/unit coverage is not device QA.
