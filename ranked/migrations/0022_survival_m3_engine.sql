BEGIN;

-- Survival aggregate and its queryable projections commit under one row lock.
-- Solo match/session/profile/achievement tables are not altered.
CREATE TABLE IF NOT EXISTS sur_matches (
  id UUID PRIMARY KEY,
  creation_key TEXT NOT NULL UNIQUE,
  request_hash TEXT NOT NULL,
  schema_version INTEGER NOT NULL DEFAULT 1 CHECK (schema_version = 1),
  state TEXT NOT NULL CHECK (state IN ('MAP_SELECTION','PREPARING','ENTERING','COUNTDOWN','PLAYING','ROUND_RESULT','FINAL_RESULT','CANCELLED')),
  revision INTEGER NOT NULL CHECK (revision > 0),
  config_generation TEXT NOT NULL,
  config_snapshot JSONB NOT NULL,
  average_lp DOUBLE PRECISION NOT NULL CHECK (average_lp >= 0),
  difficulty_group TEXT NOT NULL CHECK (difficulty_group IN ('RED_BRONZE','SILVER','GOLD')),
  current_round_key TEXT NOT NULL,
  winner_id TEXT,
  snapshot JSONB NOT NULL,
  map_job_token UUID,
  map_job_deadline_at TIMESTAMPTZ,
  map_retry_at TIMESTAMPTZ,
  map_error JSONB,
  created_at TIMESTAMPTZ NOT NULL,
  updated_at TIMESTAMPTZ NOT NULL,
  finished_at TIMESTAMPTZ,
  CHECK ((map_job_token IS NULL) = (map_job_deadline_at IS NULL))
);
CREATE INDEX IF NOT EXISTS sur_matches_active_idx ON sur_matches(updated_at,id)
  WHERE state NOT IN ('FINAL_RESULT','CANCELLED');

CREATE TABLE IF NOT EXISTS sur_match_players (
  match_id UUID NOT NULL REFERENCES sur_matches(id) ON DELETE CASCADE,
  participant_id TEXT NOT NULL CHECK (length(participant_id) BETWEEN 1 AND 80),
  player_id UUID REFERENCES ranked_players(id),
  player_kind TEXT NOT NULL CHECK (player_kind IN ('HUMAN','BOT')),
  player_name TEXT NOT NULL,
  lp_snapshot INTEGER NOT NULL CHECK (lp_snapshot >= 0),
  status TEXT NOT NULL CHECK (status IN ('ACTIVE','ELIMINATED','FINISHED','CANCELLED')),
  elimination_stage INTEGER,
  elimination_score DOUBLE PRECISION NOT NULL DEFAULT 0 CHECK (elimination_score BETWEEN 0 AND 100),
  forfeit_reason TEXT,
  final_rank INTEGER CHECK (final_rank BETWEEN 1 AND 16),
  PRIMARY KEY (match_id,participant_id), UNIQUE (match_id,player_id),
  CHECK (player_kind <> 'HUMAN' OR player_id IS NOT NULL)
);
CREATE UNIQUE INDEX IF NOT EXISTS sur_match_players_active_account_idx ON sur_match_players(player_id)
  WHERE player_id IS NOT NULL AND status='ACTIVE';
CREATE INDEX IF NOT EXISTS sur_match_players_history_idx ON sur_match_players(player_id,match_id);

CREATE TABLE IF NOT EXISTS sur_rounds (
  match_id UUID NOT NULL REFERENCES sur_matches(id) ON DELETE CASCADE,
  round_key TEXT NOT NULL,
  ordinal INTEGER NOT NULL CHECK (ordinal > 0),
  phase TEXT NOT NULL CHECK (phase IN ('ROUND_1','ROUND_2','ROUND_3','FINAL','TIEBREAK')),
  target_qualifiers INTEGER NOT NULL CHECK (target_qualifiers > 0),
  map_generation INTEGER NOT NULL CHECK (map_generation > 0),
  level_id BIGINT CHECK (level_id >= 40000000),
  map_snapshot JSONB,
  selected_maps JSONB NOT NULL DEFAULT '[]',
  preparation_started_at TIMESTAMPTZ,
  entry_requested_at TIMESTAMPTZ,
  first_entered_at TIMESTAMPTZ,
  starts_at TIMESTAMPTZ,
  ends_at TIMESTAMPTZ,
  finished_at TIMESTAMPTZ,
  cutoff_score DOUBLE PRECISION,
  PRIMARY KEY (match_id,round_key), UNIQUE (match_id,ordinal),
  CHECK ((starts_at IS NULL AND ends_at IS NULL) OR ends_at=starts_at + INTERVAL '180 seconds')
);
CREATE TABLE IF NOT EXISTS sur_round_players (
  match_id UUID NOT NULL,
  round_key TEXT NOT NULL,
  participant_id TEXT NOT NULL,
  assets_ready_at TIMESTAMPTZ,
  preparation_failure TEXT,
  entered_at TIMESTAMPTZ,
  highest_progress DOUBLE PRECISION NOT NULL CHECK (highest_progress BETWEEN 0 AND 100),
  round_rank INTEGER CHECK (round_rank BETWEEN 1 AND 16),
  qualified BOOLEAN NOT NULL,
  forfeited BOOLEAN NOT NULL,
  PRIMARY KEY (match_id,round_key,participant_id),
  FOREIGN KEY (match_id,round_key) REFERENCES sur_rounds(match_id,round_key) ON DELETE CASCADE,
  FOREIGN KEY (match_id,participant_id) REFERENCES sur_match_players(match_id,participant_id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS sur_attempts (
  match_id UUID NOT NULL, round_key TEXT NOT NULL, participant_id TEXT NOT NULL,
  attempt_id TEXT NOT NULL CHECK (length(attempt_id) BETWEEN 1 AND 80),
  sequence INTEGER NOT NULL CHECK (sequence > 0),
  highest_progress DOUBLE PRECISION NOT NULL CHECK (highest_progress BETWEEN 0 AND 100),
  started_at TIMESTAMPTZ NOT NULL, ended_at TIMESTAMPTZ,
  PRIMARY KEY (match_id,round_key,participant_id,attempt_id),
  UNIQUE (match_id,round_key,participant_id,sequence),
  FOREIGN KEY (match_id,round_key,participant_id) REFERENCES sur_round_players(match_id,round_key,participant_id) ON DELETE CASCADE,
  CHECK (ended_at IS NULL OR ended_at >= started_at)
);
CREATE UNIQUE INDEX IF NOT EXISTS sur_attempts_active_idx ON sur_attempts(match_id,round_key,participant_id) WHERE ended_at IS NULL;

CREATE TABLE IF NOT EXISTS sur_tiebreaks (
  match_id UUID NOT NULL, round_key TEXT NOT NULL,
  sequence INTEGER NOT NULL CHECK (sequence > 0),
  final_round_key TEXT NOT NULL,
  result JSONB,
  PRIMARY KEY (match_id,round_key), UNIQUE (match_id,sequence),
  FOREIGN KEY (match_id,round_key) REFERENCES sur_rounds(match_id,round_key) ON DELETE CASCADE,
  FOREIGN KEY (match_id,final_round_key) REFERENCES sur_rounds(match_id,round_key)
);
CREATE TABLE IF NOT EXISTS sur_tiebreak_attempts (
  match_id UUID NOT NULL, round_key TEXT NOT NULL, participant_id TEXT NOT NULL,
  attempt_id TEXT NOT NULL CHECK (length(attempt_id) BETWEEN 1 AND 80),
  sequence INTEGER NOT NULL CHECK (sequence BETWEEN 1 AND 3),
  highest_progress DOUBLE PRECISION NOT NULL CHECK (highest_progress BETWEEN 0 AND 100),
  score_millipercent INTEGER NOT NULL CHECK (score_millipercent BETWEEN 0 AND 100000),
  started_at TIMESTAMPTZ NOT NULL, ended_at TIMESTAMPTZ,
  PRIMARY KEY (match_id,round_key,participant_id,attempt_id),
  UNIQUE (match_id,round_key,participant_id,sequence),
  FOREIGN KEY (match_id,round_key) REFERENCES sur_tiebreaks(match_id,round_key) ON DELETE CASCADE,
  FOREIGN KEY (match_id,round_key,participant_id) REFERENCES sur_round_players(match_id,round_key,participant_id) ON DELETE CASCADE,
  CHECK (ended_at IS NULL OR ended_at >= started_at)
);
CREATE UNIQUE INDEX IF NOT EXISTS sur_tiebreak_attempts_active_idx ON sur_tiebreak_attempts(match_id,round_key,participant_id) WHERE ended_at IS NULL;

CREATE TABLE IF NOT EXISTS sur_match_commands (
  match_id UUID NOT NULL REFERENCES sur_matches(id) ON DELETE CASCADE,
  event_key TEXT NOT NULL CHECK (length(event_key) BETWEEN 1 AND 200),
  request_hash TEXT NOT NULL,
  command JSONB NOT NULL,
  applied_revision INTEGER NOT NULL,
  received_at TIMESTAMPTZ NOT NULL,
  PRIMARY KEY (match_id,event_key)
);
COMMIT;
