BEGIN;
-- A player's history reset must not erase the other participant's records.
ALTER TABLE ranked_profiles ADD COLUMN IF NOT EXISTS history_visible_since TIMESTAMPTZ NOT NULL DEFAULT '-infinity';
COMMIT;
