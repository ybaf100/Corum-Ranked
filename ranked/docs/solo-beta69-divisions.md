# Solo divisions — beta69

Solo configuration uses RED3/RED2/RED1, AQUA3/2/1, BRONZE3/2/1, SILVER3/2/1, GOLD. The UI displays Roman divisions. Every row owns its LP interval, Main Pool and Deathmatch Pool. Only grade I rows configure the series to the next base tier. Grade III is the base-tier demotion-protection floor; internal boundaries have no series or protection.

Deploy the updated `RankedConfig.gs` and `SurvivalConfig.gs`, then run `setupCorumRankedConfig`. A valid legacy five-row sheet is copied to `Ranked Tiers before beta69` before being expanded into thirds. Existing base cuts, pools and series settings are retained. Custom thirteen-row settings are never recalculated. The server also accepts old five-row responses while the Apps Script deployment catches up. Survival retains five display tiers and uses RED3/AQUA3/BRONZE3/SILVER3/GOLD minima. Existing matches retain their captured configuration.

GOLD population includes eligible bots. Up to three players are CHAMPION. For populations 4–6 the remainder are GOLD. Above six, the upper half (rounded up) includes the three Champions; remaining upper-half slots are MASTER. Ties never expand slots. Order is LP, series status/wins, warning, win rate, MMR, normalized/name spelling, account ID. Titles are computed on read, not persisted as LP bands. No extra LP, series or protection is attached to title changes.

`GET /api/ranked/leaderboard/tiers` supplies the live guide: division bands, pools, series/protection rules, exact Gold slots and LP cutoffs. The upper-left GOLD badge opens it. Result standings capture before/after titles and cutoffs; GOLD targets MASTER, or CHAMPION when MASTER is absent. MASTER targets CHAMPION; CHAMPION uses the next rank. A guaranteed LP target is cutoff + 1 because tied slots depend on ladder order.

Only GOLD-family upward transitions use the full promotion animation. Lower upward transitions show the promotion notice. Division demotions show a notice; base-tier demotions retain the existing animation. RED/AQUA/BRONZE assets remain unchanged. SILVER III has no persistent halo, II has its circular halo, I has full halo effects. GOLD-family badges share full existing GOLD assets.

Migration `0025_solo_tier_divisions.sql` widens only Solo constraints; startup applies it through the existing migration runner. It does not reset data. The explicitly requested one-time reset uses `ranked/tools/reset-solo-beta69.sql` after a Neon backup and active-match check. It clears Solo LP/MMR, Placement, series and history while retaining account identities, Survival and shared AP/achievements/titles. Do not schedule or auto-run the reset.

Validation covers numbered boundaries, pool selection, series/protection, placement exclusion, exact Gold slots/ties/bots, frozen result targets, native presentation and migration compatibility. Hardware rendering and device gameplay remain user-side checks.

The authorized production reset completed on 2026-10-14 for two Solo profiles. Zero Solo matches remained afterward. All Survival tables and shared achievement/AP/title data were verified unchanged within the reset transaction. Backup branch: `backup-before-solo-beta69-20261014` (`br-super-sky-azfhfl44`).
