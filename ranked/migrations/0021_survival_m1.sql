BEGIN;

-- Shared account identity stays in ranked_players. No Solo rating is copied.
CREATE TABLE IF NOT EXISTS sur_profiles (
    player_id UUID PRIMARY KEY REFERENCES ranked_players(id) ON DELETE CASCADE,
    lp INTEGER NOT NULL CHECK (lp >= 0),
    displayed_tier TEXT NOT NULL,
    demotion_warning BOOLEAN NOT NULL DEFAULT FALSE,
    lp_reached_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS sur_profiles_leaderboard_idx
    ON sur_profiles(lp DESC, lp_reached_at, player_id);

CREATE TABLE IF NOT EXISTS sur_lp_ledger (
    id UUID PRIMARY KEY,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    event_key TEXT NOT NULL,
    requested_delta INTEGER NOT NULL,
    actual_delta INTEGER NOT NULL,
    lp_before INTEGER NOT NULL CHECK (lp_before >= 0),
    lp_after INTEGER NOT NULL CHECK (lp_after >= 0),
    tier_before TEXT NOT NULL,
    tier_after TEXT NOT NULL,
    warning_before BOOLEAN NOT NULL,
    warning_after BOOLEAN NOT NULL,
    config_generation TEXT NOT NULL,
    result JSONB NOT NULL,
    awarded_at TIMESTAMPTZ NOT NULL,
    UNIQUE (player_id, event_key),
    CHECK (actual_delta = lp_after - lp_before)
);
CREATE INDEX IF NOT EXISTS sur_lp_ledger_player_idx
    ON sur_lp_ledger(player_id, awarded_at DESC, id);

-- Reuse existing unlock and AP uniqueness keys; preserve every historical row.
ALTER TABLE ranked_achievement_events DROP CONSTRAINT IF EXISTS ranked_achievement_events_source_type_check;
ALTER TABLE ranked_achievement_events ADD CONSTRAINT ranked_achievement_events_source_type_check
    CHECK (source_type IN ('MATCH_RESULT', 'PLACEMENT_RESULT', 'BACKFILL', 'SHARED_TIER_REACHED'));

COMMIT;
