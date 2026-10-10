-- Presentation cache, independent of player scores and in-progress map snapshots.
CREATE TABLE IF NOT EXISTS ranked_map_metadata_cache (
  cache_key text PRIMARY KEY,
  data jsonb NOT NULL,
  fetched_at timestamptz NOT NULL
);
