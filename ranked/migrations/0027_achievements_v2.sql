BEGIN;

-- One-time v2 cutover explicitly requested by the owner. Only achievement/AP/
-- title state is reset. Competitive history, accounts, sessions and LP/MMR stay.
CREATE TABLE IF NOT EXISTS ranked_achievement_versions (
 version SMALLINT PRIMARY KEY CHECK (version=2), activated_at TIMESTAMPTZ NOT NULL
);
ALTER TABLE ranked_achievement_metrics
 ADD COLUMN IF NOT EXISTS survival_matches INTEGER NOT NULL DEFAULT 0 CHECK(survival_matches>=0),
 ADD COLUMN IF NOT EXISTS survival_wins INTEGER NOT NULL DEFAULT 0 CHECK(survival_wins>=0),
 ADD COLUMN IF NOT EXISTS survival_podiums INTEGER NOT NULL DEFAULT 0 CHECK(survival_podiums>=0),
 ADD COLUMN IF NOT EXISTS survival_finals INTEGER NOT NULL DEFAULT 0 CHECK(survival_finals>=0),
 ADD COLUMN IF NOT EXISTS survival_round_survives INTEGER NOT NULL DEFAULT 0 CHECK(survival_round_survives>=0),
 ADD COLUMN IF NOT EXISTS survival_round_wins INTEGER NOT NULL DEFAULT 0 CHECK(survival_round_wins>=0),
 ADD COLUMN IF NOT EXISTS current_survival_podium_streak INTEGER NOT NULL DEFAULT 0 CHECK(current_survival_podium_streak>=0),
 ADD COLUMN IF NOT EXISTS survival_podium_streak INTEGER NOT NULL DEFAULT 0 CHECK(survival_podium_streak>=current_survival_podium_streak),
 ADD COLUMN IF NOT EXISTS current_survival_win_streak INTEGER NOT NULL DEFAULT 0 CHECK(current_survival_win_streak>=0),
 ADD COLUMN IF NOT EXISTS survival_win_streak INTEGER NOT NULL DEFAULT 0 CHECK(survival_win_streak>=current_survival_win_streak),
 ADD COLUMN IF NOT EXISTS solo_gold_reached BOOLEAN NOT NULL DEFAULT FALSE,
 ADD COLUMN IF NOT EXISTS survival_gold_reached BOOLEAN NOT NULL DEFAULT FALSE;
CREATE TABLE IF NOT EXISTS ranked_survival_achievement_facts (
 player_id UUID NOT NULL REFERENCES ranked_players(id) ON DELETE CASCADE,
 match_id UUID NOT NULL REFERENCES sur_matches(id) ON DELETE CASCADE,
 completed_at TIMESTAMPTZ NOT NULL,
 facts JSONB NOT NULL CHECK(jsonb_typeof(facts)='object'),
 PRIMARY KEY(player_id,match_id)
);
CREATE INDEX IF NOT EXISTS ranked_survival_achievement_facts_order_idx ON ranked_survival_achievement_facts(player_id,completed_at,match_id);
ALTER TABLE ranked_achievement_events DROP CONSTRAINT IF EXISTS ranked_achievement_events_source_type_check;
ALTER TABLE ranked_achievement_events ADD CONSTRAINT ranked_achievement_events_source_type_check
 CHECK(source_type IN ('MATCH_RESULT','PLACEMENT_RESULT','BACKFILL','SHARED_TIER_REACHED','SURVIVAL_RESULT'));
DO $$ BEGIN
 IF NOT EXISTS(SELECT 1 FROM ranked_achievement_versions WHERE version=2) THEN
  -- Same profile-first lock order as settlement and Claim avoids cross-table deadlocks.
  PERFORM player_id FROM ranked_profiles ORDER BY player_id FOR UPDATE;
  DELETE FROM ranked_ap_ledger;
  DELETE FROM ranked_achievement_unlocks;
  DELETE FROM ranked_achievement_events;
  DELETE FROM ranked_survival_achievement_facts;
  DELETE FROM ranked_achievement_metrics;
  -- Remove stale Unlock toasts from retained result payloads, without changing
  -- their competitive/rating history. Placement resolves notices by event key.
  UPDATE ranked_matches SET rating_presentation=rating_presentation-'achievementUnlocks'
   WHERE jsonb_typeof(rating_presentation)='object' AND rating_presentation ? 'achievementUnlocks';
  UPDATE sur_online_players SET result=result-'achievementUnlocks' WHERE result ? 'achievementUnlocks';
  UPDATE sur_debug_matches SET result=result-'achievementUnlocks' WHERE result ? 'achievementUnlocks';
  UPDATE ranked_profiles SET total_ap=0,equipped_achievement_id=NULL,equipped_achievement_level=NULL;
  INSERT INTO ranked_achievement_versions(version,activated_at) VALUES(2,CURRENT_TIMESTAMP);
  -- Mark the reset as complete for existing accounts. A retained v1 instance
  -- must not lazily resurrect the pre-cutover match history during handover.
  INSERT INTO ranked_achievement_metrics(player_id,backfilled_at)
   SELECT id,CURRENT_TIMESTAMP FROM ranked_players ON CONFLICT(player_id) DO NOTHING;
 END IF;
END $$;
COMMIT;
