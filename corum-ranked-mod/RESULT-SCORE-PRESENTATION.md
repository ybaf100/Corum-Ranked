# Result score presentation — beta48

The new overlay is presentation only. MatchService still calculates LP/tier outcomes with the bound match config. Its stored rating_presentation.scorePresentation contains the bound five tier minimums, both participants' before/after standings and the final promotion-series marks.

## Display rules

- Ordinary LP interpolation takes 2 seconds. The complete promotion/demotion LP change takes 4 seconds. Exact server values are used: 299 → 350 displays +51 LP.
- Placement stages and the final Placed reveal retain the existing flow. Cancelled/legacy results without a valid score snapshot use their existing result view.
- Promotion/demotion rank motions finish or Skip/Esc into the score overlay. Opening the next keypad delegate and restoring result input are queued separately; one Esc cannot dismiss both steps. The settled result is already built before the one-time blur capture.
- Final score and notices remain until Skip/Esc removes the overlay. Skipping during interpolation returns to the authoritative final result. Match identity prevents replay on polling/re-render. Achievement toasts wait for both foreground layers.
- Current tier controls LP text and foreground bar color. The track keeps the previous tier color, with a gray RED track. Promotion replaces both icons through shrink/part assembly; demotion makes old parts fall and grows replacements in place.
- WIN/LOSE and an ongoing promotion series appear at entry. A newly started series or demotion warning appears after interpolation. The server's gamesRequired controls the gray square count, with wrapping for longer series; old marks remain and new marks stamp. A zero delta is gray.

## Standings and GOLD

GET /api/ranked/session/standing uses the existing SessionGuard, including exact client version and live-session admission/presence gates. Lobby-only requests are serialized, refreshed at most every 30 seconds and cancelled/invalidated when the session is retired. Result standings come from the saved match snapshot, never current lobby polling.

All placed profiles with non-null LP count, including every historical bot profile. Rank is LP-only competition rank: 30,25,25,20 produces #1,#2,#2,#4. Top % = rank / total × 100. Unplaced profiles are excluded and display #— / Top —%. The lobby reserves a common slot above the entire rotating GOLD ornament footprint; GOLD additionally shows LP / #rank below its badge.

GOLD joint #1 has a full bar and its own GD icon. Otherwise the goal is the nearest strictly higher LP, with that group's highest-MMR player's icon. Equal-MMR ties are randomly selected once in the stored result; before/after phases share the same seed. Bots use the existing default cube, not the CoJong information reference. MMR and internal player/session IDs are not returned. GOLD uses the result goal throughout interpolation, so the last frame cannot jump to a newly polled target.

## Other UI

Settings is visible only in the lobby (including queue waiting), Achievements and History list/detail. Opponent INFO is at the card's upper right; own INFO stays left. Jukebox always occupies the LDM / Disable Shake row. When inactive it remains clickable and gray, showing the localized requirement notice. Active Jukebox opens directly; owned notices/dialogs are retired on gameplay entry and scene exit.

Existing image resources are reused. The image pack version/content is unchanged; the established workflow packages a pack only when its image inputs change. Font charsets add the required Korean copy and em dash, while the separate countdown digits remain unchanged. Manifest, exact server gate, workspaces and lockfile are beta48.

## Verification limits

The executable ResultScoreNativeTest.mjs compiles the real score layer, domain model, decoders, request callback and result handoff callbacks. Renderer, Cocos actions and GD/HTTP interfaces are boundary doubles. It checks timing/cutoffs, final-state hold, early/repeated Skip/Esc, retirement ordering, menu re-entry, Placement exclusion, fixed GOLD/bot targets, stale sessions, screen policy and Jukebox ownership. Existing gameplay/Pause/spectator/preparation regression suites remain enabled.

PostgreSQL-compatible integration tests exercise the actual standings SQL, protected HTTP route and committed match result retrieval, including all bots, ties, Top %, target selection, config changes and repeated polls. Local desktop OpenGL and GLES2 compile/link checks passed for the actual blur shaders; the two-pass Gaussian was also rendered on Mesa llvmpipe. These checks do not establish a GD device render or Windows/iOS/Android package build. Per the requested workflow, main is pushed without waiting for or inspecting Actions results; no manual Release is run.
