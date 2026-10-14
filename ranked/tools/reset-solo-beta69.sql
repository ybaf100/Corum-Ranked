-- Explicit one-time operator action. Create a Neon backup before executing.
-- Never include this file in the automatic schema-migration list.
DO $reset$
DECLARE
  table_name text;
  protected_before jsonb := '{}'::jsonb;
  protected_after jsonb := '{}'::jsonb;
  fingerprint text;
  common_before jsonb;
  common_after jsonb;
BEGIN
  SET LOCAL lock_timeout='5s';
  LOCK TABLE ranked_players,ranked_profiles,ranked_matches,ranked_queue_entries,ranked_placement_runs IN SHARE ROW EXCLUSIVE MODE;
  IF EXISTS(SELECT 1 FROM ranked_matches WHERE state NOT IN ('MATCH_RESULT','CANCELLED'))
    OR EXISTS(SELECT 1 FROM ranked_placement_runs WHERE state NOT IN ('PLACEMENT_RESULT','CANCELLED'))
    OR EXISTS(SELECT 1 FROM ranked_queue_entries WHERE status='QUEUED') THEN
    RAISE EXCEPTION 'Solo reset refused: finish Solo matches/Placement and leave queues first';
  END IF;
  FOR table_name IN SELECT tablename FROM pg_tables WHERE schemaname='public'
    AND (left(tablename,4)='sur_' OR tablename LIKE 'ranked_achievement_%' OR tablename='ranked_ap_ledger') ORDER BY tablename LOOP
    EXECUTE format('LOCK TABLE %I IN SHARE MODE',table_name);
    EXECUTE format('SELECT md5(COALESCE(jsonb_agg(to_jsonb(t) ORDER BY to_jsonb(t)::text)::text,''[]'')) FROM %I t',table_name) INTO fingerprint;
    protected_before:=protected_before||jsonb_build_object(table_name,fingerprint);
  END LOOP;
  SELECT jsonb_agg(jsonb_build_array(player_id,total_ap,equipped_achievement_id,equipped_achievement_level) ORDER BY player_id)
    INTO common_before FROM ranked_profiles;
  DELETE FROM ranked_queue_entries;
  DELETE FROM ranked_match_tokens;
  DELETE FROM ranked_outbox_events WHERE aggregate_id IN (SELECT id FROM ranked_matches);
  DELETE FROM ranked_matches;
  DELETE FROM ranked_placement_runs;
  UPDATE ranked_profiles SET displayed_tier='UNRANKED',hidden_mmr=NULL,visible_ranked_score=NULL,
    placement_games=0,wins=0,losses=0,match_draws=0,initial_csmp_tier=NULL,initial_seed_mmr=NULL,seed_applied_at=NULL,
    result_streak=0,placement_results='[]'::jsonb,promotion_source_tier=NULL,promotion_target_tier=NULL,promotion_cutoff=NULL,
    promotion_games_required=NULL,promotion_wins_required=NULL,promotion_results='[]'::jsonb,demotion_warning=FALSE,
    history_visible_since=CURRENT_TIMESTAMP,updated_at=CURRENT_TIMESTAMP;
  FOR table_name IN SELECT jsonb_object_keys(protected_before) LOOP
    EXECUTE format('SELECT md5(COALESCE(jsonb_agg(to_jsonb(t) ORDER BY to_jsonb(t)::text)::text,''[]'')) FROM %I t',table_name) INTO fingerprint;
    protected_after:=protected_after||jsonb_build_object(table_name,fingerprint);
  END LOOP;
  SELECT jsonb_agg(jsonb_build_array(player_id,total_ap,equipped_achievement_id,equipped_achievement_level) ORDER BY player_id)
    INTO common_after FROM ranked_profiles;
  IF protected_before<>protected_after OR common_before IS DISTINCT FROM common_after THEN
    RAISE EXCEPTION 'Solo reset rolled back: Survival or common data changed';
  END IF;
END;
$reset$;
