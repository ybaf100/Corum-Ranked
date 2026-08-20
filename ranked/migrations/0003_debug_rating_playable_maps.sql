BEGIN;

ALTER TABLE ranked_rounds
    ADD COLUMN IF NOT EXISTS alternate_level_id TEXT,
    ADD COLUMN IF NOT EXISTS playable_level_id TEXT;

UPDATE ranked_rounds
SET playable_level_id = level_id
WHERE playable_level_id IS NULL;

ALTER TABLE ranked_rounds
    ALTER COLUMN playable_level_id SET NOT NULL;

ALTER TABLE ranked_attempts
    ADD COLUMN IF NOT EXISTS played_level_id TEXT;

UPDATE ranked_attempts attempt
SET played_level_id = round.playable_level_id
FROM ranked_rounds round
WHERE attempt.round_id = round.id
  AND attempt.played_level_id IS NULL;

ALTER TABLE ranked_attempts
    ALTER COLUMN played_level_id SET NOT NULL;

ALTER TABLE ranked_deathmatch_attempts
    ADD COLUMN IF NOT EXISTS played_level_id TEXT;

UPDATE ranked_deathmatch_attempts attempt
SET played_level_id = COALESCE(
    NULLIF(deathmatch.map_snapshot ->> 'playableLevelId', ''),
    NULLIF(deathmatch.map_snapshot ->> 'levelId', ''),
    deathmatch.map_snapshot ->> 'canonicalLevelId'
)
FROM ranked_deathmatches deathmatch
WHERE attempt.deathmatch_id = deathmatch.id
  AND attempt.played_level_id IS NULL;

ALTER TABLE ranked_deathmatch_attempts
    ALTER COLUMN played_level_id SET NOT NULL;

CREATE OR REPLACE VIEW ranked_public_match_history AS
SELECT *
FROM ranked_matches;

CREATE OR REPLACE VIEW ranked_leaderboard AS
SELECT
    p.gd_account_id,
    p.gd_username,
    rp.visible_ranked_score,
    rp.displayed_tier,
    rp.placement_games,
    rp.wins,
    rp.losses,
    rp.match_draws,
    rp.updated_at
FROM ranked_profiles rp
JOIN ranked_players p ON p.id = rp.player_id
WHERE rp.visible_ranked_score IS NOT NULL
  AND rp.displayed_tier <> 'UNRANKED'
  AND p.gd_account_id > 0;

COMMIT;
