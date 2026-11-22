-- Designated-map matches (debug): the round maps in order and an optional
-- Death Match map; NULL for every ordinary match.
ALTER TABLE ranked_matches
    ADD COLUMN IF NOT EXISTS fixed_maps JSONB NULL;
