BEGIN;

ALTER TABLE ranked_placement_maps
    ADD COLUMN IF NOT EXISTS prepare_started_at TIMESTAMPTZ,
    ADD COLUMN IF NOT EXISTS prepare_deadline_at TIMESTAMPTZ,
    ADD COLUMN IF NOT EXISTS ready_at TIMESTAMPTZ;

ALTER TABLE ranked_placement_maps
    DROP CONSTRAINT IF EXISTS ranked_placement_maps_clear_count_check,
    DROP CONSTRAINT IF EXISTS ranked_placement_maps_map_score_check,
    DROP CONSTRAINT IF EXISTS ranked_placement_maps_clear_count_beta9_check,
    DROP CONSTRAINT IF EXISTS ranked_placement_maps_map_score_beta9_check;

ALTER TABLE ranked_placement_maps
    ADD CONSTRAINT ranked_placement_maps_clear_count_beta9_check
        CHECK (clear_count BETWEEN 0 AND 2),
    ADD CONSTRAINT ranked_placement_maps_map_score_beta9_check
        CHECK (map_score BETWEEN 0 AND 500);

ALTER TABLE ranked_placement_runs
    ADD COLUMN IF NOT EXISTS completion_kind TEXT,
    ADD COLUMN IF NOT EXISTS skipped_at TIMESTAMPTZ;

ALTER TABLE ranked_placement_runs
    DROP CONSTRAINT IF EXISTS ranked_placement_runs_completion_kind_check;

ALTER TABLE ranked_placement_runs
    ADD CONSTRAINT ranked_placement_runs_completion_kind_check
        CHECK (completion_kind IS NULL OR completion_kind IN ('COMPLETED', 'SKIPPED'));

COMMIT;
