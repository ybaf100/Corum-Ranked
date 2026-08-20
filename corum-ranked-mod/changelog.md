# Changelog

## v0.4.0-alpha.4

- Added the compile-time-gated Debug Bot Match password/config UI and server-driven opponent simulator.
- Debug Bot matches use the ordinary Ranked attempt, round, Bo3, deathmatch, rating, placement, statistics, and history paths.
- Added Easy/Normal/Hard bot ratings near the player's MMR plus deterministic debug scenarios, including LAST ATTEMPT spectator testing.
- Added canonical/alternate/playable map snapshots and authoritative attempt Level ID validation.
- Moved Ranked Pool and Qualifying input to two columns on the existing Corum map sheet; no separate Ranked Pool tab is used.
- Added Koyeb + Neon free-alpha deployment documentation.

## v0.1.0

- Added the isolated Corum Ranked menu and runtime.
- Added fail-closed allowlist and Click Between Frames checks.
- Added server sessions, queue polling, private bans, ready flow, map launch, attempt reporting, BO3 banners, LAST ATTEMPT, and deathmatch display.
- Added server-clock deadline rendering and host-side policy tests.
- Added corner-anchored gameplay HUD with measured frame cadence, authoritative Score, two approved-Clear checks, Qualifying snapshot, MATCH POINT, TIEBREAKER, FINAL ATTEMPT, and LAST ATTEMPT displays.
- Added rate-limited current-attempt telemetry and a server-authorized progress-only spectator overlay for the 2-Clear versus 1-Clear LAST ATTEMPT case.
