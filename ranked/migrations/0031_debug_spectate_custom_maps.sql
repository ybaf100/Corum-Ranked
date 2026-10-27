-- Expand only the isolated Debug Spectate fixture. Existing rows and all Ranked data remain intact.
BEGIN;
ALTER TABLE debug_spectate_recordings DROP CONSTRAINT IF EXISTS debug_spectate_recordings_level_id_check;
ALTER TABLE debug_spectate_recordings ADD CONSTRAINT debug_spectate_recordings_level_id_check
  CHECK (level_id BETWEEN 1 AND 2147483647);
CREATE INDEX IF NOT EXISTS debug_spectate_latest_finalized
  ON debug_spectate_recordings (finalized_at DESC, created_at DESC, id DESC) WHERE completed;
COMMIT;
