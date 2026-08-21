# v0.4.0-alpha.14
- Moved the main-menu Ranked shortcut from the crowded bottom action row to the upper-right corner.
- Rebuilt Ranked song preparation around Geometry Dash's own `CustomSongWidget` download action, with MusicDownloadManager retained only as a fallback.
- Added defensive LevelInfoLayer song-download re-kicks and explicit 20-second no-song bypass through the post-warning play step.
- Locked LevelInfoLayer Back/key-back during an active Ranked match and made Ranked return-to-scene idempotent to prevent scene-stack escape from repeated quit/back input.
- Enlarged player icons, moved Match Found heading to the top, widened the five-map ban row, and allowed two-line map titles instead of aggressive truncation.
- Kept LAST ATTEMPT settling, provisional live scoring, Debug Bot nerfs, active-only mod allowlisting, and server rules unchanged.
- Corum Integration remains unchanged.

# v0.4.0-alpha.13
- Fixed LAST ATTEMPT so attempts accepted within the 10-second start window continue to completion after the timer expires.
- Kept spectator mode active through ROUND_SETTLING until the accepted LAST ATTEMPT ends.
- Added provisional live HUD scoring after Qualifying while preserving end-of-attempt authoritative scoring.
- Nerfed Debug Bot default attempt frequency, clear chance, qualifying chance, and progress speed.

# Changelog

## v0.4.0-alpha.13

- Moved Corum Ranked into its own Geometry Dash-style scene/tab instead of attaching the Ranked fullscreen layer directly to whatever scene is currently open.
- Replaced the old small `R` main-menu entry with a bottom-center crossed-swords Ranked button. The swords are drawn from Cocos nodes, so no external texture asset is required.
- Fixed repeated Ranked `Load Failed` launches by distinguishing saved list metadata from a fully downloaded playable level. Ranked now requires a non-empty `m_levelString` before a map is considered downloaded.
- Changed automatic round entry to Geometry Dash's normal `LevelInfoLayer -> onPlay` path instead of directly constructing `PlayLayer::scene`; quitting returns through the game's normal scene stack.
- Reworked song preparation to use every custom song ID in `m_songID` + the 2.2 comma-separated `m_songIDs` field.
- Song download now requests missing `SongInfoObject` metadata first, retries stalled metadata/download requests on a cooldown, and polls `MusicDownloadManager` until all required custom songs are available.
- Kept the 20-second song ceiling: a round may start without unfinished audio while an in-flight Geometry Dash song download is left running for later attempts.
- Kept the 30-second map ceiling, Round 1 cancellation policy, later-round forfeit policy, active-only mod allowlist, alpha.10 UI flow, and alpha.11 authoritative LAST ATTEMPT behavior.
- Corum Integration remains unchanged.

## v0.4.0-alpha.11

- Updated the server integration harness for the alpha.10 2-Clear rule: a player at 0 Clears now receives a real 10-second LAST ATTEMPT start window, so rated Debug Bot tests wait for that authoritative window to expire before readying the next round.
- Updated the two-client relay regression expectation from one to two `LAST_ATTEMPT` events because both tested rounds now legitimately enter the new LAST ATTEMPT path.
- Production 2-Clear, rating, map/song download, active-mod allowlist, and match flow behavior are unchanged; this release fixes CI expectations that still encoded the pre-alpha.10 rule.
- Corum Integration remains unchanged.

## v0.4.0-alpha.10

- Replaced the Ranked popup-style flow with a full-screen in-game Ranked layer based on the supplied UI sketches. Orange annotations/arrows from the sketches are implementation notes only and are never rendered in-game.
- Added Match Found, private 10-second map ban, round/deathmatch preparation, Match End, Queue Again, and Match History/detail screens with round-by-round scores and approved Clear checks.
- Added explicit map/song resource states: `DOWNLOAD MAP` / `DOWNLOAD SONG`, `DOWNLOADING...`, and `DOWNLOADED`. Missing resources auto-start downloading when the 10-second start countdown reaches 5 seconds remaining.
- Added map download handling with a 30-second maximum: Round 1 timeout cancels the match with no rating/stat/history result; Round 2+, Round 3, and Deathmatch map timeout forfeits the match for the failing player.
- Added song download handling with a 20-second maximum. If the song is still unavailable after the window, play starts without waiting; the Geometry Dash download continues in the background and can become available on a later attempt.
- Added `WAITING FOR YOUR DOWNLOAD...` / `WAITING FOR <PLAYER>'S DOWNLOAD...` readiness messaging while either side is not map-ready.
- Changed the 2-Clear rule so a player at 0 Clears also receives the same 10-second LAST ATTEMPT start window. A first Clear does not end the window; any next attempt started before the deadline remains valid through completion, and reaching 2 Clears produces a Draw.
- Added automatic exit through Geometry Dash's normal `PlayLayer::onQuit()` path when the authoritative server state says the active round/match is no longer playable.
- Added server match-history summaries and resource-failure handling required by the new result/history UI.
- Kept active-only mod allowlist enforcement, CSMP timeout/retry handling, the embedded Render server URL, and Debug Bot rating/stat behavior from prior alphas.
- Corum Integration remains unchanged.

## v0.4.0-alpha.9

- Split CSMP source timeout control from Ranked config fetch timeout with `RANKED_CSMP_FETCH_TIMEOUT_MS` (30 seconds by default).
- Apps Script `csmp` / `player_records` requests retry once on timeout and return HTTP 503 with a stable `CSMP_SOURCE_TIMEOUT` code instead of surfacing a generic 500.
- Cached the shared `csmp` definition for the Ranked config refresh interval and de-duplicated concurrent CSMP definition fetches; per-player records remain fresh per initial seed lookup.
- Added action-specific timeout/error logging without logging player IDs or secrets.
- Extended the initial Geode session request timeout to 70 seconds so the server can complete one CSMP retry during Apps Script cold starts.
- Added regression tests for CSMP caching, timeout retry, 503 mapping, and the independent environment setting.
- Corum Integration remains unchanged.

## v0.4.0-alpha.8

- Fixed the Ranked server session test isolation bug that caused deterministic test UUIDs to collide across test cases in the shared PGlite database.
- `session.service.test.ts` now truncates session/profile/player tables before each test, so the active-only allowlist regression test does not fail on a stale `ranked_players` primary key.
- Production UUID generation remains unchanged and continues to use `node:crypto` `randomUUID()`; this release only fixes the CI test harness.
- Corum Integration remains unchanged.

## v0.4.0-alpha.7

- Changed the Ranked allowlist gate to inspect only mods that are currently enabled and loaded. Installed-but-disabled user mods no longer block Ranked.
- The client now omits inactive user mods from the environment payload; inactive CBF is retained only to report the mandatory dependency as not active.
- The shared server-side anti-cheat independently filters to active mods, so legacy clients that still send disabled mods cannot make those disabled mods trigger the allowlist.
- Required allowlisted mods must still be active, and CBF remains mandatory with its required settings enforced.
- Updated client/server/rules tests for active-only mod enforcement and bumped Corum Ranked to v0.4.0-alpha.7. Corum Integration remains unchanged.

## v0.4.0-alpha.6

- Embedded `https://corum-ranked.onrender.com` as the production Ranked server URL and added a runtime fallback when the saved Geode string setting is empty.
- Updated stale Debug Bot test naming from `DebugBotMatchService` / `debugBotMatch` to the canonical `DebugBotService` / `debugBot` API used by the server.
- Added a compatibility smoke test at the legacy `debug-bot-flow.integration.test.ts` path so old working trees are overwritten instead of leaving a type-check-breaking stale test behind.
- Updated Render + Neon deployment guidance; production Render builds install dev type packages with `npm ci --include=dev && npm run build`.
- Bumped the standalone Corum Ranked client/server/rules workspace to v0.4.0-alpha.6. Corum Integration runtime code is unchanged.

## v0.4.0-alpha.5

- Fixed Debug Bot Match client/runtime API compatibility so both the current single-options call and the alpha.4 two-argument call compile.
- Excluded the stale alpha.4 `src/debug/DebugBotPopup.cpp` overlay path from CMake to prevent duplicate/legacy Debug Bot compilation.
- The compatibility fix is platform-independent and applies to Windows, iOS, Android32, and Android64 builds.
- Kept the server-side debug password validation and did not bypass the debug gate to fix compilation.
- Bumped the standalone Corum Ranked client/server workspace version to v0.4.0-alpha.5.

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
