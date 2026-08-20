# Corum Ranked

Corum Ranked is a standalone Geode mode for the server-authoritative Ranked queue. It intentionally has no runtime or UI dependency on `hwanhee1.corum_integration`.

The client checks every installed Geode package, including disabled packages, against the server-provided allowlist before it creates a session and whenever it readies. Click Between Frames must be installed, active, and configured exactly as required by the current server configuration.

The server owns matchmaking, canonical/alternate/playable map snapshots, bans, clocks, attempt acceptance, scoring, round outcomes, deathmatches, and MMR. The mod loads only the snapshotted `playableLevelId`, includes it with every attempt event, and renders the authoritative state returned by the server.

During a Ranked round, the HUD keeps both players' server scores and approved Clear checks visible. The opponent's live attempt progress remains private except when the server places the two-Clear player into the LAST ATTEMPT waiting overlay. Progress telemetry is temporary and never decides score, Clear validity, or the winner.

Set **Ranked server URL** in this mod's settings. Production credentials and webhook secrets do not belong in the mod or repository.
