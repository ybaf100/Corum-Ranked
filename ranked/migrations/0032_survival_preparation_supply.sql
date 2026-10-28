-- Additive operational caches only. No account, match, LP, AP or achievement reset.
CREATE TABLE IF NOT EXISTS sur_map_candidate_buckets (
  bucket_key text PRIMARY KEY CHECK (length(bucket_key)<=200),
  request jsonb NOT NULL,
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS sur_map_candidates (
  bucket_key text NOT NULL REFERENCES sur_map_candidate_buckets(bucket_key),
  level_id bigint NOT NULL CHECK (level_id>=40000000),
  metadata jsonb NOT NULL CHECK (pg_column_size(metadata)<=65536),
  status text NOT NULL CHECK (status IN ('READY','RESERVED','CONSUMED','INVALID','COOLDOWN','EXPIRED')),
  verified_at timestamptz NOT NULL,
  expires_at timestamptz NOT NULL,
  last_used_at timestamptz,
  reservation_token uuid,
  match_id uuid,
  round_key text,
  map_generation integer,
  lease_until timestamptz,
  invalid_reason text,
  retry_after timestamptz,
  PRIMARY KEY(bucket_key,level_id)
);
CREATE INDEX IF NOT EXISTS sur_map_candidates_ready ON sur_map_candidates(bucket_key,status,expires_at);
CREATE UNIQUE INDEX IF NOT EXISTS sur_map_candidates_reserved_level ON sur_map_candidates(level_id) WHERE status='RESERVED';
CREATE TABLE IF NOT EXISTS sur_audio_failure_reports (
  match_id uuid NOT NULL REFERENCES sur_matches(id),
  round_key text NOT NULL,
  map_generation integer NOT NULL,
  level_id bigint NOT NULL,
  player_id uuid NOT NULL REFERENCES ranked_players(id),
  resource_kind text NOT NULL CHECK(resource_kind IN ('SONG','SFX')),
  resource_id integer NOT NULL CHECK(resource_id>0),
  category text NOT NULL,
  native_code integer,
  reported_at timestamptz NOT NULL,
  PRIMARY KEY(match_id,round_key,map_generation,player_id,resource_kind,resource_id)
);
CREATE TABLE IF NOT EXISTS sur_resource_quarantine (
  resource_kind text NOT NULL CHECK(resource_kind IN ('MAP','SONG','SFX')),
  resource_id bigint NOT NULL,
  reason text NOT NULL,
  verified_at timestamptz NOT NULL,
  retry_after timestamptz NOT NULL,
  PRIMARY KEY(resource_kind,resource_id)
);

ALTER TABLE sur_matches ADD COLUMN IF NOT EXISTS map_failure_count integer NOT NULL DEFAULT 0;
