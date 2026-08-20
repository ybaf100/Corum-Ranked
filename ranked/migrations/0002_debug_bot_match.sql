BEGIN;

-- Historical alpha.2 behavior. Migration 0003 replaces the history view so
-- DEBUG_BOT results participate in normal Ranked history/rating semantics.

ALTER TABLE ranked_matches
    ADD COLUMN IF NOT EXISTS match_type TEXT NOT NULL DEFAULT 'RANKED_PVP',
    ADD COLUMN IF NOT EXISTS debug_config JSONB,
    ADD COLUMN IF NOT EXISTS debug_discord_events BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE ranked_matches
    DROP CONSTRAINT IF EXISTS ranked_match_type_check,
    DROP CONSTRAINT IF EXISTS ranked_match_debug_shape;

ALTER TABLE ranked_matches
    ADD CONSTRAINT ranked_match_type_check
        CHECK (match_type IN ('RANKED_PVP', 'DEBUG_BOT')),
    ADD CONSTRAINT ranked_match_debug_shape CHECK (
        (match_type = 'RANKED_PVP' AND debug_config IS NULL AND debug_discord_events = FALSE)
        OR
        (match_type = 'DEBUG_BOT' AND debug_config IS NOT NULL)
    );

CREATE INDEX IF NOT EXISTS ranked_matches_type_state_idx
    ON ranked_matches(match_type, state)
    WHERE finished_at IS NULL;

CREATE OR REPLACE VIEW ranked_public_match_history AS
SELECT *
FROM ranked_matches
WHERE match_type = 'RANKED_PVP';

COMMIT;
