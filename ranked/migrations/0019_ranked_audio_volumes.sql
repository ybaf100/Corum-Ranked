-- Operational music settings survive player/bot/database resets.
-- No player foreign key: audit identity is retained after a player is deleted.
CREATE TABLE IF NOT EXISTS ranked_audio_volumes (
  song_id integer PRIMARY KEY CHECK (song_id > 0),
  volume_percent integer NOT NULL CHECK (volume_percent BETWEEN 0 AND 100),
  updated_by_account_id bigint NOT NULL,
  updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);
