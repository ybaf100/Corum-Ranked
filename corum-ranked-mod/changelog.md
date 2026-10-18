# v0.5.0-beta.83

- Fix a Solo Ranked native round that stays black/frozen in ROUND_ENTERING: the process-owned preparation heartbeat now advances the existing guarded native scene entry and server-ACK/countdown gate when PlayLayer postUpdate and its scheduled callbacks stop during the entry hold.
- Keep the authoritative /entered API, both-player acknowledgement, server startsAt, scene identity, input/music gating and attempt journaling unchanged. Do not release ordinary, spectator, Survival, detached or exiting scenes.
- Add a regression test for the independent 100 ms entry pulse, retaining all existing native entry and pause tests. Device verification is required to confirm the iPadOS failure is resolved.
- Synchronize the client manifest, npm workspace and lockfile, and server required-client version to v0.5.0-beta.83; publish new immutable Debug/Release packages via CI.

# v0.5.0-beta.82

- Migrate the Corum Ranked production client endpoints from Render to Railway, including Solo Ranked, Survival, spectator, and debug APIs.
- Keep the existing Neon database, ranked rules, gameplay protocol, and Server-authoritative match behavior unchanged.
- Synchronize the Geode manifest, npm workspaces, lockfile, and server-required client version so the server advertises v0.5.0-beta.82.
- Publish fresh immutable Debug and Release packages to R2 through GitHub Actions when runners and R2 publishing are available. Older clients pointing to the suspended Render hostname need a working legacy update endpoint or one manual install to reach Railway.
- Add client source endpoint regression coverage; physical device behavior and update-package publication require independent validation.

# v0.5.0-beta.80

- Use Geode force-priority-aware targeted touch registration for the two in-game Survival Pause actions; record actual touch coordinates, item hit rectangles and gate outcomes for future iPad reproduction.
- Provide a button-only targeted touch route for Survival confirmation popups, retaining normal native menu callbacks and strict once-only dismissal/ACK validation.
- After a native round scene returns to the authoritative coordinator, clear only the stale global application pause flag retained from the departed Pause; never unpause a running native gameplay scene.
- Keep the thumbnail reel when images are still downloading instead of permanently locking the numeric reel on the first uncached frame. Delay freezing the preview list until it has multiple images, and never display an unrelated winner image.
- Increase the selected level thumbnail and reel cards while preserving compact layout constraints.
- Keep the server-authoritative countdown and confirmation rules unchanged. Native fixtures cover force-aware touch registration and asynchronous thumbnail choices.
- Synchronize the mod, rules, server and required-client metadata to beta.80. Device touch behavior still requires hands-on iPadOS verification.

# v0.5.0-beta.79

- Fix Survival Pause surrender and Debug Force Clear taps on iPadOS by validating the authoritative scene while native PlayLayer is paused, retaining modal protection for newly opened dialogs, and accepting confirmed actions from that paused owner.
- Recover re-entry when the previously popped, stopped native PlayLayer remains attached to a stale, non-running scene. Never detach an active scene or running game.
- Recover a stuck Survival match-state poll after eight seconds and reject late callback responses from cancelled requests.
- Mark live players currently below the qualification cutoff with a red card background, without waiting for elimination to become final.
- Center Survivor and Selected Map titles in the full screen independently of the top-right round timer.
- Enlarge carousel thumbnails, use continuous velocity-matched easing, freeze thumbnail order during animation, and avoid numeric fallback if the final thumbnail is still loading.
- Show START / 시작 instead of READY / 준비 when reopening the selected map after already entering the round; preserve the same server-authoritative entry permissions.
- Update related native C++ test doubles and regression assertions. Device-specific behavior still needs physical validation.
- Synchronize client and server versions to beta.79, avoiding overwrite of immutable beta.78 R2 packages.

# v0.5.0-beta.78

- Wait five server-authoritative seconds after all players confirm assets ready, then preserve the existing five-second native-entry allowance and three-second gameplay countdown after everybody enters.
- Diagnose and repair a missing Survival Pause touch delegate after event dispatch; retain native modal, authentication, scene-owner and ACK protections for surrender/Debug Force Clear.
- Restore the Survival coordinator when an old native scene is already retired; avoid stale queue-match polling and skip only a detached, stopped PlayLayer singleton in the native-entry gate.
- Add selectable Survival roster avatars that open a match-scoped player information panel (rank, best/now progress, attempts, tier and GD account or bot indicator).
- Tighten Survival top-header spacing; preserve the already-implemented red elimination cards.
- Regression checks updated for five-second ready room and missing touch-handler registration. CI, Windows/iOS builds and device interaction were not run; real-device verification is still required.
- Version metadata and server required-client gate updated to beta.78. Matching server deployment is required; no Render/R2/Sheets publication was performed.

# v0.5.0-beta.77

- Route Survival Pause surrender and Debug Force Clear taps through their exact visible bounds before native Pause blockers; respect confirmations, retired scenes and server acknowledgement.
- Read thumbnail HTTP headers case-insensitively, warm previews during the roster and keep a round in numeric fallback once chosen.
- Keep all ready players in map preparation for three server-timed seconds before native entry opens; retain the full native-entry timeout and existing gameplay countdown.
- Keep the LDM control visible and disabled as NO LDM / LDM 없음 on unsupported maps, without moving the other controls or changing saved preferences.
- See [causes, implementation and validation limits](../ranked/docs/survival-beta77-input-thumbnails-ready.md).

# v0.5.0-beta.76

- Add a shared four-second server-map reveal: optional public Level Thumbnails carousel or one horizontal row of vertical ID reels, with safe numeric fallback and server-start skips.
- Replace the Survival map panel with responsive image/numeric layouts, native map metadata, separate readiness status, round switches and a server-authorized yellow READY button.
- Add saved Survival defaults to Ranked Settings; migrate prior preferences once and keep same-round overrides across native object replacement and re-entry.
- Bound anonymous thumbnail requests, WebP validation/decode, cancellation, disk and texture caches. Preserve server selection, ranking, FREE PLAY and Jukebox ownership.
- Put the Survival surrender action back in the native Pause menu with its full caption inside the touch target; release closed confirmation input priority immediately. Allow waiting-room MAP_SELECTION surrender with server acknowledgement and idempotent settlement.
- See [implementation, validation and device limits](../ranked/docs/survival-beta76-map-selection.md).

# v0.5.0-beta.75

- Restore Survival Pause action touch handling and deferred surrender confirmation.
- Keep Debug Force Clear visible while entry acknowledgement is pending; retain server permission checks.
- Fit Survival result headings and card text within their reserved bounds.
- Block native gameplay construction while another scene transition or native PlayLayer owner remains.

# v0.5.0-beta.74

- Keep a server-synchronized round/start clock on the Survival board and distinguish preparation states without invented deadlines.
- Let unfinished players return from gameplay to map preparation, then browse the retained board and return to the same map without surrendering; revalidate the server before re-entry.
- Preserve original player-name casing, including a lowercase-capable fallback font. Color retained cards by elimination first, then the server's podium rank.
- Display reliable map seconds, native length categories, or UNKNOWN LENGTH; stop interpreting verification timestamps as map duration and invalidate the old display cache namespace.
- Reapply saved Survival LDM/Disable Shake options after native level replacement and immediately before native launch/init. Hide unsupported/unknown LDM and reflow the remaining controls.
- Add default-TRUE modRestrictionEnabled across Sheets, snapshots, public config and native/server admission. Optional loader dependencies permit FALSE; TRUE still enforces existing required rules. Version, authentication, HAC and gameplay validation remain independent.
- See [implementation, validation limits and operations steps](../ranked/docs/survival-beta74-board-settings.md).

# v0.5.0-beta.73

- Keep the Survival Pause countdown tied to the last valid server clock; show transient status separately.
- Move Debug FORCE CLEAR out of the native icon row and align its registered touch priority with Pause.
- Return from Survival gameplay through the deferred scene handoff as soon as the server accepts surrender, including while paused.
- Use the previous major tier for the Solo Ranked score track across all divisions; preserve foreground, LP and rank effects.

# v0.5.0-beta.72

- Return normal Survival 100% clears to the live dashboard after the server's terminal Attempt ACK and native completion setup. Keep Death Match Attempts and score precision unchanged.
- Show server BEST/NOW, cleared and eliminated status on retained participant cards. Retarget the existing movement from the current position and update elimination backgrounds independently.
- Add FREE PLAY after acknowledged normal clears. Keep polling and attach the same unchanged SurvivalHud to ordinary GD levels; preserve vanilla progress/Practice/records and return safely at the authoritative round boundary.
- Add Debug-only Pause Force Clear with the Corum confirmation, dedicated authenticated server route, exact context checks and persistent request idempotency. Never grant native GD completion rewards.
- Retain beta.71 round-handoff/BGM/final-result fixes and the user's round HUD position. Synchronize server/client versions without a migration.
- Instrument R2 transfer/save/hash/install timing, coalesce update progress callbacks and save downloads on the verification worker while preserving validation/rollback. Add an optional Custom Domain origin override and a public download probe; the reported slow transfer was not reproduced and no production domain/cache setting was changed. See [measured R2 diagnostics](../ranked/docs/r2-beta72-download-diagnostics.md).
- See [implementation and device checks](../ranked/docs/survival-beta72-free-play.md).

# v0.5.0-beta.71

- Return Survival protocol conflicts as HTTP 409, retire obsolete progress after server round completion, and reject delayed canceled-request callbacks so presentation completion cannot remain behind endless progress retries. Keep valid Attempt End retries, round timers, scores and LP unchanged.
- Select Survival music from the local player's server outcome: keep normal music for survivors, use the existing Final/Death Match preparation track, and use server winner IDs/result flags for final audio. Preserve fades, native GD music and configured volumes.
- Prevent NEXT MAP SOON/preparation controls after final results, elimination, settlement pending or cancellation. Preserve the user's round HUD label position from bd92313e9.
- Make Common Admin DB reset delete all Ranked application data, including accounts, sessions, both modes' records, AP/titles, caches and audio overrides; preserve schema/migration history and require fresh login. Keep Solo/Survival reset scopes independent.
- Synchronize beta.71 package/server versions. No DB migration or R2/production deployment. See [round handoff validation](../ranked/docs/survival-beta71-round-handoff.md) and [Common reset behavior](../ranked/docs/admin-beta71-common-reset.md).

# v0.5.0-beta.70

- Use integer highest-progress scores for general Survival ranking and cutoffs; retain raw Attempt evidence and .001% Death Match adjudication.
- Broaden Rated catalogs with liked/recent/awarded searches and full reported pagination. Exclude XL at search, predicate, download and native entry; persist the last 30 confirmed selections across all Survival games with atomic conflict checks and history-only relaxation.
- Open the shared Jukebox dialog, defer native entry while it is attached, and retire old-round dialogs. Render Featured/Epic/Legendary/Mythic through native GD difficulty sprites, including delayed metadata.
- Use Round12 BGM for every preparation phase, hold elimination/result audio through presentation, and select final result music from server winner IDs. Add a smooth personal-best progress marker in Player Color 1 alongside distinct cutoff/leader markers.
- Add exact Bot Count to queue fill (0–15) and dedicated debug games (1–15), start immediately, and reuse short finals, scaled LP rewards and actual participant-count Force bounds.
- Keep Solo 125ms and Survival 250ms execution while discovering active debug games every 2s; register creations and recover at startup immediately. Retain failed discoveries and unsettled finals for safe payment retries.
- Scope Admin DB reset to Solo, Survival, or both plus shared achievements/AP/titles for Common. Accounts and sessions remain available.
- Add migration 0026 and additive API fields. See [implementation, SQL measurements and validation](../ranked/docs/survival-beta70-improvements.md). Existing R2 publication paths remain unchanged.

- Fix the packaging font budget overflow by reducing the reserved, unused Regular atlas. Keep active bilingual UI/countdown bundles unchanged; actual generated UHD fonts shrink from 98.77 to 83.84 MiB and pass the existing 96 MiB limit.

# v0.5.0-beta.69

- Split Solo RED, AQUA, BRONZE and SILVER into III / II / I divisions with editable LP cuts and per-division map pools. Series and demotion protection remain at base-tier boundaries.
- Allocate exact CHAMPION and MASTER slots among GOLD players, including bots. Use the existing deterministic ladder order for ties and freeze both title-cut targets in each match result.
- Reuse GOLD badges for MASTER/CHAMPION, vary SILVER halos by division, keep lower promotions to text, and add a GOLD-button tier guide to the Solo matching card.
- Show up to ten Survival leaderboard rows in every round, shorten the cutoff line to content width, show live time in the re-entry screen, and unify the ready hint. Remove “(1배)” from Korean rules.
- Add migration 0025 and an explicit operator Solo-only reset script. Preserve Survival, shared achievements/AP/titles and accounts. See [beta69 configuration notes](../ranked/docs/solo-beta69-divisions.md).

## v0.5.0-beta.68

- Complete public Survival matchmaking with durable, independent queue/status/heartbeat/leave/recovery APIs. Match 16 immediately, or 2–15 after 180 seconds; repeat one-person waits. Preserve admission fencing and cancel waiting entries on Survival OFF while started matches continue.
- Support confirmed reduced-size targets and reward mapping, plus Final-only 2–3 player games and Round 3 → Final 4–7 player games. Keep 180-second rounds, cutoff ties and repeated .001% three-attempt finals.
- Fill the public queue to 16 with existing seeded beta67 bots after requester Debug-build/password authorization. Release peers join the same authoritative game. Pay real humans only; continue while any human survives and conclude bot-only games by current-stage records.
- Connect Release/Debug clients to public queue and native map/audio/SFX preparation, common entry/countdown, actual attempts, retained HUD and result motions. Restore active queue/matches after app restart and session rotation; absent entrants score zero and may return mid-round without pausing server time.
- Settle each player's immutable elimination rank exactly once. Explicit surrender fixes that round's lowest rank and pays immediately; confirmation names the rank and lobby return. Refresh full final standings in already-paid records without paying again. Preserve Solo LP/MMR, shared AP/GOLD and independent history.
- Add additive migration 0024, PostgreSQL 16 multi-connection HTTP regression coverage, capability-gated lobby entry and beta68 version synchronization. Reuse unchanged fonts, GD relay, R2 immutable version paths and infrastructure. See [M5 notes](../ranked/docs/survival-m5.md) for rules, API and deployment/verification limits.

## v0.5.0-beta.67

- Defer Survival native scene construction outside the roster scheduler callback. Recheck exact round/map/scene/modal eligibility; guard concurrent setup and pending native quits. Retain the saved level, origin scene and native texture-cache owner at the entry/return boundary, with setup diagnostics for the reported iOS Round 2 crash.
- Show the current passing/cutoff leaders up to ten, with a separate own row when outside that list. Add a red cutoff separator. Keep retained row animation, Solo fonts and ordinary integer score display.
- Add real-time red cutoff and gold first-place markers to GD's existing progress bar, without enabling it for users who disabled it. Use authoritative scores and native bar/fill transforms.
- Nerf normal-round bots to independent normal progress candidates (mean 50%, standard deviation 20%), integer 0–100%, at 20/50/80/110/140 seconds with a 40% draw chance. Preserve best records, seeded replay and persisted event cursors; Death Match remains three immediate attempts.
- Synchronize beta67 client/server/package versions. No migration, Apps Script or R2 configuration change. See [beta67 notes](../ranked/docs/survival-beta67-round-entry.md).

## v0.5.0-beta.66

- Share Solo GameplayUx's actual timer/delta font sizes, weights and text-width pair positioning with Survival. Keep round text above the pair and remove the extra delta caption. Increase the lower-left rank and pop it on every authoritative rank change.
- Remove in-game rank-row backgrounds and size rank/name/score gaps from the rendered text. Display ordinary scores/progress as integers across HUDs, roster and history, retaining Death Match precision and existing server comparisons. Keep surrender exclusively in the native pause menu, with the existing confirmation.
- Hold the initial participant roster and every completed-round roster for five seconds. Keep elimination motion and the two-second post-motion wait inside that interval, enforce the minimum in server acknowledgements, and retain replay/recovery safety.
- Show a small existing animated tier badge/halo next to each roster avatar. Use server-provided current human tier snapshots and bot LP tier groups; virtual LP affects map difficulty without replacing the human's real tier icon. Support explicit tier-band group mapping for subdivided tiers.
- Synchronize beta66 client/server/package versions. Reuse unchanged font assets/charsets and keep the existing 96 MiB atlas limit. No new DB migration or Apps Script setup. See [beta66 notes](../ranked/docs/survival-beta66-hud-roster.md).

## v0.5.0-beta.65

- Fix the Debug/Release package font budget failure: reuse existing glyphs for the new roster/rules/pause captions and restore the established character sets, preserving all five font weights, raster sizes, Korean support and the 96 MiB RGBA limit. Validate actual generated SD/HD/UHD font resources without compiling a platform client.

- Share Solo's original shadowed HUD labels and Placement map heading/difficulty/duration components with Survival. Keep the FPS display unchanged and use larger, separately aligned rank/name/score columns with dark backing and retained row motion.
- Present all 16 entrants before map selection, show the selected native map's Solo-style information and resource readiness, then enter automatically. Persist a server presentation barrier so completed-round participant tiles and elimination motion finish, followed by a two-second wait, before selecting the next map; reconnects preserve the barrier.
- Restore Survival's clock after native Pause/Resume. Add confirmed surrender in both gameplay and the native pause menu, keep the server's 180-second deadline running, and return an expired paused round to its participant/result screen once. Restrict Force n to the matching screen.
- Reclaim waiting/result audio after native gameplay teardown and prevent the retained Solo lobby from replacing Survival result music. Add a single clockwise chooser-card shine on mobile tap and desktop click that survives the channel slide, while retaining chooser-only hover behavior.
- Synchronize beta65 admission/package versions and reuse the existing Korean font glyphs. No DB migration, Apps Script change or new GD relay configuration. See [beta65 flow notes](../ranked/docs/survival-beta65-ui-flow.md) for interfaces and validation limits.

## v0.5.0-beta.64

- Build server-owned candidate pools from recent Rated GD pages, with independent difficulty/Demon filters, exact match exclusions, download-time metadata/geometry attestation and bounded invalid-candidate quarantine. Keep durable selection retries and log concrete failures in Geode.
- Reuse Solo waiting music for Survival rounds, Round 3 music for Final/Death Match and result music for eliminated users. Retain completed round summaries across ticks; tint eliminated tiles red and animate icons away before entering the next map or showing result motion.
- Keep chooser cards enlarged while hovered and slow their single clockwise light pass. Disable rank-card hover/press responses inside channels. Hide all matchmaking contents, including inner button frames, until card motion completes, then fade their GD surface opacity with the icons/text.
- Publish immutable Debug/Release packages and descriptors to a configurable public R2 bucket instead of Ranked GitHub Releases. Retain exact version/channel/hash/size checks, fail safely on changed published bytes and support interrupted publication retries. Add operator setup instructions for `corum-ranked-download` and the initial manual beta64 installation.
- Synchronize beta64 client/server versions. Add no DB migration or Apps Script change. Per request, local builds/tests, device/package checks and new GitHub Actions results were not run. See [beta64 notes](../ranked/docs/survival-beta64-maps-r2.md).

## v0.5.0-beta.63

- Match Survival participant tiles to `surface()`'s actual `CCNode*` return type, fixing the release/debug platform compile failure without a downcast or changing tile rendering/actions.
- Correct the official-map enum to GD's actual `GJLevelType::Main` on every platform. Mirror the native enum in the metadata fixture and cover official Demon versus community Demon subtype presentation.
- Fix beta63 validation blockers: preserve Solo result structures, retire legacy entry controls after card motion, align native test boundaries with hover/footer/history changes, and keep full achievement-description assertions. Use a supported Korean nickname prompt. Report searches containing only ineligible maps as exhaustion rather than duplicates.
- Preserve the latest main Debug-button change. Keep Debug and Settings inside the lobby message strip, and add lobby-only hover/press pulses plus a single clockwise card highlight without changing frame thickness.
- Reveal matchmaking content only after the card crossing finishes; fade it out before reversing. Add player-name searches that jump to and highlight the matching leaderboard row while retaining mode/request isolation.
- Display Survival result ranks with podium colors and a red override for actual LP loss. Remove overlapping Solo labels from Survival history. Show Survival wins/losses/win rate using actual LP changes: an increase wins; unchanged or decreased LP loses. Remove the slash between card rank and Top percentage.
- Keep complete achievement descriptions with wrapping instead of ellipses. Enlarge Solo surrender text using Corum menu fonts, confirm Survival surrender, display official Demon maps as Demon, and fade in integer-only death-log percentages.
- Improve GD map selection with separate difficulty searches, bounded random pages, independent candidate validation and durable selection/retry status. Arrange alive players' native icons, names and records in the Survival waiting screen. Automatically enter ready rounds; show Re-enter only for a previously entered round. Retain LDM, Disable Shake and Jukebox options.
- Synchronize beta63 client/server versions. No new DB migration or Apps Script change. Per request, builds, tests, device/package checks and GitHub Actions inspection were not performed for this patch. See [beta63 notes](../ranked/docs/survival-beta63-fixes.md).

## v0.5.0-beta.62

- Connect Survival Debug Bot Match to real native GD map/resource preparation, server countdowns, round Attempts and the M3 engine, using one human and 15 deterministic server bots. Preserve the existing password and independent mode admission settings.
- Add three average-tier choices with generated bot LP snapshots, four staggered monotone 50% updates, immediate three-attempt bot Death Matches, durable recovery and password-protected Force 1–16 result controls.
- Add the Survival PlayLayer HUD below the existing FPS display, persistent moving rank rows, interpolated bot scores, cutoff/first-place differences and the large colored current rank. Keep Solo HUD and spectator protections intact.
- Pay actual Survival LP exactly once to the human, reuse promotion/demotion/LP result animations, retain bot joint winners when the human is eliminated and record bot rewards only in the result. Share GOLD/Claims without triggering Solo achievements.
- Scope Admin actions and resets to Solo, Survival or Common. Preserve unrelated mode data and shared accounts/sessions; keep Common achievement/AP/title resets explicit. Add migration 0023 and actual Survival history/card summaries.
- Synchronize the beta62 exact version gate and workspaces. See [Survival M4 documentation](../ranked/docs/survival-m4.md) for interfaces, operation and validation limits.

## v0.5.0-beta.61

- Implement the durable 16-player Survival engine, 180-second rounds, cutoff ties, final rankings and repeat three-attempt Death Matches. Capture Survival LP/config once and use server receipt deadlines with idempotent transitions.
- Select and download real rated GD maps on the server with the exact per-round star/Demon predicates, 40-million ID floor, data verification, durable selection leases and retries. Add native client metadata validation for M4 preparation.
- Persist Survival matches, entrants, rounds, attempts, tiebreaks and commands in migration 0022. Expose authenticated participant HUD state and trusted M4/M5 application ports; keep public Survival matching and online LP settlement closed.
- Restore full-size clickable player cards and equally sized matching panels. Fix history button replacement geometry/spacing, center matching labels, localize lobby messages and use dark gold-accented selected tabs/Solo history buttons. Slightly thin GOLD/promotion leaderboard frames.
- Show a localized Corum-font notice before entering a disabled channel's view-only lobby. Confirm opens the channel; Cancel/Back keeps the chooser, with repeated and obsolete callbacks retired safely.
- Synchronize the beta61 exact version gate and workspaces. See [Survival M3 documentation](../ranked/docs/survival-m3.md) for deployment, rules, integration and validation limits.

## v0.5.0-beta.60

- Restore the original compact player cards and matchmaking panel, with Enter below the cards, persistent top History/Achievement buttons and the bottom message/Debug strip. Align both matchmaking panels and highlight Solo History in gold.
- Reveal Survival's menu from behind its stationary card. Cross the two cards for Solo entry and blend the outgoing Survival card into its matchmaking panel at the midpoint; Back follows the same eased path in reverse with input locked until completion.
- Fix cascading transparent badge containers that hid structural tier sprites when mode cards faded. Keep badge and halo actions alive throughout transitions.
- Mark the selected leaderboard mode with a yellow background and reduce only the promotion/GOLD border radii by about 4–5%, preserving their brightness and motion.
- Open contextual achievement/history tabs from the lobby, add one authenticated Survival/Solo/All history API and ignore cancelled or stale history replies. Rename Korean Shared tabs and title prefixes to 공통 without changing IDs, unlocks or AP.
- Keep Survival matchmaking blocked at M1. This UI update adds no database migration or Apps Script deployment; client and server exact versions must be upgraded together. See `../ranked/docs/survival-m1-ui-beta60.md`.

## v0.5.0-beta.59

- Added the integrated Survival / Solo Ranked lobby after version, cached resource and environment checks, with reversible eased card/menu actions and protected input ownership.
- Added independent Survival profiles, LP/tier rules, leaderboard tabs and additive Survival Config; disabled modes retain read access and stop new admissions while Solo games finish using their snapshots.
- Split achievements into Solo Ranked, Survival and Shared, retaining existing IDs/AP/Claims and sharing GOLD and one equipped title across modes.
- Survival matches remain unavailable in M1. See `../ranked/docs/survival-m1.md` for deployment, validation and M3/M4/M5 extension points.

## v0.5.0-beta.50

- Normalize Admin text-input strings before JSON serialization so Android32/64 builds accept their separate gd::string ABI. Exercise both string types in the native popup regression fixture.
- Make the result bar's rounded ends visible, move tier names beside its ends and place endpoint badges closer to the moving LP. Keep lower-tier track colors opaque and wipe a changed track color smoothly from left to right.
- Skip/Esc first settles the final LP, assembled icons and all pending notices. The button becomes Continue; the next press retires the overlay after native input dispatch. Stamp demotion warnings and include the missing Korean promotion glyphs.
- Move streak/promotion/demotion state into one status row above the lobby emblem and player cards, with YOUR RANK when empty. Enlarge the lobby emblem, move its halo with it, show SILVER/GOLD halos on both player cards and INFO, and append the standings population.
- Keep Settings within the viewport and separate the language buttons. Scroll up to the most recent 30 competitive matches. Consolidate lobby debug entries into a three-button Debug menu.
- Add server-checked hwanhee1/debug-password Admin actions for batch achievement unlock, LP/tier, MMR or both, bot deletion, nickname history reset and confirmed DB reset. Keep metrics/Claim semantics, shared opponent records and operational configuration intact.
- Offer an explicit red-button session takeover, revoke both old session and match credentials and recover the active assignment. Recheck queue admission inside its transaction and scope late queue updates/Close to their original session.
- Synchronize beta50 client/server exact gate, workspaces and lockfile. Image pack inputs are unchanged; no manual Release. See [BETA50-UI-ADMIN-SESSIONS.md](BETA50-UI-ADMIN-SESSIONS.md) for behavior and validation limits.

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
