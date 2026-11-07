-- Solo map pool statistics read settled rounds and Death Matches per map.
-- Indexes only; no rows are changed.
CREATE INDEX IF NOT EXISTS ranked_rounds_canonical_level_idx
    ON ranked_rounds(canonical_level_id) WHERE result IS NOT NULL;
CREATE INDEX IF NOT EXISTS ranked_deathmatches_canonical_level_idx
    ON ranked_deathmatches((map_snapshot->>'canonicalLevelId')) WHERE winner_id IS NOT NULL;
