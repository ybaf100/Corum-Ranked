# Corum Ranked

Corum Ranked is a standalone Geode mode for the server-authoritative Ranked queue. It intentionally has no runtime or UI dependency on `hwanhee1.corum_integration`.

The client checks only currently active Geode user mods (enabled + loaded) against the server-provided allowlist before it creates a session and whenever it readies. Installed-but-disabled mods are ignored. Click Between Frames must be installed, active, and configured exactly as required by the current server configuration. Node IDs `v1.23.3` or newer is a required Geode dependency. Jukebox (`fleym.nongd`) is an optional allowed mod and is never required to enter Ranked.

When the environment is blocked, Corum Ranked opens its own Paperlogy-font in-game warning instead of exposing only raw IDs or error codes. It separates unauthorized active mods, missing or inactive required mods, incompatible versions, and required CBF setting changes; installed mod names are shown with IDs retained only as supporting detail.

The server owns matchmaking, map snapshots, bans, clocks, attempt acceptance, scoring, round outcomes, deathmatches, hidden MMR, and visible LP. The mod only reports local input-derived attempt events and renders the authoritative public state returned by the server; hidden MMR is never rendered.

During a standard Ranked round, the HUD keeps the current Round above the timer, plus both players' server scores and three approved Clear checks. A compact lower-right group shows the opponent's server-reported current progress, `Finished` after their authoritative Attempt End, and a wrench only while that opponent is in Practice. Progress and Practice-presence telemetry are ephemeral and never decide score, Clear validity, the timer, or the winner.

Death Match does not use Qualifying %. Each player receives exactly three authoritative Attempts and the winner is the player with the higher best raw progress percentage across those Attempts; a Clear is 100%. Equal best percentages repeat Death Match on the existing retry flow. The ordinary timer position shows only the authoritative remaining Attempt count from 3 through 0.

The client is locked to the production Ranked server at `https://corum-ranked.onrender.com`; it is not editable in mod settings. Production credentials and webhook secrets do not belong in the mod or repository.

Unranked players in `v0.5.0-beta.14` complete a server-authoritative solo Placement Run:
three or five Stages, two distinct Ranked Pool maps per Stage, three minutes and two Clears per Map,
and the existing Attempt score curve with the best two Attempts contributing to Map Score.
The bottom-left Qualifying HUD is replaced only during Placement by an image-first S/A/B/C/D
badge. A fixed upper-right Top 2 view combines completed authoritative Attempts with the active provisional Attempt without persisting provisional evidence. Provisional score and Grade can update during progress, while the persisted Map Grade is committed only at authoritative Attempt end. Missing Grade images safely fall back to text.

Rank visual lookup prefers a previously verified external asset cache and always falls back to the bundled package. The beta.14 client contains the versioned Google Drive ZIP/cache/SHA-256/extraction infrastructure, but production external downloading stays disabled until authoritative asset-pack version, Drive file ID, SHA-256, and byte-size metadata are supplied.

Ranked audio streams are prepared before competitive phase transitions where possible, and beta.14 emits low-cost transition timing logs for server-state receipt, audio handoff, image decode/node construction, and the first gameplay frame. These logs diagnose transition hitching without changing the authoritative 10-second Prepare/state-machine timing.

The `v0.5.0-beta.14` development build can include **DEBUG BOT MATCH** behind
`CORUM_RANKED_DEBUG_BOT_MATCH`, including Force Win and Force Lose. The CMake option defaults
to OFF, so official packages do not contain its UI. Placement-incomplete players cannot enter it.
Disable the matching server environment flag for production as well.
