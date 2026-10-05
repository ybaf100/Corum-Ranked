# Live Spectator Phase 4 — protocol v2 (beta27 hotfix)

Exact client/server gate: **v0.5.0-beta.27**. Production protocol **2** and Debug
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
- Optional camera/visibility/native discontinuity fields are described below;
  velocity/player2 reserved fields remain unused. Input
  edges are not replayed; there is no opponent physics or portal reconstruction.

The existing envelope retains match/round/Death Match sequence/Attempt/generation,
sequence and elapsedMs. Stale identity/generation, reversed time/sequence, corrupt
state and unsupported protocol fail closed. Terminal streams cannot resume in the
same Attempt; a fresh server Attempt/context re-acquires the visual and camera.

## Native capture and rendering

beta27 observes native PlayerObject update/rotation and teleport/Spider callbacks.
The completed GJBaseGameLayer update samples the latest pose with the actual
`m_gameState.m_cameraPosition`, at the existing 50ms HTTP opportunity. Unsampled
native event endpoints are retained until a sample is submitted. Debug visual
capture requires advancing native attempt time and stable moving gameplay frames;
it does not share the Ranked Attempt journal's unchanged 500ms settling window.

Optional additive v2 fields (older format-v2 recordings remain readable):

```json
{
  "cameraX": 700,
  "cameraY": -20,
  "cameraSnap": true,
  "visible": true,
  "discontinuity": {
    "type": "TELEPORT",
    "fromX": 1000,
    "fromY": 40,
    "toX": 1729.7268,
    "toY": 40
  }
}
```

These coordinates are synthetic regression values using the observed iPadOS
rejection magnitude, not the missing original recording coordinates. Publishers
mark TELEPORT only from the native portal callback or rising native teleport flag;
SPIDER_SNAP requires a native Spider callback/gravity transition. Endpoint vectors
must be finite/bounded (Teleport length <=100,000; Spider |dx| <=400, |dy| <=2400).
Viewer endpoints must align within the existing 400-unit continuous-motion bound
of the adjacent accepted samples. A generic large-jump flag is not accepted.

Verified relocation clears previous interpolation and applies current position,
rotation and camera exactly. It does not reset Attempt/generation/epoch or allocate
another Remote Player. Camera-only native frame relocation uses cameraSnap without
bypassing pose checks. Normal camera samples interpolate with normal pose samples;
terminal samples hold exact pose/camera without extrapolation. Discrete state uses
accepted sample boundaries. Camera fields are a finite bounded pair; invalid or
missing camera drops only that optional presentation and uses safe pose-follow.
Remote objects are allocated on the first valid pose and retained across transient
waiting/fallback; true context/Attempt/generation reset or exit still cleans them.

Debug server playback subtracts the first persisted elapsed time after validating
the original DB duration/terminal. Inter-frame and terminal timing remain unchanged.
The first in-scene HTTP poll starts the server clock and can emit elapsedMs=0.
Persisted JSONB is not rewritten and TEST never downloads a local replay file.

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
sample. Explicit native evidence is checked before this legacy bounded-state
fallback. Unbounded/unknown corrections still reject; existing recordings lacking
evidence are not silently authorized. Full complex camera parity remains Phase 5.

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
