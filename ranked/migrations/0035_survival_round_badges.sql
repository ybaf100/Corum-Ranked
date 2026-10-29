-- Append-only official results. No LP/AP rewards or history rewrites.
CREATE TABLE IF NOT EXISTS sur_round_badges (
  match_id uuid NOT NULL REFERENCES sur_matches(id) ON DELETE CASCADE,
  participant_id text NOT NULL,
  player_id uuid REFERENCES ranked_players(id) ON DELETE CASCADE,
  logical_round_key text NOT NULL,
  source_round_key text NOT NULL,
  ordinal integer NOT NULL CHECK (ordinal > 0),
  round_label text NOT NULL,
  level_id bigint NOT NULL CHECK (level_id > 0),
  difficulty text NOT NULL,
  official_rank integer NOT NULL CHECK (official_rank > 0),
  survived boolean NOT NULL,
  grade text NOT NULL CHECK (grade IN ('MYTHIC','LEGENDARY','EPIC','FEATURED','RATED','ELIMINATED')),
  confirmed_at timestamptz NOT NULL,
  PRIMARY KEY (match_id, participant_id, logical_round_key),
  FOREIGN KEY (match_id,participant_id) REFERENCES sur_match_players(match_id,participant_id) ON DELETE CASCADE,
  FOREIGN KEY (match_id,source_round_key) REFERENCES sur_rounds(match_id,round_key) ON DELETE CASCADE,
  CHECK ((grade = 'ELIMINATED') = (NOT survived))
);
CREATE UNIQUE INDEX IF NOT EXISTS sur_round_badges_player_result ON sur_round_badges(player_id,match_id,logical_round_key) WHERE player_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS sur_round_badges_collection ON sur_round_badges(player_id,difficulty,grade) WHERE player_id IS NOT NULL;
