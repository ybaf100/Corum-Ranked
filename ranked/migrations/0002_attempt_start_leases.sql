BEGIN;

-- A visual attempt can start while the previous attempt's End is still waiting
-- in the client's FIFO transport. Persist the observed Start separately so a
-- deadline/result poll (or a Render process restart) cannot finalize the Round
-- before that already-started visual attempt reaches /attempt/start.
CREATE TABLE IF NOT EXISTS ranked_attempt_start_leases (
    id UUID PRIMARY KEY,
    match_id UUID NOT NULL REFERENCES ranked_matches(id) ON DELETE CASCADE,
    round_id UUID NOT NULL REFERENCES ranked_rounds(id) ON DELETE CASCADE,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    side TEXT NOT NULL CHECK (side IN ('A', 'B')),
    level_id TEXT NOT NULL,
    client_start_event_id TEXT NOT NULL,
    client_started_at TIMESTAMPTZ NOT NULL,
    observed_at TIMESTAMPTZ NOT NULL,
    consumed_at TIMESTAMPTZ,
    invalidated_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (round_id, player_id, client_start_event_id)
);

CREATE INDEX IF NOT EXISTS ranked_attempt_start_leases_pending_idx
    ON ranked_attempt_start_leases(round_id, side, observed_at)
    WHERE consumed_at IS NULL AND invalidated_at IS NULL;

COMMIT;
