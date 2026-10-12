# Survival beta65 UI and round flow

Based on main `979ddbd8720aa8e17feaff568157328acccc94c4`. This patch retains the working GD relay and existing Survival scoring/LP rules.

## Presentation sequence

1. New debug matches show all 16 participants before searching for Round 1's map.
2. The server selects and verifies the map using the existing provider. The client downloads and validates that same ID and prepares its audio.
3. Survival displays the shared Solo Placement map heading, native difficulty badge, duration, creator/ID and readiness information. LDM, Disable Shake and Jukebox remain available. Ready maps enter automatically after a short information display; Re-enter is still limited to a previously entered playable round.
4. After the authoritative round ends, the client returns to the completed round's full participant roster. Eliminated tiles turn red and their icons fly away.
5. After that presentation finishes, the client waits two seconds and releases the next-round barrier. Only then does the server create/select the next round. The same sequence applies to cutoff ties and repeated Death Matches. Terminal results play result music and show the existing tier/LP motion.

New debug snapshots set `presentationControlled=true` and `presentationGate="INTRO"`. The existing JSON snapshot in `sur_matches` persists these fields; no migration is needed. An authenticated debug event `PRESENTATION_COMPLETE` supplies `eventId`, `roundKey` and `mapGeneration`. It cannot supply a rank, score, map ID or LP reward. INTRO acknowledgements enable map selection; ROUND_RESULT acknowledgements require the same active human and at least 3,050 ms since round completion (1,050 ms presentation + 2,000 ms wait). Command receipts and match locks preserve replay safety, including after restart.

The gate does not pause or extend a PLAYING round. Preparation and play timestamps are not started while the initial/result roster is pending. Completed round records are retained for recovery. Pre-beta65 matches without presentation control and ordinary M3 engine adapters keep their previous behavior. This endpoint remains debug-only; public matchmaking is still M5 scope.

## Native client reuse and controls

`ui/RankedHudLabel.hpp` supplies the same Corum bitmap font scaling and black shadow used by Solo's PlayLayer. Survival uses it for the round, clock, score difference, rank and leaderboard columns. It keeps the existing RenderFpsMeter and FPS node. Rank rows are retained by participant ID; server scores decide ranks while displayed scores interpolate independently.

`ui/RankedMapHeading.hpp` is shared by Solo Placement and Survival. Solo's authored title/badge/duration positions remain unchanged. Survival reads native level metadata through `RankedMapDetails`; its selected-map card is shown only for the current ID/context.

Native PauseLayer ownership now includes Survival. Survival uses its own server deadline, skips Solo pause leases and retains normal native Resume/Quit teardown. A paused round that ends returns once to the coordinator; resuming gameplay restores the Survival heartbeat and reads the remaining server time. Both the gameplay surrender button and pause-menu surrender action use `RankedConfirmPopup`, with Korean text and current-match/context checks. Cancelling keeps the native pause menu available. Quitting without surrender preserves the recoverable match.

Force n stays in the participant matching screen and is absent from PlayLayer. Chooser cards give a single 0.76-second clockwise shine on touch/click, independently of hover input being disabled for channel motion. Rank cards inside a channel still have no hover/press feedback.

The retained Solo lobby yields audio ownership while the Survival coordinator is the running scene. On return from native gameplay the coordinator requests FMOD output recovery, then chooses the existing Solo round wait or result resource. Final/Death Match retain Round 3 wait music. Result win/loss music still follows actual LP increase versus unchanged/decreased LP.

## Operation and validation

Client, server admission and workspace packages are `v0.5.0-beta.65`. Use the existing R2 publication setup and GD relay; no new secret, DB migration or Apps Script setup is required.

Local validation covers TypeScript source/test type checks, rules, persistent Survival debug/engine/LP integration, native callback/domain harnesses, Korean glyph coverage and release policy scripts. These are source-level checks with native API doubles, not a Geode/platform build or device test. Actual Windows/mobile layout, animation, pause/resume and audible music playback must be checked on the user's devices. GitHub Actions build/device/package results are not awaited for this patch.

Verified locally: TypeScript type checks; all 194 rules tests; 88 selected Survival/admission/environment tests; Solo match and Death Match flow integration; Placement/achievement/environment checks; the Debug policy job's ten strict C++ domain programs; all 170 existing native checks (the ten initially failing fixture/glyph checks passed on targeted rerun); two new shared-font/tap-shine native checks; and 16 release/asset policy checks. After correcting terminal presentation cleanup, the 58 affected rules tests and all 16 Survival debug integration tests were rerun successfully. No complete server regression suite or platform client build was run.
