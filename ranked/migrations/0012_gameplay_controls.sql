-- Budgets belong to the match, never to a round, and survive server restart.
BEGIN;
ALTER TABLE ranked_matches ADD COLUMN IF NOT EXISTS pause_state jsonb NOT NULL DEFAULT '{}'::jsonb;
ALTER TABLE ranked_matches ADD COLUMN IF NOT EXISTS surrender_events jsonb NOT NULL DEFAULT '[]'::jsonb;
CREATE TABLE IF NOT EXISTS ranked_runtime_features (
  feature text PRIMARY KEY,
  enabled boolean NOT NULL,
  revision bigint NOT NULL DEFAULT 1
);
INSERT INTO ranked_runtime_features (feature, enabled) VALUES ('live_spectator', true)
ON CONFLICT (feature) DO NOTHING;
COMMIT;
