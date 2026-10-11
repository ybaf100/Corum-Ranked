-- Optional presentation marker; scoring and protocol v2 remain unchanged.
ALTER TABLE ranked_deathmatch_attempts
  ADD COLUMN IF NOT EXISTS native_death BOOLEAN NOT NULL DEFAULT FALSE;
