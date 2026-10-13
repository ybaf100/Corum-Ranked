BEGIN;

-- Public Survival admission, assignment, presence and result recovery. Bots
-- remain aggregate participants and never receive ranked_players/profiles.
CREATE TABLE IF NOT EXISTS sur_mode_state (
  singleton BOOLEAN PRIMARY KEY DEFAULT TRUE CHECK (singleton),
  enabled BOOLEAN NOT NULL,
  generation TEXT NOT NULL,
  generated_at TIMESTAMPTZ NOT NULL
);
CREATE TABLE IF NOT EXISTS sur_queue_entries (
  player_id UUID PRIMARY KEY REFERENCES ranked_players(id) ON DELETE CASCADE,
  session_id UUID NOT NULL REFERENCES ranked_sessions(id),
  client_version TEXT NOT NULL,
  entry_id UUID NOT NULL DEFAULT gen_random_uuid(),
  joined_at TIMESTAMPTZ NOT NULL,
  wait_started_at TIMESTAMPTZ NOT NULL,
  last_heartbeat_at TIMESTAMPTZ NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('QUEUED','MATCHED','LEFT','EXPIRED','DISABLED')),
  matched_match_id UUID REFERENCES sur_matches(id),
  CHECK ((status='MATCHED') = (matched_match_id IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS sur_queue_waiting_idx ON sur_queue_entries(wait_started_at,joined_at,player_id) WHERE status='QUEUED';
CREATE TABLE IF NOT EXISTS sur_online_matches (
  match_id UUID PRIMARY KEY REFERENCES sur_matches(id) ON DELETE CASCADE,
  bot_filled BOOLEAN NOT NULL,
  seed BIGINT NOT NULL CHECK (seed BETWEEN 0 AND 4294967295),
  driver_state JSONB NOT NULL DEFAULT '{}'::jsonb,
  created_at TIMESTAMPTZ NOT NULL
);
CREATE TABLE IF NOT EXISTS sur_online_players (
  match_id UUID NOT NULL REFERENCES sur_online_matches(match_id) ON DELETE CASCADE,
  player_id UUID NOT NULL REFERENCES ranked_players(id),
  participant_id TEXT NOT NULL,
  admission_session_id UUID NOT NULL REFERENCES ranked_sessions(id),
  last_seen_at TIMESTAMPTZ NOT NULL,
  observed_round_key TEXT,
  observed_map_generation INTEGER,
  result JSONB,
  settled_at TIMESTAMPTZ,
  acknowledged_at TIMESTAMPTZ,
  PRIMARY KEY(match_id,player_id),
  FOREIGN KEY(match_id,participant_id) REFERENCES sur_match_players(match_id,participant_id) ON DELETE CASCADE,
  CHECK ((result IS NULL) = (settled_at IS NULL))
);
CREATE INDEX IF NOT EXISTS sur_online_recovery_idx ON sur_online_players(player_id,last_seen_at DESC) WHERE acknowledged_at IS NULL;
CREATE INDEX IF NOT EXISTS sur_online_pending_result_idx ON sur_online_players(match_id,player_id) WHERE settled_at IS NULL;
CREATE TABLE IF NOT EXISTS sur_queue_fill_requests (
  player_id UUID NOT NULL REFERENCES ranked_players(id),
  request_id UUID NOT NULL,
  match_id UUID NOT NULL REFERENCES sur_online_matches(match_id) ON DELETE CASCADE,
  created_at TIMESTAMPTZ NOT NULL,
  PRIMARY KEY(player_id,request_id)
);
CREATE TABLE IF NOT EXISTS sur_request_rates (
  player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
  bucket TEXT NOT NULL,
  window_started_at TIMESTAMPTZ NOT NULL,
  requests INTEGER NOT NULL CHECK(requests>0),
  PRIMARY KEY(player_id,bucket)
);

COMMIT;
