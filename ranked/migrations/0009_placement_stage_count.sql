BEGIN;

ALTER TABLE ranked_placement_runs
    ADD COLUMN IF NOT EXISTS stage_count SMALLINT NOT NULL DEFAULT 5;

ALTER TABLE ranked_placement_runs
    DROP CONSTRAINT IF EXISTS ranked_placement_runs_stage_count_check;

ALTER TABLE ranked_placement_runs
    ADD CONSTRAINT ranked_placement_runs_stage_count_check
    CHECK (stage_count IN (3, 5));

COMMIT;
