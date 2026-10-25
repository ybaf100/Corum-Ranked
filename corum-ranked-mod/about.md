# Corum Ranked

Corum Ranked is a standalone Geode mode for the server-authoritative Ranked queue. It intentionally has no runtime or UI dependency on `hwanhee1.corum_integration`.

The client checks only currently active Geode user mods (enabled + loaded) against the server-provided allowlist before it creates a session and whenever it readies. Installed-but-disabled mods are ignored. Click Between Frames must be installed, active, and configured exactly as required by the current server configuration. Node IDs `v1.23.3` or newer is a required Geode dependency. Jukebox (`fleym.nongd`) is an optional allowed mod and is never required to enter Ranked.

When the environment is blocked, Corum Ranked opens its own Paperlogy-font in-game warning instead of exposing only raw IDs or error codes. It separates unauthorized active mods, missing or inactive required mods, incompatible versions, and required CBF setting changes; installed mod names are shown with IDs retained only as supporting detail.

The server owns matchmaking, map snapshots, bans, clocks, attempt acceptance, scoring, round outcomes, deathmatches, hidden MMR, and visible LP. The mod only reports local input-derived attempt events and renders the authoritative public state returned by the server; hidden MMR is never rendered.

During a standard Ranked round, the HUD keeps the current Round above the timer, plus both players' server scores and three approved Clear checks. A compact lower-right group shows the opponent's server-reported current progress, `Finished` only after that side has no active or legally startable Attempt remaining, and a wrench only while that opponent is in Practice. A pending final-window Start lease also suppresses `Finished` while its authoritative journal is in transit. Progress and Practice-presence telemetry are ephemeral and never decide score, Clear validity, the timer, or the winner.

Death Match does not use Qualifying %. Each player receives exactly three authoritative Attempts and the winner is the player with the higher best raw progress percentage across those Attempts; a Clear is 100%. Equal best percentages repeat Death Match on the existing retry flow. The ordinary timer position shows only the authoritative remaining Attempt count from 3 through 0. `Finished` stays hidden after Attempts one and two and appears only after the third Attempt is complete; if that End immediately resolves the match, the terminal result preserves the final viewer-relative lifecycle projection.

The client connects only to Singapore Lightsail at `https://api.52-74-37-130.sslip.io`. Railway and Render hosting, update bridges and fallbacks are retired. Older clients using those hosts require manual installation of the Lightsail package. Production credentials and webhook secrets do not belong in the mod or repository.
