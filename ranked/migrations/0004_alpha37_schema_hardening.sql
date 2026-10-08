BEGIN;

-- 0001 historically created match_type with DEFAULT 'PVP', while the later
-- DEBUG_BOT migration changed the accepted production value to 'RANKED_PVP'.
-- Keep the default aligned with the current constraint for every future insert,
-- even if a call site ever omits match_type explicitly.
ALTER TABLE ranked_matches
    ALTER COLUMN match_type SET DEFAULT 'RANKED_PVP';

COMMIT;
