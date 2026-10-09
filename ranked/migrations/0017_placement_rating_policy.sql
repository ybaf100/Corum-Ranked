-- Existing runs retain their start-time scoring policy. New service inserts
-- explicitly opt in to NORMALIZED_V1; no existing profile LP/MMR is rewritten.
ALTER TABLE ranked_placement_runs
  ADD COLUMN IF NOT EXISTS rating_policy_version TEXT NOT NULL DEFAULT 'LEGACY_CAP_V1';

ALTER TABLE ranked_placement_maps
  ADD COLUMN IF NOT EXISTS skip_client_event_id TEXT,
  ADD COLUMN IF NOT EXISTS skipped_at TIMESTAMPTZ;

CREATE UNIQUE INDEX IF NOT EXISTS ranked_placement_map_skip_event_unique
  ON ranked_placement_maps (run_id, skip_client_event_id)
  WHERE skip_client_event_id IS NOT NULL;
