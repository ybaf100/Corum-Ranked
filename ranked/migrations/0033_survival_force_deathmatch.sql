-- Forced Death Match can start without a preceding FINAL. Existing FINAL links
-- and all historical attempts/results remain intact; NULL means no FINAL exists.
ALTER TABLE sur_tiebreaks ALTER COLUMN final_round_key DROP NOT NULL;
ALTER TABLE sur_queue_fill_requests ADD COLUMN IF NOT EXISTS force_death_match boolean NOT NULL DEFAULT false;
