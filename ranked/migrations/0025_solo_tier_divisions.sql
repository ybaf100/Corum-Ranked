BEGIN;

-- This migration changes allowed Solo tier IDs only. It never resets data.
ALTER TABLE ranked_profiles DROP CONSTRAINT IF EXISTS ranked_profiles_displayed_tier_check;
ALTER TABLE ranked_profiles ADD CONSTRAINT ranked_profiles_displayed_tier_check CHECK (displayed_tier IN
 ('UNRANKED','RED','AQUA','BRONZE','SILVER','GOLD','RED3','RED2','RED1','AQUA3','AQUA2','AQUA1','BRONZE3','BRONZE2','BRONZE1','SILVER3','SILVER2','SILVER1'));
ALTER TABLE ranked_matches DROP CONSTRAINT IF EXISTS ranked_matches_effective_tier_check;
ALTER TABLE ranked_matches ADD CONSTRAINT ranked_matches_effective_tier_check CHECK (effective_tier IN
 ('RED','AQUA','BRONZE','SILVER','GOLD','RED3','RED2','RED1','AQUA3','AQUA2','AQUA1','BRONZE3','BRONZE2','BRONZE1','SILVER3','SILVER2','SILVER1'));
ALTER TABLE ranked_placement_runs DROP CONSTRAINT IF EXISTS ranked_placement_runs_final_displayed_tier_check;
ALTER TABLE ranked_placement_runs ADD CONSTRAINT ranked_placement_runs_final_displayed_tier_check CHECK (final_displayed_tier IS NULL OR final_displayed_tier IN
 ('UNRANKED','RED','AQUA','BRONZE','SILVER','GOLD','RED3','RED2','RED1','AQUA3','AQUA2','AQUA1','BRONZE3','BRONZE2','BRONZE1','SILVER3','SILVER2','SILVER1'));

ALTER TABLE ranked_profiles DROP CONSTRAINT IF EXISTS ranked_profile_promotion_tiers;
ALTER TABLE ranked_profiles ADD CONSTRAINT ranked_profile_promotion_tiers CHECK (
 (promotion_source_tier IS NULL AND promotion_target_tier IS NULL AND promotion_cutoff IS NULL
  AND promotion_games_required IS NULL AND promotion_wins_required IS NULL AND promotion_results='[]'::jsonb)
 OR
 (promotion_source_tier IN ('RED','AQUA','BRONZE','SILVER','RED1','AQUA1','BRONZE1','SILVER1')
  AND promotion_target_tier IN ('AQUA','BRONZE','SILVER','GOLD','AQUA3','BRONZE3','SILVER3')
  AND promotion_cutoff IS NOT NULL AND promotion_games_required>0
  AND promotion_wins_required>0 AND promotion_wins_required<=promotion_games_required
  AND jsonb_typeof(promotion_results)='array')
);

COMMIT;
