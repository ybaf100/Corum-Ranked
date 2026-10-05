-- Debug-only domain: deliberately no Ranked profile/match foreign keys or writes.
BEGIN;
CREATE TABLE IF NOT EXISTS debug_spectate_recordings (
  id uuid PRIMARY KEY,
  level_id bigint NOT NULL CHECK (level_id = 88350599),
  format_version integer NOT NULL CHECK (format_version >= 1),
  sample_interval_ms integer NOT NULL CHECK (sample_interval_ms > 0),
  created_at timestamptz NOT NULL,
  finalized_at timestamptz,
  recorder_identity text,
  token_hash text NOT NULL,
  expires_at timestamptz NOT NULL,
  completed boolean NOT NULL DEFAULT false,
  duration_ms integer NOT NULL DEFAULT 0 CHECK (duration_ms >= 0),
  frame_count integer NOT NULL DEFAULT 0 CHECK (frame_count >= 0)
);
CREATE INDEX IF NOT EXISTS debug_spectate_latest_completed
  ON debug_spectate_recordings (created_at DESC, id DESC) WHERE completed;
CREATE TABLE IF NOT EXISTS debug_spectate_frames (
  recording_id uuid NOT NULL REFERENCES debug_spectate_recordings(id),
  sequence integer NOT NULL CHECK (sequence >= 0),
  elapsed_ms integer NOT NULL CHECK (elapsed_ms >= 0),
  keyframe jsonb NOT NULL,
  PRIMARY KEY (recording_id, sequence)
);
COMMIT;
