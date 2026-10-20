import {
  Inject,
  Injectable,
  Logger,
  type OnApplicationShutdown,
  type OnModuleInit,
} from "@nestjs/common";
import { readFile } from "node:fs/promises";
import { Pool, type PoolClient, type QueryResultRow } from "pg";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "../config/server-environment.js";
import type { DatabasePort, SqlExecutor, SqlResult } from "./database.port.js";
import { migrationConnectionString } from "./migration-connection.js";

// Earlier builds took this session lock through PgBouncer transaction pooling;
// an UNLOCK may run on another backend, stranding the original lock.
const MIGRATION_LOCK_KEY = "corum-ranked-schema-migrations-v2";

const REQUIRED_MIGRATIONS = [
  "0001_initial_ranked.sql",
  "0002_debug_bot_match.sql",
  "0002_attempt_start_leases.sql",
  "0003_debug_rating_playable_maps.sql",
  "0004_alpha37_schema_hardening.sql",
  "0005_v050_rating_system.sql",
  "0006_v050_dynamic_csmp_tiers.sql",
  "0007_solo_stage_placement.sql",
  "0008_placement_beta9.sql",
  "0009_placement_stage_count.sql",
  "0010_achievements_v1.sql",
  "0011_debug_spectate.sql",
  "0012_gameplay_controls.sql",
  "0013_native_scene_entry.sql",
  "0014_forum_records.sql",
  "0015_forum_refresh_cooldown.sql",
  "0016_admin_history_reset.sql",
  "0017_placement_rating_policy.sql",
  "0018_map_metadata_cache.sql",
  "0019_ranked_audio_volumes.sql",
  "0021_survival_m1.sql",
  "0022_survival_m3_engine.sql",
  "0023_survival_m4_debug.sql",
  "0024_survival_m5_online.sql",
  "0025_solo_tier_divisions.sql",
  "0026_survival_catalog_bot_count.sql",
  "0027_achievements_v2.sql",
  "0028_debug_achievement_parity.sql",
] as const;

export interface RankedSchemaStatus {
  readonly ready: boolean;
  readonly missing: readonly string[];
}

type SchemaProbeRow = QueryResultRow & {
  ranked_schema_migrations: boolean;
  ranked_matches: boolean;
  ranked_rounds: boolean;
  ranked_attempts: boolean;
  ranked_deathmatch_attempts: boolean;
  ranked_attempt_start_leases: boolean;
  ranked_placement_runs: boolean;
  ranked_placement_maps: boolean;
  ranked_placement_attempts: boolean;
  ranked_achievement_events: boolean;
  ranked_achievement_metrics: boolean;
  ranked_achievement_unlocks: boolean;
  ranked_ap_ledger: boolean;
  ranked_achievement_versions:boolean;
  ranked_survival_achievement_facts:boolean;
  sur_profiles: boolean;
  sur_matches: boolean;
  sur_match_players: boolean;
  sur_rounds: boolean;
  sur_round_players: boolean;
  sur_attempts: boolean;
  sur_tiebreaks: boolean;
  sur_tiebreak_attempts: boolean;
  sur_match_commands: boolean;
  sur_lp_ledger: boolean;
  sur_debug_matches: boolean;
  sur_mode_state: boolean;
  sur_queue_entries: boolean;
  sur_online_matches: boolean;
  sur_online_players: boolean;
  sur_queue_fill_requests: boolean;
  sur_request_rates: boolean;

  ranked_forum_records: boolean;
  ranked_forum_refresh_cooldowns: boolean;
  match_type: boolean;
  debug_config: boolean;
  match_entry_state: boolean;
  round_alternate_level_id: boolean;
  round_playable_level_id: boolean;
  attempt_played_level_id: boolean;
  deathmatch_attempt_played_level_id: boolean;
  placement_prepare_deadline_at: boolean;
  placement_completion_kind: boolean;
  placement_rating_policy_version: boolean;
  placement_skip_event: boolean;
  placement_skipped_at: boolean;
  profile_total_ap: boolean;
  profile_equipped_achievement: boolean;
  profile_equipped_achievement_level: boolean;
  profile_history_visible_since: boolean;
};

@Injectable()
export class DatabaseService implements DatabasePort, OnModuleInit, OnApplicationShutdown {
  private readonly logger = new Logger(DatabaseService.name);
  private readonly pool: Pool;
  private readonly migrationPool: Pool;

  public constructor(
    @Inject(SERVER_ENVIRONMENT) environment: ServerEnvironment,
  ) {
    this.pool = new Pool({
      connectionString: environment.databaseUrl,
      max: 5,
      idleTimeoutMillis: 30_000,
      connectionTimeoutMillis: 10_000,
      keepAlive: true,
    });
    // pg-pool emits idle-client failures on the Pool itself. Without an error
    // listener Node treats that event as uncaught and kills the entire Render
    // process when Neon rotates or drops a pooled TLS connection.
    this.pool.on("error", (error) => {
      this.logger.error(`Unexpected idle PostgreSQL connection error: ${error.message}`);
    });
    // Session-level advisory locks must be acquired and released on the same
    // PostgreSQL backend; Neon's transaction pooler does not guarantee that.
    // This separate direct-session pool is used for migrations only.
    this.migrationPool = new Pool({
      connectionString: migrationConnectionString(environment.databaseUrl),
      max: 1,
      idleTimeoutMillis: 5_000,
      connectionTimeoutMillis: 10_000,
      keepAlive: true,
    });
    this.migrationPool.on("error", (error) => {
      this.logger.error(`Unexpected idle migration connection error: ${error.message}`);
    });
  }

  public async onModuleInit(): Promise<void> {
    // Database schema is part of server compatibility. Never let Render report a
    // deploy as healthy while the running code references a migration that Neon
    // has not received yet.
    await this.applyRequiredMigrations();
    const schema = await this.schemaStatus();
    if (!schema.ready) {
      throw new Error(`Corum Ranked database schema is incomplete: ${schema.missing.join(", ")}`);
    }
  }

  public query<Row>(
    sql: string,
    values: readonly unknown[] = [],
  ): Promise<SqlResult<Row>> {
    return this.pool.query<QueryResultRow>(sql, [...values]).then((result) => ({
      rows: result.rows as Row[],
      rowCount: result.rowCount ?? 0,
    }));
  }

  public async transaction<T>(operation: (client: SqlExecutor) => Promise<T>): Promise<T> {
    const client = await this.pool.connect();
    try {
      await client.query("BEGIN");
      const result = await operation(new PgClientAdapter(client));
      await client.query("COMMIT");
      return result;
    } catch (error) {
      try {
        await client.query("ROLLBACK");
      } catch (rollbackError) {
        const message = rollbackError instanceof Error ? rollbackError.message : String(rollbackError);
        this.logger.warn(`PostgreSQL rollback failed after connection error: ${message}`);
      }
      throw error;
    } finally {
      client.release();
    }
  }

  public async ping(): Promise<boolean> {
    try {
      await this.pool.query("SELECT 1");
      return true;
    } catch {
      return false;
    }
  }

  public async schemaStatus(): Promise<RankedSchemaStatus> {
    try {
      const result = await this.pool.query<SchemaProbeRow>(`
        SELECT
          to_regclass('public.ranked_schema_migrations') IS NOT NULL AS ranked_schema_migrations,
          to_regclass('public.ranked_matches') IS NOT NULL AS ranked_matches,
          to_regclass('public.ranked_rounds') IS NOT NULL AS ranked_rounds,
          to_regclass('public.ranked_attempts') IS NOT NULL AS ranked_attempts,
          to_regclass('public.ranked_deathmatch_attempts') IS NOT NULL AS ranked_deathmatch_attempts,
          to_regclass('public.ranked_attempt_start_leases') IS NOT NULL AS ranked_attempt_start_leases,
          to_regclass('public.ranked_placement_runs') IS NOT NULL AS ranked_placement_runs,
          to_regclass('public.ranked_placement_maps') IS NOT NULL AS ranked_placement_maps,
          to_regclass('public.ranked_placement_attempts') IS NOT NULL AS ranked_placement_attempts,
          to_regclass('public.ranked_achievement_events') IS NOT NULL AS ranked_achievement_events,
          to_regclass('public.ranked_achievement_metrics') IS NOT NULL AS ranked_achievement_metrics,
          to_regclass('public.ranked_achievement_unlocks') IS NOT NULL AS ranked_achievement_unlocks,
          to_regclass('public.ranked_ap_ledger') IS NOT NULL AS ranked_ap_ledger,
          to_regclass('public.ranked_achievement_versions') IS NOT NULL AS ranked_achievement_versions,
          to_regclass('public.ranked_survival_achievement_facts') IS NOT NULL AS ranked_survival_achievement_facts,
          to_regclass('public.sur_profiles') IS NOT NULL AS sur_profiles,
          to_regclass('public.sur_matches') IS NOT NULL AS sur_matches,
          to_regclass('public.sur_match_players') IS NOT NULL AS sur_match_players,
          to_regclass('public.sur_rounds') IS NOT NULL AS sur_rounds,
          to_regclass('public.sur_round_players') IS NOT NULL AS sur_round_players,
          to_regclass('public.sur_attempts') IS NOT NULL AS sur_attempts,
          to_regclass('public.sur_tiebreaks') IS NOT NULL AS sur_tiebreaks,
          to_regclass('public.sur_tiebreak_attempts') IS NOT NULL AS sur_tiebreak_attempts,
          to_regclass('public.sur_match_commands') IS NOT NULL AS sur_match_commands,
          to_regclass('public.sur_lp_ledger') IS NOT NULL AS sur_lp_ledger,
          to_regclass('public.sur_debug_matches') IS NOT NULL AS sur_debug_matches,
          to_regclass('public.sur_mode_state') IS NOT NULL AS sur_mode_state,
          to_regclass('public.sur_queue_entries') IS NOT NULL AS sur_queue_entries,
          to_regclass('public.sur_online_matches') IS NOT NULL AS sur_online_matches,
          to_regclass('public.sur_online_players') IS NOT NULL AS sur_online_players,
          to_regclass('public.sur_queue_fill_requests') IS NOT NULL AS sur_queue_fill_requests,
          to_regclass('public.sur_request_rates') IS NOT NULL AS sur_request_rates,

          to_regclass('public.ranked_forum_records') IS NOT NULL AS ranked_forum_records,
          to_regclass('public.ranked_forum_refresh_cooldowns') IS NOT NULL AS ranked_forum_refresh_cooldowns,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_matches' AND column_name = 'match_type'
          ) AS match_type,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_matches' AND column_name = 'debug_config'
          ) AS debug_config,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_rounds' AND column_name = 'alternate_level_id'
          ) AS round_alternate_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_rounds' AND column_name = 'playable_level_id'
          ) AS round_playable_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_attempts' AND column_name = 'played_level_id'
          ) AS attempt_played_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_deathmatch_attempts' AND column_name = 'played_level_id'
          ) AS deathmatch_attempt_played_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_placement_maps' AND column_name = 'prepare_deadline_at'
          ) AS placement_prepare_deadline_at,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_placement_runs' AND column_name = 'completion_kind'
          ) AS placement_completion_kind,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_placement_runs' AND column_name = 'rating_policy_version'
          ) AS placement_rating_policy_version,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_profiles' AND column_name = 'total_ap'
          ) AS profile_total_ap,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_profiles' AND column_name = 'equipped_achievement_id'
          ) AS profile_equipped_achievement,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_profiles' AND column_name = 'equipped_achievement_level'
          ) AS profile_equipped_achievement_level,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_profiles' AND column_name = 'history_visible_since'
          ) AS profile_history_visible_since,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_matches' AND column_name = 'entry_state'
          ) AS match_entry_state,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_placement_maps' AND column_name = 'skip_client_event_id'
          ) AS placement_skip_event,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_placement_maps' AND column_name = 'skipped_at'
          ) AS placement_skipped_at
      `);
      const row = result.rows[0];
      if (!row) return { ready: false, missing: ["schema probe returned no row"] };

      const required: ReadonlyArray<readonly [keyof SchemaProbeRow, string]> = [
        ["ranked_schema_migrations", "table ranked_schema_migrations"],
        ["ranked_matches", "table ranked_matches"],
        ["ranked_rounds", "table ranked_rounds"],
        ["ranked_attempts", "table ranked_attempts"],
        ["ranked_deathmatch_attempts", "table ranked_deathmatch_attempts"],
        ["ranked_attempt_start_leases", "table ranked_attempt_start_leases"],
        ["ranked_placement_runs", "table ranked_placement_runs"],
        ["ranked_placement_maps", "table ranked_placement_maps"],
        ["ranked_placement_attempts", "table ranked_placement_attempts"],
        ["ranked_achievement_events", "table ranked_achievement_events"],
        ["ranked_achievement_metrics", "table ranked_achievement_metrics"],
        ["ranked_achievement_unlocks", "table ranked_achievement_unlocks"],
        ["ranked_ap_ledger", "table ranked_ap_ledger"],
        ["ranked_achievement_versions","table ranked_achievement_versions"],
        ["ranked_survival_achievement_facts","table ranked_survival_achievement_facts"],
        ["sur_profiles", "table sur_profiles"],
        ["sur_matches", "table sur_matches"],
        ["sur_match_players", "table sur_match_players"],
        ["sur_rounds", "table sur_rounds"],
        ["sur_round_players", "table sur_round_players"],
        ["sur_attempts", "table sur_attempts"],
        ["sur_tiebreaks", "table sur_tiebreaks"],
        ["sur_tiebreak_attempts", "table sur_tiebreak_attempts"],
        ["sur_match_commands", "table sur_match_commands"],
        ["sur_lp_ledger", "table sur_lp_ledger"],
        ["sur_debug_matches", "table sur_debug_matches"],
        ["sur_mode_state", "table sur_mode_state"],
        ["sur_queue_entries", "table sur_queue_entries"],
        ["sur_online_matches", "table sur_online_matches"],
        ["sur_online_players", "table sur_online_players"],
        ["sur_queue_fill_requests", "table sur_queue_fill_requests"],
        ["sur_request_rates", "table sur_request_rates"],

        ["ranked_forum_records", "table ranked_forum_records"],
        ["ranked_forum_refresh_cooldowns", "table ranked_forum_refresh_cooldowns"],
        ["match_type", "ranked_matches.match_type"],
        ["debug_config", "ranked_matches.debug_config"],
        ["match_entry_state", "ranked_matches.entry_state"],
        ["round_alternate_level_id", "ranked_rounds.alternate_level_id"],
        ["round_playable_level_id", "ranked_rounds.playable_level_id"],
        ["attempt_played_level_id", "ranked_attempts.played_level_id"],
        ["deathmatch_attempt_played_level_id", "ranked_deathmatch_attempts.played_level_id"],
        ["placement_prepare_deadline_at", "ranked_placement_maps.prepare_deadline_at"],
        ["placement_completion_kind", "ranked_placement_runs.completion_kind"],
        ["placement_rating_policy_version", "ranked_placement_runs.rating_policy_version"],
        ["placement_skip_event", "ranked_placement_maps.skip_client_event_id"],
        ["placement_skipped_at", "ranked_placement_maps.skipped_at"],
        ["profile_total_ap", "ranked_profiles.total_ap"],
        ["profile_equipped_achievement", "ranked_profiles.equipped_achievement_id"],
        ["profile_equipped_achievement_level", "ranked_profiles.equipped_achievement_level"],
      ];
      const missing = required.filter(([key]) => !row[key]).map(([, label]) => label);
      if (!row.profile_history_visible_since) missing.push("column ranked_profiles.history_visible_since");

      if (row.ranked_schema_migrations) {
        const applied = await this.pool.query<{ filename: string }>(
          "SELECT filename FROM ranked_schema_migrations WHERE filename = ANY($1::text[])",
          [[...REQUIRED_MIGRATIONS]],
        );
        const appliedNames = new Set(applied.rows.map((item) => item.filename));
        for (const filename of REQUIRED_MIGRATIONS) {
          if (!appliedNames.has(filename)) missing.push(`migration ${filename}`);
        }
      }

      return { ready: missing.length === 0, missing };
    } catch (error) {
      return {
        ready: false,
        missing: [error instanceof Error ? error.message : "schema probe failed"],
      };
    }
  }

  private async applyRequiredMigrations(): Promise<void> {
    const client = await this.migrationPool.connect();
    let lockHeld = false;
    try {
      // Render can briefly overlap old/new instances. A PostgreSQL advisory lock
      // guarantees only one process mutates the Neon schema at a time.
      await client.query("SELECT pg_advisory_lock(hashtext($1))", [MIGRATION_LOCK_KEY]);
      lockHeld = true;

      await client.query(`
        CREATE TABLE IF NOT EXISTS ranked_schema_migrations (
          filename TEXT PRIMARY KEY,
          applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
        )
      `);

      const baseline = await client.query<{ exists: boolean }>(
        "SELECT to_regclass('public.ranked_matches') IS NOT NULL AS exists",
      );
      if (baseline.rows[0]?.exists) {
        // Legacy alpha databases predate the ledger. Presence of ranked_matches
        // proves 0001 was applied; record it without re-running the non-trivial
        // initial schema script.
        await this.recordMigration(client, "0001_initial_ranked.sql");
      }

      for (const filename of REQUIRED_MIGRATIONS) {
        if (await this.migrationApplied(client, filename)) continue;
        this.logger.log(`Applying Ranked migration ${filename}`);
        await this.executeMigration(client, filename);
        await this.recordMigration(client, filename);
      }
    } catch (error) {
      // Migration files own their BEGIN/COMMIT boundary. ROLLBACK is harmless
      // when no transaction is active and ensures a failed script cannot leave a
      // transaction pinned to the advisory-lock connection.
      try {
        await client.query("ROLLBACK");
      } catch {
        // Ignore; the original migration error is the one that matters.
      }
      const message = error instanceof Error ? error.message : String(error);
      this.logger.error(`Ranked database migration failed: ${message}`);
      throw error;
    } finally {
      if (lockHeld) {
        try {
          await client.query("SELECT pg_advisory_unlock(hashtext($1))", [MIGRATION_LOCK_KEY]);
        } catch (error) {
          const message = error instanceof Error ? error.message : String(error);
          this.logger.warn(`Could not release Ranked migration lock: ${message}`);
        }
      }
      client.release();
    }
  }

  private async migrationApplied(client: PoolClient, filename: string): Promise<boolean> {
    const result = await client.query<{ exists: boolean }>(
      "SELECT EXISTS (SELECT 1 FROM ranked_schema_migrations WHERE filename = $1) AS exists",
      [filename],
    );
    return result.rows[0]?.exists === true;
  }

  private async recordMigration(client: PoolClient, filename: string): Promise<void> {
    await client.query(
      "INSERT INTO ranked_schema_migrations (filename) VALUES ($1) ON CONFLICT (filename) DO NOTHING",
      [filename],
    );
  }

  private async executeMigration(client: PoolClient, filename: string): Promise<void> {
    // Both src/database and dist/database are four directories below ranked/.
    // Render retains ranked/migrations next to the compiled apps/server/dist.
    const migrationUrl = new URL(`../../../../migrations/${filename}`, import.meta.url);
    const sql = await readFile(migrationUrl, "utf8");
    await client.query(sql);
  }

  public async onApplicationShutdown(): Promise<void> {
    await Promise.all([this.pool.end(), this.migrationPool.end()]);
  }
}

class PgClientAdapter implements SqlExecutor {
  public constructor(private readonly client: PoolClient) {}

  public query<Row>(sql: string, values: readonly unknown[] = []): Promise<SqlResult<Row>> {
    return this.client.query<QueryResultRow>(sql, [...values]).then((result) => ({
      rows: result.rows as Row[],
      rowCount: result.rowCount ?? 0,
    }));
  }
}

