-- Resource READY and native scene entry are separate authoritative barriers.
-- The countdown survives process restarts; no gameplay clock runs while loading.
BEGIN;
ALTER TABLE ranked_matches ADD COLUMN IF NOT EXISTS entry_state JSONB;
ALTER TABLE ranked_matches DROP CONSTRAINT IF EXISTS ranked_matches_state_check;
ALTER TABLE ranked_matches ADD CONSTRAINT ranked_matches_state_check CHECK (state IN (
    'MATCHED', 'BAN_PHASE', 'ROUND_PREPARE', 'ROUND_ENTERING', 'ROUND_PLAYING',
    'FINAL_ATTEMPT_WINDOW', 'LAST_ATTEMPT_WINDOW', 'ROUND_SETTLING', 'ROUND_RESULT',
    'DEATHMATCH_PREPARE', 'DEATHMATCH_ENTERING', 'DEATHMATCH_PLAYING',
    'DEATHMATCH_RESULT', 'MATCH_RESULT', 'CANCELLED'
));
ALTER TABLE ranked_rounds DROP CONSTRAINT IF EXISTS ranked_rounds_phase_check;
ALTER TABLE ranked_rounds ADD CONSTRAINT ranked_rounds_phase_check CHECK (phase IN (
    'ROUND_PREPARE', 'ROUND_ENTERING', 'ROUND_PLAYING', 'FINAL_ATTEMPT_WINDOW',
    'LAST_ATTEMPT_WINDOW', 'ROUND_SETTLING', 'ROUND_RESULT'
));
COMMIT;
