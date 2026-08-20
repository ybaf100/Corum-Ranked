# Changelog

## v0.4.0-alpha.3

- Changed Debug Bot Match results to use the normal Ranked MMR, score, tier, placement, statistics, leaderboard, and history path.
- Added configurable difficulty rating offsets around the current player MMR while retaining the shared ELO calculator.
- Split map identity into canonical, alternate, and playable IDs, with mandatory alternate-first resolution and snapshot validation.
- Added playable level IDs to every client attempt event and server-side Round/Deathmatch rejection for mismatched levels.

## v0.4.0-alpha.2

- Added the compile-time gated development-only Debug Bot Match entry and setup UI.
- Added server-generated Bot attempts, progress telemetry, private bans, and deterministic rule scenarios through the normal authoritative match engine.
- Isolated Debug Bot results from MMR, placement, public statistics, public history, queue state, and Discord relay by default.
- Added release-removal switches and automated debug flow regression coverage.

## v0.4.0-alpha.1

- Published the first explicitly versioned alpha source snapshot.
- Included the server-authoritative HUD and progress-only LAST ATTEMPT spectator overlay.
- Kept the legacy Corum Integration mod isolated and unchanged by Ranked runtime code.

## v0.1.0

- Added the isolated Corum Ranked menu and runtime.
- Added fail-closed allowlist and Click Between Frames checks.
- Added server sessions, queue polling, private bans, ready flow, map launch, attempt reporting, BO3 banners, LAST ATTEMPT, and deathmatch display.
- Added server-clock deadline rendering and host-side policy tests.
- Added corner-anchored gameplay HUD with measured frame cadence, authoritative Score, two approved-Clear checks, Qualifying snapshot, MATCH POINT, TIEBREAKER, FINAL ATTEMPT, and LAST ATTEMPT displays.
- Added rate-limited current-attempt telemetry and a server-authorized progress-only spectator overlay for the 2-Clear versus 1-Clear LAST ATTEMPT case.
