-- Debug builds only: recent client log files sent from the main menu.
-- Each file arrives in text chunks (request bodies stay under 100 KB).
CREATE TABLE IF NOT EXISTS ranked_debug_log_chunks (
    upload_id UUID NOT NULL,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    client_version TEXT NOT NULL,
    file_index INTEGER NOT NULL CHECK (file_index BETWEEN 0 AND 2),
    file_name TEXT NOT NULL,
    part INTEGER NOT NULL CHECK (part >= 0),
    total_parts INTEGER NOT NULL CHECK (total_parts BETWEEN 1 AND 40),
    content TEXT NOT NULL,
    received_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (upload_id, file_index, part)
);
CREATE INDEX IF NOT EXISTS ranked_debug_log_chunks_player_idx
    ON ranked_debug_log_chunks(player_id, received_at DESC);
