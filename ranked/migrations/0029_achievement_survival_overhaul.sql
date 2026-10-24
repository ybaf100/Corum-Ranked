BEGIN;
-- Explicitly authorized one-time reset of achievement state only. The nullable
-- epoch marker makes re-executing this file safe even if the outer ledger write
-- failed after COMMIT. Existing v2 migrations remain immutable.
ALTER TABLE ranked_achievement_versions ADD COLUMN IF NOT EXISTS overhaul_activated_at TIMESTAMPTZ;
ALTER TABLE ranked_achievement_metrics
 ADD COLUMN IF NOT EXISTS ranked_rounds_played INTEGER NOT NULL DEFAULT 0 CHECK(ranked_rounds_played>=0),
 ADD COLUMN IF NOT EXISTS survival_rounds_played INTEGER NOT NULL DEFAULT 0 CHECK(survival_rounds_played>=0),
 ADD COLUMN IF NOT EXISTS survival_perfect_rounds INTEGER NOT NULL DEFAULT 0 CHECK(survival_perfect_rounds>=0);
ALTER TABLE sur_rounds ADD COLUMN IF NOT EXISTS achievement_checkpoint JSONB
 CHECK(achievement_checkpoint IS NULL OR jsonb_typeof(achievement_checkpoint)='object');
DO $$ BEGIN
 IF NOT EXISTS(SELECT 1 FROM ranked_achievement_versions WHERE version=2) THEN
  RAISE EXCEPTION 'Achievement v2 migration must run before the overhaul';
 END IF;
 IF EXISTS(SELECT 1 FROM ranked_achievement_versions WHERE version=2 AND overhaul_activated_at IS NULL) THEN
  PERFORM player_id FROM ranked_profiles ORDER BY player_id FOR UPDATE;
  DELETE FROM ranked_ap_ledger;
  DELETE FROM ranked_achievement_unlocks;
  DELETE FROM ranked_achievement_events;
  DELETE FROM ranked_survival_achievement_facts;
  DELETE FROM ranked_achievement_metrics;
  UPDATE ranked_profiles SET total_ap=0,equipped_achievement_id=NULL,equipped_achievement_level=NULL;
  -- Invalidate only achievement notices cached in retained result payloads.
  UPDATE ranked_matches SET rating_presentation=rating_presentation-'achievementUnlocks'
   WHERE jsonb_typeof(rating_presentation)='object' AND rating_presentation ? 'achievementUnlocks';
  UPDATE sur_online_players SET result=result-'achievementUnlocks' WHERE result ? 'achievementUnlocks';
  UPDATE sur_debug_matches SET result=result-'achievementUnlocks' WHERE result ? 'achievementUnlocks';
  UPDATE sur_rounds SET achievement_checkpoint=NULL WHERE achievement_checkpoint IS NOT NULL;
  UPDATE sur_matches SET snapshot=jsonb_set(snapshot,'{rounds}',
   (SELECT COALESCE(jsonb_agg(r-'achievementCheckpoint'),'[]'::jsonb) FROM jsonb_array_elements(snapshot->'rounds') r))
   WHERE jsonb_typeof(snapshot->'rounds')='array' AND EXISTS(
    SELECT 1 FROM jsonb_array_elements(snapshot->'rounds') r WHERE r ? 'achievementCheckpoint');
  UPDATE ranked_achievement_versions SET overhaul_activated_at=CURRENT_TIMESTAMP WHERE version=2;
  INSERT INTO ranked_achievement_metrics(player_id,backfilled_at)
   SELECT id,CURRENT_TIMESTAMP FROM ranked_players ON CONFLICT(player_id) DO NOTHING;
 END IF;
END $$;
COMMIT;
