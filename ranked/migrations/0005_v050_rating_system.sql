BEGIN;

ALTER TABLE ranked_profiles
  ADD COLUMN IF NOT EXISTS result_streak INTEGER NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS placement_results JSONB NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN IF NOT EXISTS promotion_source_tier TEXT,
  ADD COLUMN IF NOT EXISTS promotion_target_tier TEXT,
  ADD COLUMN IF NOT EXISTS promotion_cutoff INTEGER,
  ADD COLUMN IF NOT EXISTS promotion_games_required SMALLINT,
  ADD COLUMN IF NOT EXISTS promotion_wins_required SMALLINT,
  ADD COLUMN IF NOT EXISTS promotion_results JSONB NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN IF NOT EXISTS demotion_warning BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE ranked_matches
  ADD COLUMN IF NOT EXISTS ranked_score_a_before INTEGER,
  ADD COLUMN IF NOT EXISTS ranked_score_b_before INTEGER,
  ADD COLUMN IF NOT EXISTS ranked_score_delta_a INTEGER,
  ADD COLUMN IF NOT EXISTS ranked_score_delta_b INTEGER,
  ADD COLUMN IF NOT EXISTS ranked_score_a_after INTEGER,
  ADD COLUMN IF NOT EXISTS ranked_score_b_after INTEGER,
  ADD COLUMN IF NOT EXISTS rating_presentation JSONB;

-- Placement LP was previously mirrored from MMR. v0.5 keeps it at zero and hidden.
UPDATE ranked_profiles
SET visible_ranked_score = 0
WHERE displayed_tier = 'UNRANKED'
  AND visible_ranked_score IS DISTINCT FROM 0;

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ranked_profile_result_streak_range') THEN
    ALTER TABLE ranked_profiles
      ADD CONSTRAINT ranked_profile_result_streak_range
      CHECK (result_streak BETWEEN -1000 AND 1000);
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ranked_profile_placement_results_array') THEN
    ALTER TABLE ranked_profiles
      ADD CONSTRAINT ranked_profile_placement_results_array
      CHECK (jsonb_typeof(placement_results) = 'array');
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ranked_profile_promotion_tiers') THEN
    ALTER TABLE ranked_profiles
      ADD CONSTRAINT ranked_profile_promotion_tiers CHECK (
        (promotion_source_tier IS NULL AND promotion_target_tier IS NULL AND
         promotion_cutoff IS NULL AND promotion_games_required IS NULL AND
         promotion_wins_required IS NULL AND promotion_results = '[]'::jsonb)
        OR
        (promotion_source_tier IN ('RED', 'AQUA', 'BRONZE', 'SILVER') AND
         promotion_target_tier IN ('AQUA', 'BRONZE', 'SILVER', 'GOLD') AND
         promotion_cutoff IS NOT NULL AND promotion_games_required > 0 AND
         promotion_wins_required > 0 AND promotion_wins_required <= promotion_games_required AND
         jsonb_typeof(promotion_results) = 'array')
      );
  END IF;
END $$;

-- 0003 used SELECT * for this convenience view, which would expose hidden MMR
-- columns to any read-only/public database consumer. v0.5 deliberately lists
-- only match and visible-LP presentation fields.
DROP VIEW IF EXISTS ranked_public_match_history;
CREATE VIEW ranked_public_match_history AS
SELECT
  id,
  player_a_id,
  player_b_id,
  effective_tier,
  series_state,
  state,
  winner_id,
  rules_version,
  match_type,
  cancellation_reason,
  ranked_score_a_before,
  ranked_score_b_before,
  ranked_score_delta_a,
  ranked_score_delta_b,
  ranked_score_a_after,
  ranked_score_b_after,
  rating_presentation,
  created_at,
  started_at,
  finished_at
FROM ranked_matches;

COMMIT;
