# v0.4.0-alpha.31

- Fixed the two-Clear LAST ATTEMPT transition so the triggering player enters the opponent spectator screen immediately instead of seeing a misleading second `STARTS IN 10` prepare screen.
- The spectator screen now distinguishes an opponent that has not started yet (`WAITING TO START`) from a live attempt and shows the remaining LAST ATTEMPT start window.
- Fixed Debug Bot Match deadlock when a Bot attempt survives past the LAST ATTEMPT start deadline and the round enters `ROUND_SETTLING`.
- Debug Bot simulation now continues progress/end ticks for an already accepted Bot attempt during `ROUND_SETTLING`, while still forbidding new attempts after the deadline.
- Added recovery for an authoritative in-progress Bot attempt after a dev/Render server restart during `ROUND_SETTLING`.
- Added a regression test that verifies the Bot final attempt is closed and the match leaves `ROUND_SETTLING`.
- No scoring, MMR, map-pool, ban, FINAL/LAST ATTEMPT deadline, or normal PvP attempt semantics were changed.

# v0.4.0-alpha.30

- Fixed Ranked lobby/resource BGM downloads that could fail when Geometry Dash had not already loaded the configured Song ID metadata.
- Removed direct `MusicDownloadManager::downloadSong()` use from Ranked resource acquisition.
- Ranked now fetches Geometry Dash song metadata and downloads configured BGM sequentially with Geode `WebRequest` into a private Ranked audio cache.
- Existing Geometry Dash-downloaded songs remain preferred and are reused without duplicate downloads.
- Preserved `RANKED RESOURCES`, download progress, `RETRY FAILED`, `CONTINUE`, audio fades, and all existing gameplay/scoring/MMR behavior.

# v0.4.0-alpha.29

- Added Config-driven Ranked audio using Geometry Dash custom Song IDs with per-track start positions in seconds.
- Added `RANKED RESOURCES` first-entry/update gate. Missing configured resources are skipped if already present and can be fetched in one action with `DOWNLOAD ALL`.
- Added sequential resource download progress, 60-second per-resource timeout, `RETRY FAILED`, and `CONTINUE` after all resources are ready.
- Added Ranked BGM state mapping for Main/Queue, Match/Ban/Prepare/Spectator, and Win/Lose results with fallback to already configured tracks.
- Added configurable music fade-in/fade-out and UI fade-out/fade-in transitions. Same-track phase changes do not restart playback.
- Ranked audio fades out before vanilla LevelInfo/gameplay and Geometry Dash menu music is restored when leaving Ranked.
- Added server pass-through and validation for the client presentation config without changing gameplay rules or scoring.

# v0.4.0-alpha.28

- Removed the 30-second FINAL/LAST ATTEMPT start-intent expiry. A valid pre-deadline intent now holds the round while the player remains connected; a 15-minute hard TTL exists only as a deadlock guard.
- Preserved the bounded 5-second late packet reconciliation window; the actual 10-second gameplay start deadline is unchanged.
- Removed absolute `orphanAttemptSeconds` force-ending for active Round/Death Match attempts. Connected attempts now end only by natural death/Clear; disconnect policy handles abandoned clients.
- Repeated same-percent progress telemetry now refreshes the active-attempt lease heartbeat.
- Added a client safeguard that refuses to `onQuit()` a still-alive visual attempt merely because polling published an early result state.
- Prevented vanilla reset from creating a new visual attempt whenever the authoritative state no longer permits a new start.
- Replaced the full-screen `SYNCING RESULT` dead-end with immediate authoritative result rendering and terminal transport cleanup for already-finalized events.
- Serialized FINAL/LAST start-intent delivery with retry so multiple quick final-window starts cannot silently overwrite an in-flight intent request.
- Hardened the PostgreSQL pool against Neon idle connection drops so an emitted pool error no longer terminates the Render process; rollback failure can no longer mask the original DB error.

# v0.4.0-alpha.27

- Fixed FINAL/LAST ATTEMPT semantics so an attempt that visually starts before the 10-second start deadline is never force-ended when the window expires.
- Added an out-of-band start-intent signal for FINAL/LAST ATTEMPT. It does not create an attempt or extend gameplay time; it only prevents the authoritative server from finalizing while the serialized Start event is still catching up.
- Added server-side intent-aware recovery for a valid pre-deadline Start that reaches the authoritative FIFO after the deadline, then transitions the round to ROUND_SETTLING while the active attempt continues naturally.
- Added a Ranked spectator waiting screen after the local final attempt ends first, showing the opponent name and live progress until their active attempt finishes.
- Preserved the 10-second start deadline: new attempts after the deadline remain forbidden.
- Added rules/runtime regression coverage for late transport of a pre-deadline visual start.

# v0.4.0-alpha.27
- Prevented stale `/attempt/progress` responses from overwriting newer Attempt End/Clear snapshots; progress callbacks are now applied only while the same attempt is still active and has no queued End.
- Snapshotted the first visual Attempt start at LevelInfo -> PlayLayer arm time, so a legal scene transition can still journal its Start if server polling changes phase before PlayLayer finishes constructing. New scene entry is now forbidden in `ROUND_SETTLING`.
- Scoped optimistic Score/Clear presentation to the exact Round or Death Match sequence so pending events from the previous round cannot leak into the next round HUD.
- Blocked vanilla `resetLevel()` while the server is in `ROUND_SETTLING`, preventing a fake extra visual attempt after Final/LAST Attempt has already closed.
- Added a PLAYING-state LevelInfo recovery path so skipped `DEATHMATCH_PREPARE` / fast server transitions cannot leave the client permanently stuck on Loading Map.
- Ranked LevelInfo preparation now labels the actual phase: `ROUND 1`, `ROUND 2 - MATCH POINT`, `ROUND 3 - TIEBREAKER`, or `DEATH MATCH` instead of the generic `RANKED MATCH` text.
- Extended integration regression coverage to verify Round 2 starts with authoritative score/display score/clears all reset to zero.
- Preserves alpha.25 transport reconciliation, server-authoritative scoring, 70%+ x1.5 scoring, Clear=200, and all existing anti-cheat/rules behavior.

# v0.4.0-alpha.25
- Replaced the stretched `square02_001.png` Scale9 panel system with deterministic `CCLayerColor` panels. This removes the opaque black rectangles that could cover cards, LevelInfo UI, the vanilla progress bar, and gameplay percentage.
- Reduced the gameplay HUD footprint and moved the Ranked timer/state lower so Geometry Dash's native progress bar and percentage remain unobstructed.
- Shrunk the LevelInfo Ranked gate into a compact countdown/status strip while keeping the vanilla song acquisition controls as the only enabled inputs during preparation.
- Added client-observed `clientStartedAt` / `clientEndedAt` timestamps to Attempt transport using the synchronized server clock.
- Added a bounded 2-second server transport-reconciliation grace: gameplay deadlines are unchanged, but a Start packet observed before the deadline can still be accepted if network delivery lands immediately after it.
- Attempt Start/End FIFO entries are no longer erased when polling reaches `ROUND_RESULT`, `DEATHMATCH_RESULT`, `MATCH_RESULT`, or spectator state before their ACKs.
- PlayLayer now journals an outstanding visual attempt before automatic scene exit, closing another path where local progress/Clear could disappear before authoritative submission.
- Match dismissal / Queue Again waits for pending attempt transport instead of throwing away unsynchronized result events.
- Added server timing regression tests for bounded client timestamps and the transport-settling window.
