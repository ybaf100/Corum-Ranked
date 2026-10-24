BEGIN;
-- Additive format support only. Existing match snapshots, ratings and
-- achievement cutover markers are deliberately retained.
ALTER TABLE sur_mode_state ADD COLUMN IF NOT EXISTS match_player_count SMALLINT NOT NULL DEFAULT 16
 CHECK (match_player_count IN (16,64));
ALTER TABLE sur_match_players DROP CONSTRAINT IF EXISTS sur_match_players_final_rank_check;
ALTER TABLE sur_match_players ADD CONSTRAINT sur_match_players_final_rank_check CHECK (final_rank BETWEEN 1 AND 64);
ALTER TABLE sur_round_players DROP CONSTRAINT IF EXISTS sur_round_players_round_rank_check;
ALTER TABLE sur_round_players ADD CONSTRAINT sur_round_players_round_rank_check CHECK (round_rank BETWEEN 1 AND 64);
ALTER TABLE sur_debug_matches DROP CONSTRAINT IF EXISTS sur_debug_matches_force_rank_check;
ALTER TABLE sur_debug_matches ADD CONSTRAINT sur_debug_matches_force_rank_check CHECK (force_rank BETWEEN 1 AND 64);
ALTER TABLE sur_queue_fill_requests DROP CONSTRAINT IF EXISTS sur_queue_fill_requests_bot_count_check;
ALTER TABLE sur_queue_fill_requests ADD CONSTRAINT sur_queue_fill_requests_bot_count_check CHECK (bot_count BETWEEN 0 AND 63);
ALTER TABLE sur_rounds DROP CONSTRAINT IF EXISTS sur_rounds_phase_check;
ALTER TABLE sur_rounds ADD CONSTRAINT sur_rounds_phase_check CHECK (phase IN ('ROUND_1','ROUND_2','ROUND_3','ROUND_4','FINAL','TIEBREAK'));
COMMIT;
