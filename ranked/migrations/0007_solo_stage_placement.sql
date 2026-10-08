BEGIN;

CREATE TABLE IF NOT EXISTS ranked_placement_runs (
    id UUID PRIMARY KEY,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    config_snapshot_id UUID NOT NULL REFERENCES ranked_config_snapshots(id),
    config_generation TEXT NOT NULL,
    state TEXT NOT NULL CHECK (state IN (
        'POOL_SELECTION', 'MAP_PREPARE', 'MAP_PLAYING', 'MAP_SETTLING',
        'MAP_RESULT', 'STAGE_RESULT', 'PLACEMENT_RESULT', 'CANCELLED'
    )),
    state_version BIGINT NOT NULL DEFAULT 1 CHECK (state_version > 0),
    starting_pool SMALLINT NOT NULL CHECK (starting_pool BETWEEN 1 AND 6),
    current_stage_index SMALLINT NOT NULL DEFAULT 1 CHECK (current_stage_index BETWEEN 1 AND 5),
    current_map_index SMALLINT NOT NULL DEFAULT 1 CHECK (current_map_index BETWEEN 1 AND 2),
    stage_pools JSONB NOT NULL DEFAULT '[]'::jsonb CHECK (jsonb_typeof(stage_pools) = 'array'),
    map_snapshots JSONB NOT NULL DEFAULT '[]'::jsonb CHECK (jsonb_typeof(map_snapshots) = 'array'),
    stage_results JSONB NOT NULL DEFAULT '[]'::jsonb CHECK (jsonb_typeof(stage_results) = 'array'),
    placement_rating NUMERIC(14, 6),
    final_hidden_mmr INTEGER,
    final_visible_ranked_score INTEGER,
    final_displayed_tier TEXT CHECK (
        final_displayed_tier IS NULL OR final_displayed_tier IN ('RED', 'AQUA', 'BRONZE', 'SILVER', 'GOLD')
    ),
    last_heartbeat_at TIMESTAMPTZ NOT NULL,
    deadline_at TIMESTAMPTZ,
    cancellation_reason TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMPTZ,
    finalized_at TIMESTAMPTZ,
    result_acknowledged_at TIMESTAMPTZ,
    CONSTRAINT ranked_placement_finalization_all_or_none CHECK (
        (finalized_at IS NULL AND placement_rating IS NULL AND final_hidden_mmr IS NULL AND
         final_visible_ranked_score IS NULL AND final_displayed_tier IS NULL)
        OR
        (finalized_at IS NOT NULL AND placement_rating IS NOT NULL AND final_hidden_mmr IS NOT NULL AND
         final_visible_ranked_score IS NOT NULL AND final_displayed_tier IS NOT NULL)
    )
);

CREATE UNIQUE INDEX IF NOT EXISTS ranked_placement_runs_one_active_player_idx
    ON ranked_placement_runs(player_id)
    WHERE state NOT IN ('PLACEMENT_RESULT', 'CANCELLED');

CREATE INDEX IF NOT EXISTS ranked_placement_runs_recovery_idx
    ON ranked_placement_runs(state, last_heartbeat_at)
    WHERE state NOT IN ('PLACEMENT_RESULT', 'CANCELLED');

CREATE TABLE IF NOT EXISTS ranked_placement_maps (
    id UUID PRIMARY KEY,
    run_id UUID NOT NULL REFERENCES ranked_placement_runs(id) ON DELETE CASCADE,
    stage_index SMALLINT NOT NULL CHECK (stage_index BETWEEN 1 AND 5),
    map_index SMALLINT NOT NULL CHECK (map_index BETWEEN 1 AND 2),
    level_id TEXT NOT NULL,
    canonical_level_id TEXT NOT NULL,
    alternate_level_id TEXT,
    playable_level_id TEXT NOT NULL,
    title TEXT NOT NULL,
    creator TEXT NOT NULL,
    difficulty TEXT NOT NULL,
    pool SMALLINT NOT NULL CHECK (pool BETWEEN 1 AND 6),
    qualifying_percent NUMERIC(6, 3) NOT NULL CHECK (qualifying_percent BETWEEN 0 AND 100),
    state TEXT NOT NULL CHECK (state IN (
        'MAP_PREPARE', 'MAP_PLAYING', 'MAP_SETTLING', 'MAP_RESULT'
    )),
    state_version BIGINT NOT NULL DEFAULT 1 CHECK (state_version > 0),
    started_at TIMESTAMPTZ,
    normal_end_at TIMESTAMPTZ,
    final_window_end_at TIMESTAMPTZ,
    settled_at TIMESTAMPTZ,
    result_deadline_at TIMESTAMPTZ,
    clear_count SMALLINT NOT NULL DEFAULT 0 CHECK (clear_count BETWEEN 0 AND 3),
    top_attempts JSONB NOT NULL DEFAULT '[]'::jsonb CHECK (jsonb_typeof(top_attempts) = 'array'),
    map_score NUMERIC(10, 3) NOT NULL DEFAULT 0 CHECK (map_score BETWEEN 0 AND 750),
    map_grade TEXT NOT NULL DEFAULT 'D' CHECK (map_grade IN ('S', 'A', 'B', 'C', 'D')),
    UNIQUE (run_id, stage_index, map_index)
);

CREATE INDEX IF NOT EXISTS ranked_placement_maps_run_stage_idx
    ON ranked_placement_maps(run_id, stage_index, map_index);

CREATE TABLE IF NOT EXISTS ranked_placement_attempts (
    id UUID PRIMARY KEY,
    run_id UUID NOT NULL REFERENCES ranked_placement_runs(id) ON DELETE CASCADE,
    map_id UUID NOT NULL REFERENCES ranked_placement_maps(id) ON DELETE CASCADE,
    player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
    attempt_sequence INTEGER NOT NULL CHECK (attempt_sequence > 0),
    domain_attempt_id TEXT NOT NULL,
    client_start_event_id TEXT NOT NULL,
    client_end_event_id TEXT,
    played_level_id TEXT NOT NULL,
    server_accepted_start_at TIMESTAMPTZ NOT NULL,
    client_started_at TIMESTAMPTZ,
    ended_at TIMESTAMPTZ,
    client_ended_at TIMESTAMPTZ,
    progress_percent NUMERIC(6, 3) CHECK (progress_percent BETWEEN 0 AND 100),
    latest_progress_percent SMALLINT NOT NULL DEFAULT 0 CHECK (latest_progress_percent BETWEEN 0 AND 100),
    cleared BOOLEAN NOT NULL DEFAULT FALSE,
    awarded_score NUMERIC(10, 3) NOT NULL DEFAULT 0 CHECK (awarded_score BETWEEN 0 AND 250),
    valid BOOLEAN NOT NULL DEFAULT TRUE,
    invalid_reason TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (map_id, attempt_sequence),
    UNIQUE (map_id, domain_attempt_id),
    UNIQUE (map_id, client_start_event_id),
    UNIQUE (map_id, client_end_event_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS ranked_placement_attempts_one_active_idx
    ON ranked_placement_attempts(map_id)
    WHERE ended_at IS NULL AND valid = TRUE;

COMMIT;
