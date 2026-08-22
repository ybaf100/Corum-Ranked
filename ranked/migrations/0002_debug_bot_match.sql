BEGIN;

-- Historical alpha.2 behavior. Migration 0003 replaces the history view so
-- DEBUG_BOT results participate in normal Ranked history/rating semantics.
--
-- alpha.37 hardening: older versions of 0001 used match_type='PVP' plus the
-- legacy debug_bot_config / discord_events_enabled columns. Normalize those
-- rows before installing the newer constraint so an old Neon database can be
-- migrated automatically instead of failing at server startup.

ALTER TABLE ranked_matches
    ADD COLUMN IF NOT EXISTS match_type TEXT NOT NULL DEFAULT 'RANKED_PVP',
    ADD COLUMN IF NOT EXISTS debug_config JSONB,
    ADD COLUMN IF NOT EXISTS debug_discord_events BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE ranked_matches
    DROP CONSTRAINT IF EXISTS ranked_match_type_check,
    DROP CONSTRAINT IF EXISTS ranked_match_debug_shape;

UPDATE ranked_matches
SET match_type = 'RANKED_PVP'
WHERE match_type = 'PVP';

-- Current/legacy 0001 schemas retain these source columns. Keep old Debug Bot
-- history usable when the normalized alpha.2 columns are first introduced.
UPDATE ranked_matches
SET
    debug_discord_events = CASE
        WHEN debug_config IS NULL THEN COALESCE(
            (to_jsonb(ranked_matches) ->> 'discord_events_enabled')::BOOLEAN,
            debug_discord_events
        )
        ELSE debug_discord_events
    END,
    debug_config = COALESCE(debug_config, to_jsonb(ranked_matches) -> 'debug_bot_config')
WHERE match_type = 'DEBUG_BOT';

UPDATE ranked_matches
SET debug_config = NULL,
    debug_discord_events = FALSE
WHERE match_type = 'RANKED_PVP';

ALTER TABLE ranked_matches
    ALTER COLUMN match_type SET DEFAULT 'RANKED_PVP',
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
