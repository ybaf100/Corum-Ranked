# Elimination effects: season-scoped tier unlocks (deferred to season settlement)

Status: open. Decided 2026-11-08: keep the current judgment until the season
settlement system exists; only the player-facing note ships now.

## What players are told (beta.175)

The elimination effect picker shows: "Once unlocked, usable for the season.
(Achievement grade unlocks are not affected.)" / "한번 해금시, 시즌동안 사용
가능합니다. (도전과제 등급 해금은 예외)".

## What the server does today

`ranked/apps/server/src/cosmetics/elimination-effect.service.ts` derives every
unlock on demand, with no per-season record:

| Effect  | Requirement                         | Current source                                          |
|---------|-------------------------------------|---------------------------------------------------------|
| BOLT    | Survival SILVER                     | `sur_lp_ledger.tier_after` (all time), current Survival tier, `survival_gold_reached` |
| SLICE   | GOLD in both Survival and Solo Ranked | as above + `ranked_profiles.displayed_tier`, `ranked_achievement_metrics.solo_gold_reached` (all time) |
| SHATTER | Achievement grade BRONZE (1,000 AP) | claimed AP (`ranked_profiles.total_ap`) - permanent by design |
| VORTEX  | Achievement grade GOLD (3,200 AP)   | claimed AP - permanent by design                        |

So a tier unlock currently survives a season change. That is more generous
than the note; it must become season-scoped when settlement lands.

## To do with the season settlement system

1. Seasons need a start instant. `ranked_seasons` (0040) only stores
   `mode, preseason, season_number, target_at`; record a `started_at` (or a
   season history table) when the admin advances `season_number`.
2. Persist tier unlocks per season, e.g.
   `ranked_cosmetic_unlocks(player_id, effect, season_key, unlocked_at)` with
   `season_key` = the Survival/Solo season numbers that the requirement reads.
   Write the row at settlement time and whenever a qualifying tier is reached
   during the season (Survival LP settlement, Solo match/placement result), so a
   peak followed by a demotion still counts for that season.
3. `eliminationEffectUnlocked` (rules `elimination-effects.ts`) keeps the grade
   requirements as they are; the tier requirements read the current season's
   unlock rows instead of all-time peaks.
4. At season rollover, an equipped tier effect that is no longer unlocked
   falls back to DEFAULT (the service already plays a locked stored value as
   DEFAULT; admission snapshots `ranked_profiles.elimination_effect` as is, so
   normalize it there too or clear it during settlement).
5. Decide whether the previous season's final tier should grant the effect for
   the next season (a common "season reward" pattern) - not decided yet.
6. Tests: integration test across a season change (unlocked -> locked unless
   re-reached; grade unlocks unaffected), plus the admission snapshot fallback.
