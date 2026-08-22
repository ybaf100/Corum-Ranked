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
