# v0.5.0-beta.32 — Spider transport and match controls

Starts from remote main `3d24065e25c286cd4595239ab345a14541a4385f` (beta31).

## Spider and polling diagnosis

Beta30 already retained the interpolation deque across snaps. Remaining source
issues were separate: the pre-snap interval held the entire previous pose/camera,
and production sampled only after an HTTP ACK plus another 50ms wait. At 110ms
RTT that yielded about 160ms per sample. Debug TEST uses DB/server playback and
does not prove this production cadence. A late join could also deliver the
server's 72-sample/3s relay to a 64-sample/2s client, while an expired subscriber
cursor could delete the publisher's valid stream and cause POLLING fallback.

The publisher now captures at an independent 50ms cadence and immediately on
verified native snaps. One serialized request sends an immutable ordered batch
(max 16), with bounded capture queue, exact retry and terminal drain. No parallel
POSTs or WebSocket are introduced. With native evidence, the viewer interpolates
to the pre-click source endpoint/camera and then applies the entire destination
atomically. Snap boundaries preserve samples, generation and camera ownership.
Late join/reset starts near the live edge (350ms/16 samples); missed subscriber
cursors recover without deleting the stream. Real loss retains safe fallback.
Existing adaptive viewer polling remains 40/20/5ms, not a correctness workaround.

## Match controls and server setting

- Native Ranked pause adds SURRENDER → ROUND/MATCH → confirmation. Existing earned
  scores remain; unfinished attempts are invalidated. Context-bound persisted
  event IDs and state-version checks prevent duplicate/stale concessions.
- Each player has a cumulative 30 seconds of pause for the whole match, including
  Death Match. The counter resumes at zero; round deadlines keep wall time.
  Persisted leases prevent retries or scene/round changes from refilling it.
- The live spectator setting is server-global and persistent. The Debug Spectate
  button shows ON/OFF after ACK. OFF routes Finished players to the existing
  opponent percentage/score wait UI confirmed in the Notion UI/UX specification.
- Authorized pause holds remote presentation without stream timeout, starvation
  or a wall-time hole in the publisher timeline. Resume keeps the same stream.

See [gameplay controls](../ranked/docs/gameplay-controls.md) for endpoints,
authorization, migration and the per-player pause-budget semantics.

## Preserved behavior and validation

No PlayerObject/GJBaseGameLayer detours are added; beta31's global setVisible hook
remains removed. One PauseLayer::customSetup hook adds the Ranked UI; normal
Resume is observed at the existing frame boundary. No opponent physics is
re-enabled. Safe Mode, native world/camera ordering and Follow-Y sampling remain.
Placement persistence, timer-zero final Clear, stale Attempt protection and
local result authority boundaries remain. Placement and ordinary Practice do not
use these match controls. Packet protocol 2 and Debug recording format 2 remain;
the publish batch wrapper/source camera/paused state are additive, exact-gated
to beta32. No unrelated web/integration source is modified.

Executable regressions cover 110ms ACK-bound transport with 50ms captures,
urgent Spider evidence during in-flight requests, immutable retries/terminal
drain, snap-source motion, 26 repeated snaps at 60/120/144/240Hz under the old
190ms response cadence, 12s pause/resume, persistent per-player 30s across rounds,
stale leases/concessions, real DB surrender/MMR, global setting and Debug paths,
Placement lifecycle, camera/world driver, safety and packaged hook inventory.
CI builds Windows, iOS Debug ON/OFF and both Android architectures.

Automated tests/native compilation do not establish iPadOS device verification.
Device follow-up must cover repeated Spider clicks/pauses under the same network,
ROUND/MATCH surrender, 30-second exhaustion across rounds, OFF Finished wait UI,
and ordinary Practice with the beta31 visibility regression preserved. Dual/P2,
complete native trigger/world parity, complex camera/cosmetics and persistent
push transport remain Phase 5/6 work.
