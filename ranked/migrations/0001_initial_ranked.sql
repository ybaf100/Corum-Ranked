BEGIN;

CREATE TABLE ranked_players (
    id UUID PRIMARY KEY,
    gd_account_id BIGINT NOT NULL UNIQUE,
    gd_username TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE ranked_profiles (
    player_id UUID PRIMARY KEY REFERENCES ranked_players(id) ON DELETE CASCADE,
    displayed_tier TEXT NOT NULL DEFAULT 'UNRANKED'
        CHECK (displayed_tier IN ('UNRANKED', 'RED', 'AQUA', 'BRONZE', 'SILVER', 'GOLD')),
    hidden_mmr INTEGER,
    visible_ranked_score INTEGER,
    placement_games INTEGER NOT NULL DEFAULT 0 CHECK (placement_games >= 0),
    wins INTEGER NOT NULL DEFAULT 0 CHECK (wins >= 0),
    losses INTEGER NOT NULL DEFAULT 0 CHECK (losses >= 0),
    match_draws INTEGER NOT NULL DEFAULT 0 CHECK (match_draws >= 0),
    initial_csmp_tier TEXT CHECK (
        initial_csmp_tier IS NULL OR
        initial_csmp_tier IN ('NONE', 'RED', 'AQUA', 'BRONZE', 'SILVER', 'GOLD')
    ),
    initial_seed_mmr INTEGER,
    seed_applied_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT ranked_profile_seed_all_or_none CHECK (
        (hidden_mmr IS NULL AND initial_csmp_tier IS NULL AND initial_seed_mmr IS NULL AND seed_applied_at IS NULL)
        OR
        (hidden_mmr IS NOT NULL AND initial_csmp_tier IS NOT NULL AND initial_seed_mmr IS NOT NULL AND seed_applied_at IS NOT NULL)
    )
);

CREATE TABLE ranked_config_snapshots (
    id UUID PRIMARY KEY,
    generation TEXT NOT NULL,
    rules_version TEXT NOT NULL,
    source_payload JSONB NOT NULL,
    fetched_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (generation, rules_version)
);

CREATE TABLE ranked_matches (
    id UUID PRIMARY KEY,
    match_type TEXT NOT NULL DEFAULT 'RANKED_PVP'
        CHECK (match_type IN ('RANKED_PVP', 'DEBUG_BOT')),
    debug_config JSONB,
    debug_discord_events BOOLEAN NOT NULL DEFAULT FALSE,
    player_a_id UUID NOT NULL REFERENCES ranked_players(id),
    player_b_id UUID NOT NULL REFERENCES ranked_players(id),
    config_snapshot_id UUID NOT NULL REFERENCES ranked_config_snapshots(id),
    mmr_a_before INTEGER NOT NULL,
    mmr_b_before INTEGER NOT NULL,
    effective_rating_average NUMERIC(12, 3) NOT NULL,
    effective_tier TEXT NOT NULL CHECK (effective_tier IN ('RED', 'AQUA', 'BRONZE', 'SILVER', 'GOLD')),
    candidate_maps_snapshot JSONB NOT NULL,
    ban_a_canonical_id TEXT,
    ban_b_canonical_id TEXT,
    ban_a_confirmed_at TIMESTAMPTZ,
    ban_b_confirmed_at TIMESTAMPTZ,
    ban_deadline_at TIMESTAMPTZ,
    selected_round_maps_snapshot JSONB,
    series_state JSONB NOT NULL,
    current_round_number SMALLINT CHECK (current_round_number BETWEEN 1 AND 3),
    current_deathmatch_id UUID,
    ready_a_at TIMESTAMPTZ,
    ready_b_at TIMESTAMPTZ,
    ready_deadline_at TIMESTAMPTZ,
    last_heartbeat_a_at TIMESTAMPTZ,
    last_heartbeat_b_at TIMESTAMPTZ,
    round_wins_a SMALLINT NOT NULL DEFAULT 0 CHECK (round_wins_a BETWEEN 0 AND 3),
    round_wins_b SMALLINT NOT NULL DEFAULT 0 CHECK (round_wins_b BETWEEN 0 AND 3),
    state TEXT NOT NULL CHECK (state IN (
        'MATCHED', 'BAN_PHASE', 'ROUND_PREPARE', 'ROUND_PLAYING',
        'FINAL_ATTEMPT_WINDOW', 'LAST_ATTEMPT_WINDOW', 'ROUND_SETTLING',
        'ROUND_RESULT', 'DEATHMATCH_PREPARE', 'DEATHMATCH_PLAYING',
        'DEATHMATCH_RESULT', 'MATCH_RESULT', 'CANCELLED'
    )),
    state_version BIGINT NOT NULL DEFAULT 1 CHECK (state_version > 0),
    deadline_at TIMESTAMPTZ,
    winner_id UUID REFERENCES ranked_players(id),
    mmr_delta_a INTEGER,
    mmr_delta_b INTEGER,
    mmr_a_after INTEGER,
    mmr_b_after INTEGER,
    result_applied_at TIMESTAMPTZ,
    rules_version TEXT NOT NULL,
    cancellation_reason TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMPTZ,
    finished_at TIMESTAMPTZ,
    CONSTRAINT ranked_match_distinct_players CHECK (player_a_id <> player_b_id),
    CONSTRAINT ranked_match_winner_participant CHECK (
        winner_id IS NULL OR winner_id = player_a_id OR winner_id = player_b_id
    ),
    CONSTRAINT ranked_match_debug_shape CHECK (
        (match_type = 'RANKED_PVP' AND debug_config IS NULL AND debug_discord_events = FALSE)
        OR
        (match_type = 'DEBUG_BOT' AND debug_config IS NOT NULL)
    )
);

CREATE INDEX ranked_matches_player_a_created_idx ON ranked_matches(player_a_id, created_at DESC);
CREATE INDEX ranked_matches_player_b_created_idx ON ranked_matches(player_b_id, created_at DESC);
CREATE INDEX ranked_matches_state_idx ON ranked_matches(state) WHERE finished_at IS NULL;
CREATE INDEX ranked_matches_type_state_idx ON ranked_matches(match_type, state)
    WHERE finished_at IS NULL;

CREATE VIEW ranked_public_match_history AS
SELECT *
FROM ranked_matches;

CREATE VIEW ranked_leaderboard AS
SELECT
    p.gd_account_id,
    p.gd_username,
    rp.visible_ranked_score,
    rp.displayed_tier,
    rp.placement_games,
    rp.wins,
    rp.losses,
    rp.match_draws,
    rp.updated_at
FROM ranked_profiles rp
JOIN ranked_players p ON p.id = rp.player_id
WHERE rp.visible_ranked_score IS NOT NULL
  AND rp.displayed_tier <> 'UNRANKED'
  AND p.gd_account_id > 0;

CREATE TABLE ranked_match_tokens (
    match_id UUID NOT NULL REFERENCES ranked_matches(id) ON DELETE CASCADE,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (match_id, player_id)
);

CREATE TABLE ranked_rounds (
    id UUID PRIMARY KEY,
    match_id UUID NOT NULL REFERENCES ranked_matches(id) ON DELETE CASCADE,
    round_number SMALLINT NOT NULL CHECK (round_number BETWEEN 1 AND 3),
    level_id TEXT NOT NULL,
    canonical_level_id TEXT NOT NULL,
    alternate_level_id TEXT,
    playable_level_id TEXT NOT NULL,
    title TEXT NOT NULL,
    creator TEXT NOT NULL,
    difficulty TEXT NOT NULL,
    pool SMALLINT NOT NULL CHECK (pool BETWEEN 1 AND 6),
    qualifying_percent NUMERIC(6, 3) NOT NULL CHECK (qualifying_percent BETWEEN 0 AND 100),
    started_at TIMESTAMPTZ,
    normal_end_at TIMESTAMPTZ,
    final_window_end_at TIMESTAMPTZ,
    phase TEXT NOT NULL CHECK (phase IN (
        'ROUND_PREPARE', 'ROUND_PLAYING', 'FINAL_ATTEMPT_WINDOW',
        'LAST_ATTEMPT_WINDOW', 'ROUND_SETTLING', 'ROUND_RESULT'
    )),
    domain_state JSONB,
    ready_a_at TIMESTAMPTZ,
    ready_b_at TIMESTAMPTZ,
    ready_deadline_at TIMESTAMPTZ,
    result_deadline_at TIMESTAMPTZ,
    score_a NUMERIC(14, 3) NOT NULL DEFAULT 0,
    score_b NUMERIC(14, 3) NOT NULL DEFAULT 0,
    clears_a SMALLINT NOT NULL DEFAULT 0 CHECK (clears_a >= 0),
    clears_b SMALLINT NOT NULL DEFAULT 0 CHECK (clears_b >= 0),
    result TEXT CHECK (result IS NULL OR result IN ('A', 'B', 'DRAW')),
    result_reason TEXT,
    two_clear_rule_triggered BOOLEAN NOT NULL DEFAULT FALSE,
    last_attempt_target TEXT CHECK (last_attempt_target IS NULL OR last_attempt_target IN ('A', 'B')),
    last_attempt_window_start TIMESTAMPTZ,
    last_attempt_window_end TIMESTAMPTZ,
    settled_at TIMESTAMPTZ,
    state_version BIGINT NOT NULL DEFAULT 1 CHECK (state_version > 0),
    UNIQUE (match_id, round_number)
);

CREATE TABLE ranked_attempts (
    id UUID PRIMARY KEY,
    round_id UUID NOT NULL REFERENCES ranked_rounds(id) ON DELETE CASCADE,
    player_id UUID NOT NULL REFERENCES ranked_players(id),
    played_level_id TEXT NOT NULL,
    attempt_sequence INTEGER NOT NULL CHECK (attempt_sequence > 0),
    server_accepted_start_at TIMESTAMPTZ NOT NULL,
    client_started_at TIMESTAMPTZ,
    ended_at TIMESTAMPTZ,
    client_ended_at TIMESTAMPTZ,
    progress_percent NUMERIC(6, 3) CHECK (progress_percent BETWEEN 0 AND 100),
    cleared BOOLEAN NOT NULL DEFAULT FALSE,
    awarded_score NUMERIC(10, 3) NOT NULL DEFAULT 0,
    valid BOOLEAN NOT NULL DEFAULT TRUE,
    invalid_reason TEXT,
    domain_attempt_id TEXT NOT NULL,
    client_start_event_id TEXT NOT NULL,
    client_end_event_id TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (round_id, player_id, attempt_sequence),
    UNIQUE (round_id, player_id, domain_attempt_id),
    UNIQUE (round_id, player_id, client_start_event_id),
    UNIQUE (round_id, player_id, client_end_event_id)
);

CREATE INDEX ranked_attempts_active_idx
    ON ranked_attempts(round_id, player_id)
    WHERE ended_at IS NULL AND valid = TRUE;

CREATE TABLE ranked_deathmatches (
    id UUID PRIMARY KEY,
    match_id UUID NOT NULL REFERENCES ranked_matches(id) ON DELETE CASCADE,
    sequence INTEGER NOT NULL CHECK (sequence > 0),
    map_snapshot JSONB NOT NULL,
    score_a NUMERIC(14, 3),
    score_b NUMERIC(14, 3),
    winner_id UUID REFERENCES ranked_players(id),
    started_at TIMESTAMPTZ,
    finished_at TIMESTAMPTZ,
    UNIQUE (match_id, sequence)
);

CREATE TABLE ranked_deathmatch_attempts (
    id UUID PRIMARY KEY,
    deathmatch_id UUID NOT NULL REFERENCES ranked_deathmatches(id) ON DELETE CASCADE,
    player_id UUID NOT NULL REFERENCES ranked_players(id),
    played_level_id TEXT NOT NULL,
    attempt_sequence SMALLINT NOT NULL CHECK (attempt_sequence BETWEEN 1 AND 3),
    server_accepted_start_at TIMESTAMPTZ NOT NULL,
    ended_at TIMESTAMPTZ,
    progress_percent NUMERIC(6, 3) CHECK (progress_percent BETWEEN 0 AND 100),
    cleared BOOLEAN NOT NULL DEFAULT FALSE,
    awarded_score NUMERIC(10, 3) NOT NULL DEFAULT 0,
    valid BOOLEAN NOT NULL DEFAULT TRUE,
    invalid_reason TEXT,
    client_start_event_id TEXT NOT NULL,
    client_end_event_id TEXT,
    UNIQUE (deathmatch_id, player_id, attempt_sequence),
    UNIQUE (deathmatch_id, player_id, client_start_event_id),
    UNIQUE (deathmatch_id, player_id, client_end_event_id)
);

CREATE TABLE ranked_sessions (
    id UUID PRIMARY KEY,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    client_version TEXT NOT NULL,
    environment_snapshot JSONB NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    last_heartbeat_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX ranked_sessions_player_active_idx
    ON ranked_sessions(player_id, expires_at DESC)
    WHERE revoked_at IS NULL;

CREATE TABLE ranked_queue_entries (
    player_id UUID PRIMARY KEY REFERENCES ranked_players(id) ON DELETE CASCADE,
    session_id UUID NOT NULL UNIQUE REFERENCES ranked_sessions(id) ON DELETE CASCADE,
    hidden_mmr_snapshot INTEGER NOT NULL,
    joined_at TIMESTAMPTZ NOT NULL,
    last_heartbeat_at TIMESTAMPTZ NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('QUEUED', 'MATCHED', 'LEFT', 'EXPIRED')),
    matched_match_id UUID REFERENCES ranked_matches(id)
);

CREATE INDEX ranked_queue_waiting_idx
    ON ranked_queue_entries(status, joined_at)
    WHERE status = 'QUEUED';

CREATE TABLE ranked_outbox_events (
    id UUID PRIMARY KEY,
    aggregate_type TEXT NOT NULL,
    aggregate_id UUID NOT NULL,
    event_type TEXT NOT NULL,
    deduplication_key TEXT NOT NULL UNIQUE,
    payload JSONB NOT NULL,
    available_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    delivered_at TIMESTAMPTZ,
    abandoned_at TIMESTAMPTZ,
    last_error TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX ranked_outbox_pending_idx
    ON ranked_outbox_events(available_at, created_at)
    WHERE delivered_at IS NULL AND abandoned_at IS NULL;

COMMIT;
