BEGIN;

ALTER TABLE sur_profiles ADD COLUMN IF NOT EXISTS history_visible_since TIMESTAMPTZ NOT NULL DEFAULT '-infinity';
ALTER TABLE sur_match_players ADD COLUMN IF NOT EXISTS result_lp_delta INTEGER;

-- Bots remain match participants: no ranked_players/sur_profiles are created.
CREATE TABLE IF NOT EXISTS sur_debug_matches (
    match_id UUID PRIMARY KEY REFERENCES sur_matches(id) ON DELETE CASCADE,
    human_player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    admission_session_id UUID NOT NULL REFERENCES ranked_sessions(id),
    seed BIGINT NOT NULL CHECK (seed BETWEEN 0 AND 4294967295),
    average_group TEXT NOT NULL CHECK (average_group IN ('RED_BRONZE','SILVER','GOLD')),
    virtual_human_lp BOOLEAN NOT NULL DEFAULT FALSE,
    human_actual_lp INTEGER NOT NULL CHECK (human_actual_lp >= 0),
    driver_state JSONB NOT NULL DEFAULT '{}'::jsonb,
    force_rank INTEGER CHECK (force_rank BETWEEN 1 AND 16),
    last_seen_at TIMESTAMPTZ NOT NULL,
    result JSONB,
    settled_at TIMESTAMPTZ,
    acknowledged_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL,
    CHECK ((result IS NULL) = (settled_at IS NULL))
);
CREATE INDEX IF NOT EXISTS sur_debug_pending_idx ON sur_debug_matches(match_id) WHERE settled_at IS NULL;
CREATE INDEX IF NOT EXISTS sur_debug_player_idx ON sur_debug_matches(human_player_id,created_at DESC);

COMMIT;
