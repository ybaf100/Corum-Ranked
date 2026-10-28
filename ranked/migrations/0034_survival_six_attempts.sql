-- Expand only Survival's projection bound. Existing rows, ranks, receipts and
-- per-match snapshot limits (including legacy three-attempt matches) survive.
ALTER TABLE sur_tiebreak_attempts DROP CONSTRAINT IF EXISTS sur_tiebreak_attempts_sequence_check;
ALTER TABLE sur_tiebreak_attempts ADD CONSTRAINT sur_tiebreak_attempts_sequence_check CHECK (sequence BETWEEN 1 AND 6);
