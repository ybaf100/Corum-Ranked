-- Preserve legacy logical FINAL rows and every immutable award. New awards use
-- the actual source round; this also fences idempotent historical backfill.
CREATE UNIQUE INDEX IF NOT EXISTS sur_round_badges_source_result
  ON sur_round_badges(match_id,participant_id,source_round_key);
