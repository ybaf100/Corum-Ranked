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
