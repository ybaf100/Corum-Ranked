## v0.5.0-beta.49

- Hold the completed promotion/demotion reveal until Continue. Explicit Skip/Esc still goes directly to the score overlay; Placement keeps its established reveal flow.
- Retire score/reveal input after native dispatch returns, retaining each callback target until touch cancellation, removal and parent restoration finish. Ignore repeated close events and avoid rebuilding a retired Ranked scene.
- Restore full-size lobby badges with standings immediately below the streak. Lower result headings, retain tier halos, align LP itself to the moving fill endpoint, and use rounded bars with continuous eased interpolation over the existing 2/4-second durations.
- Remove the blur shader's duplicate native Cocos matrix declaration. Synchronize the beta49 exact client/server gate and workspaces; image assets and their pack are unchanged.
- Clarify score-track color as the immediately lower displayed tier (SILVER uses BRONZE), following promotion/demotion crossings. Downsample before adjacent-texel bilinear blur passes to remove offset copies of result text/edges. Record the requirement audit and local regression evidence; keep the beta49 gate and image pack unchanged.

## v0.5.0-beta.48

- Add the hand-drawn result score presentation over a captured, blurred Match Result: exact LP interpolation over 2 seconds, or 4 seconds for promotion/demotion; cutoff labels, tier-colored bars, paired icon changes, promotion marks and delayed entry/warning notices. Keep Placement's existing flow.
- Chain completed/skipped promotion and demotion reveals into the score overlay. Separate keypad dispatches and restore the settled result after Skip/Esc; replay each match only once.
- Show LP competition standings and Top % above every lobby badge, plus LP / rank for GOLD. Count all historical bot profiles and exclude unplaced profiles. GOLD targets the nearest higher score, selects the highest-MMR tied player's icon, and fixes equal-MMR random choices in the stored result without publishing MMR.
- Restrict Settings to the lobby, Achievements and History. Move opponent INFO to the card's upper right. Keep Jukebox visible in the round-options row; an inactive mod gets a clickable gray button and requirement notice.
- Synchronize client/server exact gate and workspace versions to beta48. Reuse existing image assets; no image pack update is required. Local executable/GPU checks are distinct from native platform and device verification.

## v0.5.0-beta.43

- Reset the Debug Bot's round/deathmatch Attempt state when the authoritative context changes after surrender. Verify R1 round surrender → R2 fresh Attempt and positive progress against the actual server and database.
- Keep result-screen ESC in Ranked and return to its lobby. Open installed Jukebox directly, show an upper-right preparation countdown while its dialog is open, and close the owned dialog before the native gameplay modal gate.
- Show claimable achievements first using stable original card indices; use the existing red Claim button style. Start every exact round's LDM choice at OFF instead of inheriting a saved GD object's prior selection.
- Enforce one live Ranked session per account at lobby admission. Maintain an independent 5-second heartbeat, release on lobby Close, and expire lost connections after 20 seconds. Serialize admission/heartbeat/Close and retire stale tokens and callbacks.
- Synchronize client/server exact gate and workspace versions to beta43. Restore the standard GitHub native build/package workflow after the user's build preference changed; retain automatic tests and the optional [WINDOWS-BUILD.md](WINDOWS-BUILD.md) PowerShell script. Assets are unchanged. See [BETA43-LOBBY-LIFECYCLE.md](BETA43-LOBBY-LIFECYCLE.md) for evidence and validation limits.

## v0.5.0-beta.42

- Accept GD Music Library IDs in the existing Ranked audio spreadsheet keys. Library songs use GD's native custom-content downloader; Newgrounds songs retain the existing metadata/download path. Preserve configured start offsets, loops, fades, volume and gameplay/Jukebox ownership.
- Share existing native song requests, reject unfinished/error files, import completed audio into the persistent Ranked cache, and bound failures with the existing resource retry UI. Configuration changes retire local tracking without cancelling another caller's native request.
- Add executed download/cache/cancellation tests for Debug ON/OFF and spreadsheet Library ID regression coverage. Synchronize the manifest, exact server gate, workspace versions and lockfile to beta42. See [MUSIC-LIBRARY-AUDIO.md](MUSIC-LIBRARY-AUDIO.md) for configuration and verification limits. Assets are unchanged; no manual Release.

## v0.5.0-beta.30

- Replace the global by-value-string saveNewScore hook with exact-scene storeNewLocalScore(int,int), preserving spectator percentage/completion/death/transport safety and real local/Ranked/Placement saves. Keep the accepted final Placement Attempt alive in MAP_SETTLING, and reject state snapshots that resurrect End-ACKed UUIDs or roll back stateVersion.
- Retain the entire timestamp deque across Spider/teleport snaps. Validated client presentation segments hold before the destination, switch position/rotation/camera/discrete state atomically, then resume interpolation. Carry the segment to the hidden world driver to reset only local crossing/Follow Player Y history, including skipped render frames.
- Add bounded Debug TEST native collision/touch/activation/Spawn/commands/follow/dynamic/trigger probes. Preserve the native world pass and blocked PlayerObject physics. No unverified extra collision pass: actual missing native interaction and background/touch parity still need GD 2.2081 device traces.
- Adapt serialized viewer idle gaps to 40/20/5ms for sufficient/low/starving buffers with bounded error backoff. Preserve one in-flight request. Explicitly regress and document production's unchanged ACK-bound RTT + 50ms publishing; Debug RECORD's independent 50ms capture does not establish production cadence.
- Keep spectator protocol v2 and Debug recording format v2. Device verification remains outstanding; see BETA30-RELEASE.md for evidence and limits. No manual Release.

## v0.5.0-beta.29

- Feed the native world a hidden no-authority m_player1 from the same timestamp-buffered frame as the dedicated visible Remote Player and camera, before GJBaseGameLayer world processing. Keep local physics, completion, scoring and persistence blocked; allow native position/rotation writes only in the authenticated driver scope.
- Maintain continuous previous/current coordinates and native Follow Player Y history on publisher time. Apply mode/gravity before verified Spider/teleport destinations, reset snap history once, and reconcile native portal writes without resimulating remote physics. Reset native world state only for server stream/context changes.
- Preserve beta28 final viewport correction without forcing camera flags or cancelling world-trigger camera tweens each frame. Keep its interpolation limits, measured headroom, progress presentation and audio ownership unchanged.
- Capture a stationary, started native world seed immediately instead of requiring two moving frames. Anchor Debug recording time to the first accepted seed; keep server normalization, HTTP/PostgreSQL RECORD/TEST and v2 protocol/format.
- Execute production world/camera/player hook bodies and the complete driver/renderer/camera in Debug ON/OFF native regressions, including Spider/follow/background ordering, scoped native writes and 60/120/240Hz progression. Existing safety and server suites remain intact.
- Synchronize manifest, workspace and exact server gate to beta29. Actual iPadOS trigger/background/Spider parity and native late-join history catch-up still require device verification; the server's recent 3s buffer is not full Attempt history. No manual Release.

## v0.5.0-beta.28

- Give spectator viewport one final owner: native camera once, then recorded-camera correction of actual render transforms. Remove camera work from viewer polling, guard duplicate lifecycle calls and retain a camera-only scoped native remote-target fallback with exact dummy restoration.
- Render every accepted packet on a bounded publisher timestamp buffer shared by Remote Player and camera. Adapt warm-up headroom to HTTP arrival cadence; preserve 1x gameplay, shortest rotation, discrete boundaries, verified snaps and exact terminal hold. Keep HTTP rates/server architecture unchanged.
- Override spectator progress sprite/label only; preserve hidden/frozen dummy, normal Ranked/Placement/Practice, New Best, persistence and score authority. Add bounded native transform diagnostics and aggregate Debug buffer/RTT metrics.
- Execute actual camera hook/renderer/adapter against exact beta27 iPadOS sequence18–22; test batching, starvation, bounds and 60/120/144/240Hz. Preserve existing HTTP/PostgreSQL Debug and audio/safety regressions. Actual post-fix iPadOS visual QA is still required.
- Synchronize beta28 manifest, exact client/server gate and npm workspace/lock versions. No new source copied from Globed/AttemptPlaybackMod, no manual Release or Web changes. See BETA28-RELEASE.md for measured-data limits and device checklist.

## v0.5.0-beta.27

- Hand Ranked-owned lobby/match audio to gameplay only after successful native PlayLayer init, including Debug RECORD and TEST. Retained Ranked returns reselect the current authoritative lobby mode; GD level music and full-menu restore semantics are preserved.
- Normalize server playback elapsed times against the first persisted valid frame, preserving relative movement/terminal timing and format-v2 DB compatibility. The first in-scene HTTP reply can emit t=0 without the recording's leading blank time.
- Observe native player/rotation frames and sample the completed world/camera update at the existing 50ms cadence. Debug visual capture waits for stable moving native gameplay independently from the Ranked journal's unchanged settling window.
- Relay bounded native TELEPORT/SPIDER_SNAP endpoint evidence and optional actual native camera/visibility state. Verified snaps clear interpolation without changing Remote Player identity; unverified large corrections still reject. Missing/invalid camera uses the existing pose-follow fallback; terminal pose/camera holds exactly.
- Avoid pre-packet Remote Player allocation and retain the object across transient waiting/fallback. Guard native spectator updateAttempts and keep existing dummy/persistence/Ranked isolation. Debug-only forensic logging is bounded to sequences 18–22.
- Add executed native audio/capture regressions and actual HTTP -> PostgreSQL -> server clock -> production C++ decoder/Remote Player/camera tests for death and Clear. The supplied beta26 log proves the repeated 729-unit rejection, but does not identify its native cause or contain raw poses; no teleport evidence/camera is invented for old recordings. Re-record on beta27 for complete capture and diagnosis.
- Synchronize the exact client/server gate to beta27. Keep protocol/Debug format v2 and Phase 1–4; full Dual/cosmetics/jitter/reconnect/camera zoom/platformer and network optimizations remain later phases. No manual Release.

## v0.5.0-beta.26

- Fix Debug VOICE RECORD/TEST staying in LevelInfo after map/song readiness: activate the verified native GD Play item with its original listener/selector instead of directly calling onPlay with a null sender.
- Use a separate one-shot Debug callback latch, consumed before vanilla forwarding. Manual Play and duplicate dispatch remain blocked; Ranked authority is untouched.
- Wait for the native item/callback and busy state. Retry a failed activation after four seconds without cancelling its server scope; LevelInfo exit or PlayLayer entry stops the watchdog. Explicit Back still finalizes an incomplete recording.
- Execute the production native-item identity, selector dispatch, hook forwarding, PlayLayer Debug entry and runtime mode transitions in Debug ON/OFF regression tests, including missing/busy buttons, dropped/duplicate callbacks and RECORD/TEST.
- Synchronize the exact client/server gate to beta26. Preserve verified online/Saved geometry, effective song files, server recording/playback, protocol/format v2 and Phase 1–4 spectator isolation. iPadOS retest remains required; no manual Release.

## v0.5.0-beta.25

- Live Spectator protocol v2 reads actual GD Cube/Ship/Ball/UFO/Wave/Robot/Spider/Swing, mini, gravity and visual flip, using shared production/Debug capture and strict server/client validation.
- Dedicated Remote Player reuses native vehicle/size helpers only on transitions. Death fades without local gameplay calls; new Attempt respawns; Clear holds its final pose and HUD status.
- Terminal poses bypass the sampling interval, preserve Start/live ACK ordering and survive the bounded authoritative End/HTTP race. Existing dummy/no-score/no-New-Best guards remain.
- Debug VOICE RECORD/TEST now persists and replays format v2 through the existing server clock, HTTP relay, decoder, interpolation, remote and camera. Old v1 recordings are excluded.
- Bounded Spider gravity snaps avoid invalid interpolation. Dual explicitly falls back pending Phase 5; cosmetics/advanced interpolation/complex camera parity and optimizations remain later work.

## v0.5.0-beta.24

- Keep fresh online VOICE attestation, then use the matching native downloaded Saved pointer for Debug Spectate launch instead of a geometry-empty SearchResult. Reject every identity mismatch; preserve vanilla download for absent geometry and normal Ranked entry behavior.
- Wait for the effective MusicDownloadManager/Jukebox song file and native busy/modal readiness, skip existing-file acquisition, and automatically invoke vanilla Play. Add transition-only metadata/map/song/PlayLayer/recording diagnostics and cancellation reasons.
- Separate popup cancellation from preparation ownership transferred to LevelInfo. Preserve explicit popup Cancel/LevelInfo Back incomplete finalization; exercise production native proof, download/replacement, song gate and runtime handoff regressions.
- Synchronize mod/server exact gate to beta24; preserve server-backed recording, official Debug OFF and Phase 1–3 safety. iPadOS retest is still required; no manual Release or workflow rerun.

## v0.5.0-beta.23

- Reset expired-session deadline and upload backoff before a fresh Debug Spectate HTTP admission, so an old scope cannot immediately cancel a new RECORD/TEST request.
- Reproduce the lifecycle failure using the compiled production client runtime and verify fresh admission/record cancellation succeeds after expired-scope fault injection.
- Advance the exact mod/server gate and reject beta22. Preserve beta22 server-backed storage/clock, production presentation and all dummy guards. No manual Release or workflow rerun.

## v0.5.0-beta.22

- Add development-only RECORD SPECTATE / TEST SPECTATE for fixed VOICE (88350599), using verified native GD preparation/download and normal vanilla recording gameplay.
- Record the production 50 ms pose cadence through HTTP, preserve death/Clear terminal frames and persist a bounded recording in an isolated PostgreSQL debug domain with one bulk finalize transaction. Retry/finalize are idempotent; incomplete data is not TEST eligible.
- Server-owned playback clock starts with the in-scene HTTP viewer WAITING handshake, feeds the production SpectatorLiveService buffer/response, and uses the same client decoder, bounded interpolation, dedicated Remote Player and native remote camera. No local replay clock/frame file/injection and no synthetic Ranked Match.
- Keep Phase 1 dummy noclip/99%/completion/Attempt/Practice/New Best/save guards and Phase 2 separate PlayerObject. Debug data never awards rating, LP/MMR, statistics, achievements/AP/titles or Placement results.
- Opt-in server flag/password/scoped expiring tokens; Debug CI flag ON, official client flag/UI/API code OFF. Add real HTTP + disk-persistent PostgreSQL restart tests, strict recorder/native runtime tests and Debug ON/OFF native scene/decoder/safety regressions.
- Exact mod/server gate advances to beta22. Native-device RECORD/TEST E2E and two-client gameplay E2E still require device QA. No manual Release or failed-workflow rerun.

## v0.5.0-beta.21

- Correct the Phase-3 watchability helper's const qualifier: Geode Modify's m_fields accessor is non-const. This fixes beta20 native compile failure, not an infrastructure/quota failure.
- Model that exact accessor constraint in the production scene-body harness and reproduce the invalid const access in a negative compile check. Same-scene entry, dedicated remote camera and all Phase 1/2 safeguards are otherwise unchanged.
- Synchronize the exact client/server gate to beta21 and reject beta20. No failed workflow rerun or manual Release.

## v0.5.0-beta.20

- Spectator Phase 3 adds same-PlayLayer authority-gated handoff and a presentation heartbeat that survives stopped vanilla completion updates. A live local Attempt, Placement or user Practice cannot enter spectator camera mode.
- Follow the dedicated Remote Player with native forced X/Y camera targets, without moving/swapping local `m_player1`. The hidden local dummy freezes immediately after conversion; all Phase 1 noclip, 99%/completion, evidence and exact-level record guards remain independent.
- Hold the real viewport while waiting/timeout, show compact WAITING/POLLING status, resume only on validated fresh packets, and release camera/remote/live state on authority loss, result, context change or exit. Same-generation timeout recovery retains all existing stream/sequence validators.
- Add pure camera/context policy tests, actual scene/decoder callback harnesses, and a two-client mock compiling complete production renderer + camera; preserve Phase 1/2 regressions. Device two-client E2E and complex trigger/platformer parity remain unverified.
- Synchronize the exact mod/server gate to beta20 and reject beta19. No scoring/DB/protocol redesign, manual Release or workflow rerun.

## v0.5.0-beta.19

- Fix native GD CCPoint/CCSize overloaded assignment ambiguity in remote pose caches using explicit CCPoint values. Extend the complete production-renderer harness with the native-style overload.
- Synchronize the exact client/server version gate to beta19 and reject beta18. This is a corrective code patch after beta18 native CI compile failure, not an infrastructure failure or workflow rerun.

## v0.5.0-beta.18

- Spectator Phase 2 adds a dedicated render-only Cube PlayerObject, separate from the Phase 1 local dummy, using existing x/y/rotation/dead/finished telemetry and interpolation.
- Retained world-layer visual/companion ownership, context/generation/sequence/reset-epoch validation, hidden waiting/dead state and frozen finished pose prevent local gameplay lifecycle side effects.
- Preserve all Phase 1 noclip, 99%/completion, transport and local-record guards independently of remote rendering. No server protocol, scoring, DB, camera or Practice changes.
- Add strict remote lifecycle policy tests and a native-double harness compiling the complete production renderer; package MIT attribution for the adapted Globed companion cleanup inventory.
- Synchronize client/server gate to beta18. Camera follow and spectator entry E2E remain Phase 3. Release remains manual-only.

## v0.5.0-beta.17

- Spectator Phase 1 converts only server-authorized, finished local gameplay into a hidden non-scoring local dummy; live FINAL/LAST/Death Match Attempts, Placement and user Practice are excluded.
- Noclip suppresses death only for the exact local dummy. Its physics freezes at 99%, with independent vanilla end-check/animation/completion and Ranked evidence guards; the PlayLayer stays alive.
- Explicitly blocks dummy New Best, exact-level `savePercentage` and `saveNewScore`, including quit/reset calls, without relying on Practice flags or disabling ordinary Ranked records.
- Blocks new Start/Progress/End/Clear/presence transport throughout pending-to-spectator handoff while preserving already-observed FIFO delivery.
- Removes temporary spectator Practice frame semantics and local-player remote pose reuse. Opponent polling remains; separate RemotePlayer rendering/camera/mode/cosmetics/interpolation are Phase 2 only.
- Adds strict lifecycle-policy/source regression coverage and synchronizes the client/server gate to beta17. Each future patch, including hotfixes, advances the beta version; Release remains manual-only.

## v0.5.0-beta.16

- Added Achievement System v1 with ten five-level progressive achievements, eleven one-shot achievements, server-authoritative unlocks, claim-only AP awards, pending-reward badges, queued Result toasts, and equipable achievement titles.
- Added additive achievement progress, unlock, event-deduplication, and append-only AP ledger storage, including idempotent multi-level Claim and safe retroactive `첫 걸음` unlocks for normally completed Placement runs only.
- Added a Trophy screen beside Records with config-driven Korean/English cards, I–V presentation, progress bars, rarity/AP details, title-version selection, and the existing CorumSans UI font system.
- Shows equipped titles in the Ranked profile, Match Found/Prepare, and Match Result player cards without changing gameplay authority, rating, Placement, or Practice/spectator rules.
- Death Match progress-bar icons now remain at each side's best progress for the current Death Match map and advance only when valid live progress exceeds that best; winner/scoring logic remains unchanged.
- Added rules, migration, server integration, claim/idempotency, title, placement-backfill, UI-domain, and Death Match best-progress regression coverage.
- Updated the exact client/server version gate to `v0.5.0-beta.16`; Release remains manual-only.

## v0.5.0-beta.14

- Death Match no longer uses Qualifying % or the normal Ranked score curve; three authoritative Attempts are compared by best raw progress %, with Clear fixed at 100% and ties repeating Death Match.
- Death Match HUD uses the timer position for authoritative remaining Attempts (3 → 2 → 1 → 0) and keeps per-side Start usage separate.
- Opponent `Finished` now appears only after that side has no legal Attempt remaining; ordinary deaths and the first two Death Match endings retain progress without falsely marking the player finished. Practice presence remains viewer-relative and non-scoring.
- Added stable Placement Top 2 HUD rows with provisional active-Attempt insertion and animated reorder while persisted Grade remains Attempt-End authoritative.
- Added optional Jukebox allowlist/prepare compatibility and retained effective song-path validation through Geometry Dash.
- Prewarms Ranked FMOD streams outside competitive transition callbacks and adds transition timing instrumentation.
- Removed the remaining Round 3 transition stall by opening/seeking FMOD streams asynchronously, retaining the current BGM until the replacement is ready, and caching validated audio paths.
- Added verified external Rank Asset Pack cache/download infrastructure with bundled fallback; production downloading remains disabled until real Drive metadata is configured.

## v0.5.0-beta.13

- Shows the real opponent Geometry Dash icon in Ranked cards and the new compact opponent-progress HUD.
- Restores persistent Silver/Gold ambient effects across compact Ranked badge contexts and adds tier ambience behind the Prepare opponent icon.
- Keeps eligible Placement final-window deaths inside the same PlayLayer and restores the continuous two-map-to-stage-grade merge animation.
- Allows Practice Mode as a strictly local, non-scoring state; entering it closes the normal Attempt once and leaving it starts fresh from zero when allowed.
- Locks the client to the canonical production Ranked server and removes the editable server URL setting.

## v0.5.0-beta.12
- 허용되지 않은 활성 모드를 감지하면 모드 이름과 ID, 해결 방법을 Ranked 게임 내 경고창으로 안내합니다.
- 필수 모드 누락·비활성화·버전 불일치와 CBF 설정 문제도 같은 경고창에서 구분해 표시합니다.
- Node IDs `v1.23.3` 이상을 필수 dependency와 서버 허용목록 기본값에 추가했습니다.

## v0.5.0-beta.11
- Placement 시작 시 3회/5회 진행 방식을 선택할 수 있습니다. 선택 후에는 변경할 수 없습니다.
- 3회 Placement는 3 Stage(6 Map)로 종료되며 최대 배치는 AQUA로 제한됩니다.
- Placement 시작 설정 화면에 제한 경고와 배치 건너뛰기를 함께 배치했습니다.

# v0.5.0-beta.10

- Restored PvP Round and Death Match preparation to an exact ten-second countdown instead of the legacy 60-second resource floor.
- Fixed Placement Grade rendering so transparent PNGs stay transparent, reduced Grade art sizing, and rebuilt Map/Stage results as a compact `MAP 1 + MAP 2 > STAGE GRADE` three-card layout with per-card one-decimal scores.
- Unified live Placement provisional score/Grade evidence between progress updates and normal state polling, preserving monotonic Attempt progress and preventing D/C/B Grade oscillation from stale poll responses.
- Kept Stage-by-Stage Placement sessions, Play Later/resume, progress dots, and Placement history while standardizing Placement score presentation to one decimal.
- Updated the exact client/server version gate and workspace metadata to `v0.5.0-beta.10`.

# v0.5.0-beta.9

- Changed only solo Placement Maps to a three-minute Main Time, ten-second final Attempt start window, two-Clear target, and best-two Attempt Map Score capped at 500. Ranked PvP remains five minutes, three Clears, and otherwise unchanged.
- Added server-authoritative live provisional Map Score updates when an active Attempt fills or improves the current Top 2; the S/A/B/C/D Grade still changes only after Attempt End.
- Added persisted ten-second Placement preparation. Exact map loading starts immediately, the real GD LevelInfo page appears with five seconds left, and MAP_PLAYING begins at zero.
- Added deterministic per-Pool Reference Maps to the starting Pool screen, preserved literal `POOL 1` through `POOL 6`, and completed the Korean translations for the surrounding Placement UI.
- Moved starting-Pool selection behind the lobby's explicit Start Placement action and added a custom red skip confirmation that finalizes exactly once at RED - 150 LP.
- Reworked Placement Map/Stage result presentation with smaller transparent Grade art and a three-card `MAP 1 + MAP 2 > STAGE GRADE` composition, while retaining image-to-text fallback and existing result audio.
- Removed the unexplained fixed five-second post-window delay; an accepted active final Attempt still settles its Death/Clear and score before the Map Result becomes available.
- Added migration `0008_placement_beta9.sql`, recovery/skip/provisional/final-window regression coverage, and bumped the exact client/server gate to `v0.5.0-beta.9`.

# v0.5.0-beta.8

- Replaced PvP Placement with a separate server-authoritative solo aggregate: five Stages, two distinct Pool maps per Stage, persisted map snapshots, Attempts, top-three evidence, Stage results, deadlines, heartbeats, final rating, and result acknowledgement.
- Blocked public PvP Queue and Debug Bot Match until solo Placement is complete. Existing placed profiles continue through the unchanged Ranked PvP path.
- Reused the exact Ranked Attempt curve: qualifying threshold, 50%=75, 75%=150, and Clear=250. Each Map sums only its three highest authoritative Attempts; three Clears immediately force 750 points, S Grade, and Map completion.
- Added configurable S/A/B/C/D thresholds, Stage averages, S/S and D/D consistency movement, Pool 1–6 clamping, distinct canonical map selection, unused-map preference, configurable Pool rating anchors, and all-ten-map Placement Rating finalization.
- Preserved the existing non-lowest tier cutoff exclusion `[cutoff-15, cutoff+15] → cutoff-16`, tier floor, `TIER - N LP`, `PLACED`, and `RankTransitionLayer::Placement` reveal.
- Replaced Placement Qualifying HUD with a single image-first Grade badge. Grade changes occur only after Attempt-end authority; same Grade does not animate, upgrades use a short 0.8→1.12→1.0 pop, downgrades use a short fade, and missing images fall back to animated text.
- Added Map Result and Stage Result UI. The two Map Grades move inward, merge with a flash, and reveal the Stage Grade; S/A/B reuse ResultWin audio and C/D reuse ResultLose audio.
- Bundled the supplied `placement-grade-s/a/b/c/d.png` resources without modification and retained a crash-safe text fallback.
- Kept beta.7 exact real-level preparation: lookup/loading starts at the ten-second countdown, overall loading progress is visible, and the real `LevelInfoLayer` is revealed at five seconds remaining when ready.
- Added recovery and regression coverage for active Attempt, Map/Stage boundaries, server restart, result acknowledgement, top-three scoring, three Clears, Pool movement, all-ten-map rating evidence, queue/debug blocking, existing PvP, Ban, BO3, LAST ATTEMPT, Deathmatch, rating, Promotion, and Demotion.
- Updated the exact client/server version gate and workspace metadata to `v0.5.0-beta.8`.

# v0.5.0-beta.7

- Removed the synthetic `GJGameLevel::create()` fallback from Ranked map entry. The client now opens `LevelInfoLayer` only with the exact saved Geometry Dash level or an exact `SearchType::Type19` online result whose Level ID still matches the current server-authoritative match, Round, and map context.
- Starts exact-level resolution at the beginning of the existing ten-second Round/Death Match prepare phase instead of leaving the first five seconds idle. As soon as the real level object is available, its real `LevelInfoLayer` begins vanilla-owned map/update and song acquisition in the background.
- Added a full-screen map preparation window for the first half of the countdown, then a smooth reveal of the real Geometry Dash level page at five seconds remaining. If exact-level lookup completes after that boundary, the real page is revealed immediately.
- Added a continuously animated overall preparation bar across level lookup, vanilla map/update, song readiness, Ready/opponent wait, and start. This is phase progress rather than a fabricated network-byte percentage, and each incomplete stage remains capped until its real completion signal arrives.
- Preserved automatic launch through the verified real Geometry Dash Play item, server-authoritative Ready/PLAYING transitions, optional-song bypass only at countdown zero, and all existing attempt, scoring, LP, MMR, and match rules.
- Updated the exact client/server gate and workspace package metadata to `v0.5.0-beta.7`.

# v0.5.0-beta.6

- Added a dedicated full-screen, server-authoritative rank reveal after the result is confirmed. Placement Reveal, Promotion, and Demotion now run for roughly 4–8 seconds with a 1.5-second Skip lock, an explicit Continue step, and safe return to the settled result screen without changing rank, LP, MMR, placement, or promotion-series decisions.
- Built cumulative tier evolution from 31 supplied transparent structural layers: Red adds its real spike layer, Aqua assembles split front/back orbit layers, Bronze attaches four armor plates, Silver adds separate wings and a crystal crest, and Gold completes the supplied side ornaments, lower crest, radiant structure, and crown. Promotion visibly attaches those actual parts, Demotion drops the newest parts first, and Placement breaks the supplied Unranked fragments before assembling the authoritative target tier.
- Added 15 supplied sprite-based transition/ambient effects plus a full-screen background and geometry overlay. Normal lobby, Match Found, Round prepare, and result badges keep their existing layout while gaining lightweight tier ambience; Silver receives a rotating halo/orbit and crystal sheen, while Gold receives layered halos, radial rays, and crown aura.
- Replaced every Unranked/Red/Aqua/Bronze/Silver/Gold trophy resource with the supplied candidate 5 transparent PNG set. Placement supports Unranked into any Ranked tier, while presentation guards refuse Red/Unranked demotion animation.
- Fixed same-map Death Match re-entry after an Attempt returns to the retained Ranked round scene. The client now releases its completed child-scene launch guard and automatically opens the next legal Attempt, while the authoritative 3/3, spectator, result, and start-window checks remain unchanged.
- Added a persistent `English / 한국어` selector to the Ranked lobby.
- Added the editable `src/ui/RankedTranslations.hpp` translation table and Korean UI glyph coverage for Paperlogy SemiBold, ExtraBold, and Black.
- Korean mode translates score, placement, rating events, promotion/demotion states, matchmaking, ban, result, history, and spectator UI while intentionally keeping `CORUM RANKED`, `ROUND`, `Qualifying`, tier names, LP, player names, and map names in English.
- Removed the obsolete lower-right Korean `디버그` font probe. The optional Debug Bot Match controls remain development-only.
- Updated the exact client/server gate and workspace package metadata to `v0.5.0-beta.6`.

# v0.5.0-beta.5

- Rebuilt the Ranked presentation around a unified dark competitive design system while preserving every beta.4 server-authoritative state and gameplay callback.
- Replaced the prototype font with user-supplied Paperlogy Regular, Medium, SemiBold, ExtraBold, and Black roles across the Ranked scene, gameplay HUD, LevelInfo wait gate, and main-menu shortcut. SemiBold is now the default UI voice and Black is reserved for critical values and actions.
- Redesigned the lobby, queue, Match Found, private ban, Round prepare, spectator, inter-Round, result, PERFECT WIN, and history views with consistent cards, hierarchy, spacing, and state accents.
- Removed both the large gameplay HUD cards and the later text-sized rectangular scrims. Enlarged the important metrics to the supplied edit's proportions, added tightly offset glyph shadows for contrast, separated caption/value weights, and kept the current Round directly above the authoritative timer.
- Added a debug-only Korean `디버그` font probe in the Ranked lobby, retained optional user-replaceable tier trophy slots, and replaced the repeated fire sprites with a subtle animated accent rail.
- Merged the Play/update-loop hotfix: canonical Saved levels retain their native type, automatic entry verifies the live Play sprite and vanilla download state, preserves Geometry Dash's original callback, and waits for busy update work instead of repeatedly pressing the update item.
- Updated the Ranked build invariant checks so direct callback rebinding and the obsolete forwarded latch cannot return.
- Updated the exact client/server gate and workspace package metadata to `v0.5.0-beta.5`.

# v0.5.0-beta.4

- Added configurable opponent-strength weighting for Hidden MMR and visible LP, rewarding wins against stronger opponents more and penalizing losses to weaker opponents more.
- Added server-authoritative `PERFECT WIN` classification for no-loss match wins whose decisive rounds all end through a real three-Clear LAST ATTEMPT result; draws remain eligible while forfeits, forced wins, score wins, and Death Match wins do not.
- Awards the qualifying winner the existing formulas' theoretical maximum MMR and normal LP gain while leaving the loser's calculation unchanged.
- Displays `PERFECT WIN` in yellow on the qualifying winner's result card.
- Displays the current Round, or Death Match sequence, directly above the gameplay timer.
- Updated the exact client/server gate and workspace package metadata to `v0.5.0-beta.4`.

# v0.5.0-beta.3

- Replaced the unattached selector proxy with automatic activation of the real visible Geometry Dash Play menu item after normalizing its target to the active Ranked `LevelInfoLayer`; Ranked never constructs or switches to `PlayLayer` directly.
- Removed the manual Play fallback. During the Ranked wait gate only vanilla song acquisition controls are enabled, and a failed real-button activation retries automatically while server authority still permits entry.
- Resets the LevelInfo launch latch after a successful vanilla transition so quitting a live attempt back to the map page automatically presses Play again for the next legal attempt.
- Made `Ranked Tiers.Main Pool` authoritative for the five-map distribution while preserving the existing default distributions.
- Made CSMP Seed tier names operator-defined, added cross-sheet fail-closed validation, and added PostgreSQL migration `0006_v050_dynamic_csmp_tiers.sql` without re-seeding existing profiles.
- Updated the exact client/server gate and workspace package metadata to `v0.5.0-beta.3`.

# v0.5.0-beta.2

- Fixed the iOS `STARTING -> FAILED` loop by dispatching automatic Play through a selector owned by the active Corum Ranked `LevelInfoLayer`, instead of trusting a visible Play item's possibly rebound callback.
- Added a direct vanilla `onPlay` fallback and explicit launch forwarding logs so a rejected proxy activation cannot silently consume all retries.
- Cancels and closes an obsolete LevelInfo launch immediately when server authority replaces the Round/map context, preventing Round and Death Match level pages from stacking during a fast state transition.
- Includes the iOS build header hotfix for `CCParticleFire` and the fully-qualified `corum::ranked::RatingEventView` hotfix.
- Integrated the development-only Debug Bot build switch into `Build Corum Ranked Mod`; manual runs default to Debug ON, while push/PR/reusable official builds remain OFF and validate the compiled binaries.
- Updated the exact client/server gate and workspace package metadata to `v0.5.0-beta.2`. No database migration or Apps Script update is required from beta.1.

# v0.5.0-beta.1

- Split visible LP from hidden MMR. Placement begins at hidden LP 0, records `O/X` outcomes, and reveals a cutoff-safe final placement only on the last placement result.
- Added configurable promotion series, cutoff-1 entry, cutoff+50 success, cutoff-50 failure, tier-floor protection, demotion warnings, and demotion/re-promotion handling.
- Added score/clear-margin and streak-weighted hidden MMR changes, with reduced movement when the series winner trails in total Score or Clears.
- Added placement/promotion tracks, green `PLACED`, LP transition animation, promotion flames, demotion warnings, and 2x+ win/loss streak labels to the lobby and result UI.
- Updated attempt scoring to x1.5 from 50%, x2 from 75%, and a fixed 250 points at 100% Clear.
- Starts Match/Result music immediately while retaining Match music through the LevelInfo countdown until the real PlayLayer starts.
- Fixed the `STARTING -> FAILED` loop by resolving the actual vanilla Play sprite instead of activating the first menu child, preserving the original arm timestamp and bounding automatic retries.
- Added development-only Force Win/Force Lose and changed `CORUM_RANKED_DEBUG_BOT_MATCH` to default OFF for official packages.
- Added PostgreSQL migration `0005_v050_rating_system.sql`, Apps Script promotion/LP/MMR policy fields, `corum-ranked-v0.5`, and the exact `v0.5.0-beta.1` client/server gate.

# v0.4.0-alpha.40

- Moved the authoritative Ranked map catalog to `sheet2` by default while preserving Corum Integration's independent `sheet1` routing; `RANKED_MAPS_SHEET_NAME` can override only the Ranked tab.
- Updated the fixed Ranked rules to `corum-ranked-v0.4`: five-minute rounds, three Clears, and the existing ten-second FINAL/LAST attempt-start windows.
- Expanded the client HUD and result/history UI to three Clear checks, and aligned client re-entry blocking, server spectator authorization, Debug Bot scenarios, and regression fixtures with the new Clear target.
- Preserved in-progress legacy `corum-ranked-v0.3` match snapshots as two-Clear matches during a rolling server deployment; newly created matches validate at exactly three Clears.
- Fixed blank/default Geometry Dash LevelInfo metadata by normalizing the downloaded Ranked level as a Search Result and synchronizing the server-provided title/creator before the vanilla layer renders.
- Fixed repeated Android Ranked resource downloads by making the mod's persistent save directory the authoritative BGM cache. Geometry Dash's custom-song path is now a best-effort compatibility mirror and can change without reopening the resource gate.
- Updated the exact client/server version gate and workspace metadata to `v0.4.0-alpha.40`. No database migration is required; update/redeploy Apps Script and rerun `setupCorumRankedConfig()` after creating `sheet2`.

# v0.4.0-alpha.39

- Normalized Attempt End progress to the server's three-decimal wire contract, preventing raw Geometry Dash percentages from returning HTTP 400 and permanently blocking the FIFO before later Clears.
- Treats deterministic HTTP 4xx Attempt Start/End failures as terminal for that event while continuing to retry network, 5xx, timeout, Too Early, and rate-limit failures.
- Removed the initialization-only phantom `Start -> End 0 -> Start` sequence by journaling the first attempt after Geometry Dash's short scene-setup reset window; a genuine early death/Clear still self-recovers and is recorded.
- Fixed the three-minute boundary HUD so it changes directly from the normal clock to visible `FINAL ATTEMPT 10.0 ... 0.0`, even before the next authoritative poll arrives.
- Enforced the 10-second new-attempt deadline locally for stale `ROUND_PLAYING` snapshots while preserving every attempt that started in time until its natural death or Clear.
- Fixed Ranked completion-layer exits: closed Replay is redirected through the vanilla Menu path, and two-Clear/result transitions leave the completion overlay without orphaning its controls.
- Replaced the misleading `OPENING LEVEL / WAITING MAP` presentation during a pending second-Clear acknowledgement with `CLEAR RECORDED / SYNCING RESULT`.
- Added server-side defensive Attempt End normalization plus regression tests for second Clears after 3:00 and LAST ATTEMPT windows crossing the original 3:00 deadline.
- Fixed the `Validate Corum Ranked` Actions fixture so every locally generated Ranked Pool map includes the now-required alternate map code; the workflow no longer fails independently of the operator's real Spreadsheet values.
- Preserved the existing Geometry Dash LevelInfo Play-button activation path and Match music handoff; gameplay is not constructed directly by Ranked.
- Updated the exact client/server version gate and workspace metadata to `v0.4.0-alpha.39`. No database migration or Apps Script change is required.

# v0.4.0-alpha.38

- Fixed the reproducible iOS `CCTouchDispatcher::handleTouchesEnd` crash by stopping 200 ms full-menu rebuilds and explicitly clearing `CCMenu::m_pSelectedItem` before any structural UI cleanup. Countdown, download, queue, and spectator values now update in place.
- Added explicit Ranked scene input cleanup: the refresh schedule, keypad delegate, and menu touch dispatcher are disabled before the Ranked scene exits.
- Added poll-request generations and cancellation. A response from a dismissed/previous match can no longer restore old match state after **QUEUE AGAIN**, Close, Leave Queue, or a new session.
- Replaced the LevelInfo `onPlay()` watchdog/retry loop with direct construction of `PlayLayer` from Geometry Dash's canonical saved level payload. A non-null `PlayLayer::scene()` is now the launch success signal, removing the observed endless `SENDING READY...` / repeated arm loop.
- Updated the exact client/server version gate and workspace metadata to `v0.4.0-alpha.38`.
- Scoring, MMR/RP, map pool, ban rules, attempt limits, and server database schema are unchanged.

# v0.4.0-alpha.33

- Added durable PostgreSQL Start Leases for every standard-round visual attempt so a delayed FIFO Start/End cannot be lost when polling reaches a Round result first; leases survive Render process restarts.
- The client no longer discards completed attempt transport when an authoritative result poll races ahead, and newest lease requests preempt older slow lease HTTP waits.
- Added idempotent round Start/End reconciliation for lost HTTP acknowledgements.
- Fixed Death Match visual budgeting so an attempt is consumed only when the new visual Start journal is actually created; the third visual attempt is no longer pre-spent while the server still shows 2/3.
- Death Match popup usage now includes the viewer's locally-created visual attempt while its server ACK is pending.
- Keeps the next Round / MATCH POINT / Death Match map card visible for the first 5 seconds before opening the normal LevelInfo/resource gate; the existing total 10-second prepare period and authoritative timer semantics remain unchanged.
- Replaced Ranked BGM playback through `FMODAudioEngine::playMusic()` with a Ranked-owned direct FMOD streaming Sound/Channel.
- Added explicit `pauseSound()` handling plus iPadOS/iOS `resumeSound()` recovery, FMOD master-output normalization, playback-position watchdog/retry, and automatic loop recovery.
- Ranked BGM volume is Geometry Dash's current Music Volume multiplied by 0.80.
- Ranked resource downloads prefer Geometry Dash's canonical song path and migrate valid old private-cache songs when possible.
- Added PostgreSQL migration `0002_attempt_start_leases.sql`. Apps Script, scoring/MMR, map pool, ban rules, and FINAL/LAST 10-second start-window rules are unchanged.

# v0.4.0-alpha.32

- Fixed Ranked BGM remaining completely silent even after all configured songs downloaded successfully.
- Replaced the old `fadeOutMusic() -> playMusic()` handoff with a fade followed by `stopAllMusic(true)` before installing the Ranked track, preventing the previous Geometry Dash menu channel from remaining registered in a silent/fading state.
- Explicitly unpauses the main music channel group and restores Geometry Dash's current music volume before Ranked playback.
- Added a playback watchdog: the client verifies that FMOD actually created a live main-music channel and retries the track up to two times instead of permanently assuming a failed `playMusic()` call succeeded.
- Delayed configured start-position seeking until the new music channel is confirmed active.
- Ranked private-cache files now require a valid MP3/Ogg/FLAC/WAV/MP4 audio signature; HTML/error bodies can no longer be accepted merely because they are larger than 1 KB.
- Leaving Ranked now hard-stops the Ranked music slot before asking Geometry Dash to recreate its normal menu music.
- No server, scoring, MMR, map-pool, attempt, FINAL/LAST ATTEMPT, or Debug Bot behavior was changed.

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
