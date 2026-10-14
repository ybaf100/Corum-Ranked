BEGIN;
CREATE TABLE IF NOT EXISTS sur_map_selection_history (
 id BIGSERIAL PRIMARY KEY,
 match_id UUID REFERENCES sur_matches(id) ON DELETE SET NULL,
 round_key TEXT NOT NULL,
 map_generation INTEGER NOT NULL,
 level_id BIGINT NOT NULL CHECK(level_id>=40000000),
 history_window INTEGER NOT NULL CHECK(history_window IN(0,10,20,30)),
 selected_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
 UNIQUE(match_id,round_key,map_generation)
);
CREATE INDEX IF NOT EXISTS sur_map_selection_history_recent ON sur_map_selection_history(id DESC);
ALTER TABLE sur_queue_fill_requests ADD COLUMN IF NOT EXISTS bot_count SMALLINT CHECK(bot_count BETWEEN 0 AND 15);
COMMIT;
