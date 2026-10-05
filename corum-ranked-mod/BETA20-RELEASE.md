# Corum Ranked v0.5.0-beta.20 — Live Spectator Phase 3

## Implemented

- Retain the exact canonical Ranked PlayLayer after the viewer's genuine final legal Attempt ends. Existing server spectator permission and the Phase-1 local safety latch are required before presentation/camera ownership changes.
- A scene-owned presentation heartbeat continues Runtime polling, live viewer polling, remote rendering and camera application even when genuine vanilla completion has stopped gameplay postUpdate. Completion overlay handoff retains its callback owner during the one guarded native reset. No LevelInfo/menu round-trip or new Ranked Attempt is used.
- Local `m_player1` remains a separate invisible, frozen dummy. Phase-1 exact-player noclip, completion/99% defenses, transport/persistence firewalls and Practice separation are preserved. Retry cannot reopen the converted scene's local lifecycle.
- `CorumSpectatorCamera` follows the accepted/frozen frame of `CorumRemotePlayer`, not raw unvalidated packets or local PlayerObject state. Native cameraMoveX/Y establish forced camera control, and native updateCamera applies world transforms. No local player position/rotation writes or pointer swaps.
- Basic classic X look-ahead and grounded/airborne Y follow, using current viewport and world scale. First-packet wait and timeout hold the last safe camera position; remote packet absence never reveals the dummy. Existing Corum-font HUD displays compact WAITING/POLLING status.
- Same-generation HTTP timeout/network-gap recovery requires fresh contiguous validated packets. Old sequence/empty responses cannot revive stale poses. Existing 50ms bounded interpolation, server HTTP protocol and polling intervals remain unchanged.
- Match/round/map/Death Match sequence changes, authority loss, results and exit release camera ownership and owned remote nodes/live context. Dead remains hidden/frozen, finished remains frozen, and neither creates local gameplay evidence.

## References and scope

- Read-only Globed research: [Globed main da8920062f6ae8a0357f3a0f53e0d431d23bc6d0](https://github.com/GlobedGD/globed2/tree/da8920062f6ae8a0357f3a0f53e0d431d23bc6d0), GameCameraState and GJBaseGameLayer camera-follow ownership.
- Corum intentionally does NOT adopt Globed's temporary local-player-position substitution. Camera calls/forced state were checked against [official GD 2.2081 bindings d641858f3e1cd2c6d86013c799f224ebdb8ec1cd](https://github.com/geode-sdk/bindings/tree/d641858f3e1cd2c6d86013c799f224ebdb8ec1cd/bindings/2.2081).
- No substantial new Globed camera code copied. Existing Phase-2 MIT companion-cleanup attribution remains packaged.
- No rating/LP/MMR/Achievement/AP/title/Placement state-machine/server scoring/DB/protocol changes. Existing exact client/server version gate advances to beta20; Release stays manual-only and was not requested.

## Verification boundaries

Executable checks cover camera/context policy, Phase-1 safety, Phase-2 remote lifecycle, actual production scene callback bodies after stopped postUpdate, actual production decoder/interpolation bodies, and a mock Client-A telemetry -> complete production Client-B remote renderer/camera pipeline. Native doubles verify control flow/ownership, not GD ABI or device visuals. Native platform compilation is checked by the automatic Debug workflow.

No actual Geometry Dash device or two-client device E2E is available in the execution environment. This is an implemented/tested Phase-3 MVP candidate, not a claim of successful device E2E. Complex static/zoom/rotation/offset triggers and platformer parity are not certified.

## Required two-client device checklist (not yet executed)

1. A and B run beta20, required Geode dependencies and the same real Ranked map. Record ordinary camera/input/New Best first; B's live FINAL/LAST/DM Attempt must not be cut by early spectator permission.
2. B naturally finishes its final legal Attempt while A is still playing. Confirm End ACK -> server spectator.active -> SAME PlayLayer conversion, no duplicate Start, hidden frozen dummy, and completion overlay removed without replay/menu/download.
3. First packet wait must retain B's existing viewport, show WAITING, and never show B/0,0/stale opponent. When A moves, B must see the dedicated Cube and native camera X/Y must follow it through classic ground/high sections.
4. B input/collision/retry/Practice and remote death/finished must produce no local Start/Progress/End/Clear, score, achievement evidence or GD best/percentage save. A and match outcomes remain server-authoritative.
5. Brief packet gaps hold bounded pose/camera. At >1500ms timeout, remote hides and POLLING appears; fresh contiguous packets of the same generation resume. Stale sequences/generations never move camera. A's next Attempt appears with a new context.
6. Round/map/DM tie sequence, match result/cancel, authority loss and manual exit clean up remote/camera/live state; no dangling target, local scoring resurrection or stale follow on the next scene.
7. Recheck ordinary Ranked/Placement/real DM/LAST/FINAL and user Practice, achievement/AP/title displays, reconnect and existing HUD. Repeat on Windows/iOS/iPadOS/Android when devices are available.

## Later phases

- Phase 4: game modes, gravity, mini, dual/player2, death/respawn/Clear presentation, needed complex camera-trigger parity, LAST/DM visual-state details.
- Phase 5: cosmetics, advanced interpolation/jitter/loss, reconnect/disconnect, cross-platform device E2E, UI/Practice polish and final stabilization.
- Phase 6: general server/DB/network/polling/WS/viewer-gating/load optimizations. None included here.
