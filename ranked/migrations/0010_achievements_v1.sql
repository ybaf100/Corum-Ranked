BEGIN;

ALTER TABLE ranked_profiles
    ADD COLUMN IF NOT EXISTS total_ap INTEGER NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS equipped_achievement_id TEXT,
    ADD COLUMN IF NOT EXISTS equipped_achievement_level SMALLINT;

ALTER TABLE ranked_profiles
    DROP CONSTRAINT IF EXISTS ranked_profiles_total_ap_check,
    DROP CONSTRAINT IF EXISTS ranked_profiles_equipped_achievement_check;

ALTER TABLE ranked_profiles
    ADD CONSTRAINT ranked_profiles_total_ap_check CHECK (total_ap >= 0),
    ADD CONSTRAINT ranked_profiles_equipped_achievement_check CHECK (
      (equipped_achievement_id IS NULL AND equipped_achievement_level IS NULL)
      OR
      (equipped_achievement_id IS NOT NULL AND
       equipped_achievement_level IS NOT NULL AND
       equipped_achievement_level BETWEEN 0 AND 5)
    );

CREATE TABLE IF NOT EXISTS ranked_achievement_metrics (
    player_id UUID PRIMARY KEY REFERENCES ranked_players(id) ON DELETE CASCADE,
    ranked_wins INTEGER NOT NULL DEFAULT 0 CHECK (ranked_wins >= 0),
    ranked_matches INTEGER NOT NULL DEFAULT 0 CHECK (ranked_matches >= 0),
    ranked_clears INTEGER NOT NULL DEFAULT 0 CHECK (ranked_clears >= 0),
    perfect_wins INTEGER NOT NULL DEFAULT 0 CHECK (perfect_wins >= 0),
    current_win_streak INTEGER NOT NULL DEFAULT 0 CHECK (current_win_streak >= 0),
    best_win_streak INTEGER NOT NULL DEFAULT 0 CHECK (best_win_streak >= current_win_streak),
    deathmatch_wins INTEGER NOT NULL DEFAULT 0 CHECK (deathmatch_wins >= 0),
    last_attempt_clears INTEGER NOT NULL DEFAULT 0 CHECK (last_attempt_clears >= 0),
    reverse_sweeps INTEGER NOT NULL DEFAULT 0 CHECK (reverse_sweeps >= 0),
    sweep_wins INTEGER NOT NULL DEFAULT 0 CHECK (sweep_wins >= 0),
    promotion_wins INTEGER NOT NULL DEFAULT 0 CHECK (promotion_wins >= 0),
    current_perfect_win_streak INTEGER NOT NULL DEFAULT 0 CHECK (current_perfect_win_streak >= 0),
    backfilled_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ranked_achievement_events (
    event_key TEXT PRIMARY KEY,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    source_type TEXT NOT NULL CHECK (source_type IN ('MATCH_RESULT', 'PLACEMENT_RESULT', 'BACKFILL')),
    source_id UUID,
    occurred_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS ranked_achievement_events_player_idx
    ON ranked_achievement_events(player_id, occurred_at DESC);

CREATE TABLE IF NOT EXISTS ranked_achievement_unlocks (
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    achievement_id TEXT NOT NULL,
    achievement_level SMALLINT NOT NULL CHECK (achievement_level BETWEEN 0 AND 5),
    reward_ap INTEGER NOT NULL CHECK (reward_ap > 0),
    unlocked_at TIMESTAMPTZ NOT NULL,
    claimed_at TIMESTAMPTZ,
    source_event_key TEXT NOT NULL,
    PRIMARY KEY (player_id, achievement_id, achievement_level)
);

CREATE INDEX IF NOT EXISTS ranked_achievement_unlocks_unclaimed_idx
    ON ranked_achievement_unlocks(player_id, unlocked_at)
    WHERE claimed_at IS NULL;

CREATE TABLE IF NOT EXISTS ranked_ap_ledger (
    id UUID PRIMARY KEY,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    achievement_id TEXT NOT NULL,
    achievement_level SMALLINT NOT NULL CHECK (achievement_level BETWEEN 0 AND 5),
    amount INTEGER NOT NULL CHECK (amount > 0),
    awarded_at TIMESTAMPTZ NOT NULL,
    UNIQUE (player_id, achievement_id, achievement_level)
);

CREATE INDEX IF NOT EXISTS ranked_ap_ledger_player_idx
    ON ranked_ap_ledger(player_id, awarded_at, id);

COMMIT;
