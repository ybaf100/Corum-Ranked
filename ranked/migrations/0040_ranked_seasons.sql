-- Season label and time per mode, set from the in-game Admin.
-- target_at is the season end while the mode is enabled, or the mode's start
-- time while Ranked config keeps the mode disabled. NULL hides the timer.
CREATE TABLE IF NOT EXISTS ranked_seasons (
    mode TEXT PRIMARY KEY CHECK (mode IN ('SOLO_RANKED', 'SURVIVAL')),
    preseason BOOLEAN NOT NULL DEFAULT TRUE,
    season_number INTEGER NOT NULL DEFAULT 0 CHECK (season_number BETWEEN 0 AND 9999),
    target_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_by_account_id BIGINT
);
INSERT INTO ranked_seasons (mode) VALUES ('SOLO_RANKED'), ('SURVIVAL') ON CONFLICT (mode) DO NOTHING;
