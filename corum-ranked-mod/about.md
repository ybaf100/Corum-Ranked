# Corum Ranked

Corum Ranked is a standalone Geode mode for the server-authoritative Ranked queue. It intentionally has no runtime or UI dependency on `hwanhee1.corum_integration`.

The client checks only currently active Geode user mods (enabled + loaded) against the server-provided allowlist before it creates a session and whenever it readies. Installed-but-disabled mods are ignored. Click Between Frames must be installed, active, and configured exactly as required by the current server configuration.

The server owns matchmaking, map snapshots, bans, clocks, attempt acceptance, scoring, round outcomes, deathmatches, and MMR. The mod only reports local input-derived attempt events and renders the authoritative state returned by the server.

During a Ranked round, the HUD keeps both players' server scores and approved Clear checks visible. The opponent's live attempt progress remains private except when the server places the two-Clear player into the LAST ATTEMPT waiting overlay. Progress telemetry is temporary and never decides score, Clear validity, or the winner.

The production Ranked server defaults to `https://corum-ranked.onrender.com`. Production credentials and webhook secrets do not belong in the mod or repository.

The `v0.4.0-alpha.11` development build can include **DEBUG BOT MATCH** behind
`CORUM_RANKED_DEBUG_BOT_MATCH`. Its results intentionally use the same live Ranked rating and
statistics path during alpha testing. Build with that CMake option OFF and disable the matching
server environment flag before a production release.
