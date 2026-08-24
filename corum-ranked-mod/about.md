# Corum Ranked

Corum Ranked is a standalone Geode mode for the server-authoritative Ranked queue. It intentionally has no runtime or UI dependency on `hwanhee1.corum_integration`.

The client checks only currently active Geode user mods (enabled + loaded) against the server-provided allowlist before it creates a session and whenever it readies. Installed-but-disabled mods are ignored. Click Between Frames must be installed, active, and configured exactly as required by the current server configuration.

The server owns matchmaking, map snapshots, bans, clocks, attempt acceptance, scoring, round outcomes, deathmatches, hidden MMR, and visible LP. The mod only reports local input-derived attempt events and renders the authoritative public state returned by the server; hidden MMR is never rendered.

During a Ranked round, the HUD keeps the current Round above the timer, plus both players' server scores and three approved Clear checks. The opponent's live attempt progress remains private except when the server places the three-Clear player into the LAST ATTEMPT waiting overlay. Progress telemetry is temporary and never decides score, Clear validity, or the winner.

The production Ranked server defaults to `https://corum-ranked.onrender.com`. Production credentials and webhook secrets do not belong in the mod or repository.

The `v0.5.0-beta.4` development build can include **DEBUG BOT MATCH** behind
`CORUM_RANKED_DEBUG_BOT_MATCH`, including Force Win and Force Lose. The CMake option defaults
to OFF, so official packages do not contain its UI. Disable the matching server environment flag
for production as well.
