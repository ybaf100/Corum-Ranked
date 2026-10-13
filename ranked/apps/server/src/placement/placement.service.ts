import {
  ConflictException,
  Inject,
  Injectable,
  NotFoundException,
  ServiceUnavailableException,
} from "@nestjs/common";
import {
  calculatePlacementMapScore,
  calculatePlacementRating,
  capPlacementRatingForStageCount,
  normalizePlacementRating,
  NORMALIZED_PLACEMENT_RATING_POLICY,
  calculatePlacementStageScore,
  finalizePlacementScore,
  mapPresentationFields,
  scoreAttempt,
  placementGradeForMapScore,
  selectPlacementReferenceMaps,
  selectPlacementStageMaps,
  type PlacementAttemptResult,
  type PlacementGrade,
  type PlacementMapScore,
  type PlacementPolicy,
  type PoolNumber,
  type RandomSource,
  type RankedMapSnapshot,
} from "@corum-ranked/rules";
import {
  ID_GENERATOR,
  RANDOM_SOURCE,
  SERVER_CLOCK,
  type IdGenerator,
  type ServerClock,
} from "../common/runtime.module.js";
import type { RankedConfigSnapshot } from "../config/ranked-config.document.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { resolveAttemptStartTime } from "../match/attempt-timing.js";
import { SessionService } from "../session/session.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import type {
  PlacementAttemptEndDto,
  PlacementAttemptProgressDto,
  PlacementAttemptStartDto,
  ReadyPlacementMapDto,
  SkipPlacementDto,
  SkipPlacementMapDto,
  StartPlacementDto,
} from "./placement.dto.js";
import { AchievementService } from "../achievement/achievement.service.js";

type PlacementState =
  | "POOL_SELECTION"
  | "MAP_PREPARE"
  | "MAP_PLAYING"
  | "MAP_SETTLING"
  | "MAP_RESULT"
  | "STAGE_RESULT"
  | "PLACEMENT_RESULT"
  | "CANCELLED";

interface PlacementRunRow {
  id: string;
  player_id: string;
  config_snapshot_id: string;
  config_generation: string;
  state: PlacementState;
  state_version: number;
  starting_pool: number;
  stage_count: number;
  rating_policy_version: "LEGACY_CAP_V1" | "NORMALIZED_V1";
  current_stage_index: number;
  current_map_index: number;
  stage_pools: unknown;
  map_snapshots: unknown;
  stage_results: unknown;
  placement_rating: number | null;
  final_hidden_mmr: number | null;
  final_visible_ranked_score: number | null;
  final_displayed_tier: string | null;
  last_heartbeat_at: Date | string;
  deadline_at: Date | string | null;
  created_at: Date | string;
  started_at: Date | string | null;
  finalized_at: Date | string | null;
  result_acknowledged_at: Date | string | null;
  completion_kind: "COMPLETED" | "SKIPPED" | null;
  skipped_at: Date | string | null;
}

interface PlacementMapRow {
  id: string;
  run_id: string;
  stage_index: number;
  map_index: number;
  level_id: string;
  canonical_level_id: string;
  alternate_level_id: string | null;
  playable_level_id: string;
  title: string;
  creator: string;
  difficulty: string;
  pool: number;
  qualifying_percent: number;
  state: "MAP_PREPARE" | "MAP_PLAYING" | "MAP_SETTLING" | "MAP_RESULT";
  state_version: number;
  started_at: Date | string | null;
  normal_end_at: Date | string | null;
  final_window_end_at: Date | string | null;
  settled_at: Date | string | null;
  result_deadline_at: Date | string | null;
  prepare_started_at: Date | string | null;
  prepare_deadline_at: Date | string | null;
  ready_at: Date | string | null;
  clear_count: number;
  top_attempts: unknown;
  map_score: number;
  map_grade: PlacementGrade;
  skip_client_event_id: string | null;
  skipped_at: Date | string | null;
}

interface PlacementAttemptRow {
  id: string;
  domain_attempt_id: string;
  client_start_event_id: string;
  client_end_event_id: string | null;
  progress_percent: number | null;
  cleared: boolean;
  awarded_score: number;
  valid: boolean;
  ended_at: Date | string | null;
}

interface ProfileRow {
  placement_games: number;
  displayed_tier: string;
}

interface ConfigSnapshotRow {
  source_payload: unknown;
}

const asArray = (value: unknown): unknown[] => {
  if (Array.isArray(value)) return value;
  if (typeof value === "string") {
    try {
      const parsed = JSON.parse(value) as unknown;
      return Array.isArray(parsed) ? parsed : [];
    } catch {
      return [];
    }
  }
  return [];
};

const asObject = (value: unknown): Record<string, unknown> => {
  if (typeof value === "string") {
    try { return JSON.parse(value) as Record<string, unknown>; } catch { return {}; }
  }
  return value && typeof value === "object" ? value as Record<string, unknown> : {};
};

const iso = (value: Date | string | null): string | null =>
  value === null ? null : new Date(value).toISOString();

@Injectable()
export class PlacementService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    @Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    private readonly config: RankedConfigService,
    private readonly sessions: SessionService,
    private readonly achievements: AchievementService,
  ) {}

  public async active(session: RankedSessionContext) {
    const result = await this.database.query<PlacementRunRow>(
      `SELECT * FROM ranked_placement_runs
       WHERE player_id = $1 AND state <> 'CANCELLED'
         AND (state <> 'PLACEMENT_RESULT' OR result_acknowledged_at IS NULL)
       ORDER BY created_at DESC LIMIT 1`,
      [session.playerId],
    );
    const run = result.rows[0];
    if (!run) {
      const snapshot = this.config.getSnapshot();
      const placementGamesRequired = snapshot.operational.mmrPolicy?.placementGames ?? 5;
      const profileResult = await this.database.query<ProfileRow>(
        "SELECT placement_games, displayed_tier FROM ranked_profiles WHERE player_id = $1",
        [session.playerId],
      );
      const profile = profileResult.rows[0];
      const completed = profile !== undefined &&
        Number(profile.placement_games) >= placementGamesRequired &&
        profile.displayed_tier !== "UNRANKED";
      const recentResult = await this.database.query<PlacementRunRow>(
        `SELECT * FROM ranked_placement_runs
         WHERE player_id = $1 AND state <> 'CANCELLED'
         ORDER BY created_at DESC LIMIT 1`,
        [session.playerId],
      );
      const recentRun = recentResult.rows[0];
      return {
        active: false,
        action: completed ? "RANKED_QUEUE" : "START_PLACEMENT",
        referenceMaps: completed ? [] : selectPlacementReferenceMaps(snapshot.maps),
        stageResults: recentRun ? asArray(recentRun.stage_results) : [],
        configGeneration: snapshot.generation,
        serverNow: this.clock.now().toISOString(),
      };
    }
    return { active: true, action: "RESUME_PLACEMENT", ...(await this.state(run.id, session)) };
  }

  public async start(session: RankedSessionContext, body: StartPlacementDto) {
    this.sessions.assertEnvironment(body.installedMods);
    const snapshot = this.config.getSnapshot();
    const policy = snapshot.operational.placementPolicy;
    const mmrPolicy = snapshot.operational.mmrPolicy;
    if (!snapshot.operational.enabled || !policy || !mmrPolicy || !snapshot.operational.timeouts) {
      throw new ServiceUnavailableException("Placement is not operationally configured");
    }
    const now = this.clock.now();
    const runId = await this.database.transaction(async (transaction) => {
      await transaction.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
      const profileResult = await transaction.query<ProfileRow>(
        "SELECT placement_games, displayed_tier FROM ranked_profiles WHERE player_id = $1 FOR UPDATE",
        [session.playerId],
      );
      const profile = profileResult.rows[0];
      if (!profile) throw new ConflictException("Ranked profile is not seeded");
      if (Number(profile.placement_games) >= mmrPolicy.placementGames || profile.displayed_tier !== "UNRANKED") {
        throw new ConflictException({ code: "PLACEMENT_ALREADY_COMPLETED" });
      }
      const active = await transaction.query<{ id: string }>(
        `SELECT id FROM ranked_placement_runs
         WHERE player_id = $1 AND state NOT IN ('PLACEMENT_RESULT', 'CANCELLED')
         LIMIT 1 FOR UPDATE`,
        [session.playerId],
      );
      if (active.rows[0]) return active.rows[0].id;
      await this.assertNoPvPActivity(transaction, session.playerId);
      const configSnapshotId = await this.persistConfigSnapshot(transaction, snapshot);
      const firstMaps = selectPlacementStageMaps(body.startingPool, snapshot.maps, [], this.random);
      const id = this.ids.next();
      const serializedMaps = firstMaps.map((map, index) => ({
        stageIndex: 1,
        mapIndex: index + 1,
        ...map,
      }));
      await transaction.query(
        `INSERT INTO ranked_placement_runs (
           id, player_id, config_snapshot_id, config_generation, state,
           starting_pool, stage_count, current_stage_index, current_map_index, stage_pools,
           map_snapshots, last_heartbeat_at, deadline_at, started_at, rating_policy_version
         ) VALUES ($1, $2, $3, $4, 'POOL_SELECTION', $5, $6, 1, 1, $7::jsonb, $8::jsonb, $9, NULL, $9, $10)`,
        [
          id,
          session.playerId,
          configSnapshotId,
          snapshot.generation,
          body.startingPool,
          body.stageCount,
          JSON.stringify([body.startingPool]),
          JSON.stringify(serializedMaps),
          now.toISOString(),
          NORMALIZED_PLACEMENT_RATING_POLICY,
        ],
      );
      await this.insertStageMaps(transaction, id, 1, firstMaps);
      await this.beginMapPrepare(transaction, id, 1, 1, now, policy);
      return id;
    });
    return this.state(runId, session);
  }

  public async skip(session: RankedSessionContext, body: SkipPlacementDto) {
    this.sessions.assertEnvironment(body.installedMods);
    const snapshot = this.config.getSnapshot();
    const mmrPolicy = snapshot.operational.mmrPolicy;
    if (!snapshot.operational.enabled || !snapshot.operational.placementPolicy || !mmrPolicy) {
      throw new ServiceUnavailableException("Placement is not operationally configured");
    }
    const now = this.clock.now();
    const runId = await this.database.transaction(async (transaction) => {
      const profileResult = await transaction.query<ProfileRow>(
        "SELECT placement_games, displayed_tier FROM ranked_profiles WHERE player_id = $1 FOR UPDATE",
        [session.playerId],
      );
      const profile = profileResult.rows[0];
      if (!profile) throw new ConflictException("Ranked profile is not seeded");
      const previous = await transaction.query<{ id: string }>(
        `SELECT id FROM ranked_placement_runs
         WHERE player_id = $1 AND completion_kind = 'SKIPPED'
           AND result_acknowledged_at IS NULL
         ORDER BY created_at DESC LIMIT 1 FOR UPDATE`,
        [session.playerId],
      );
      if (previous.rows[0]) return previous.rows[0].id;
      if (Number(profile.placement_games) >= mmrPolicy.placementGames || profile.displayed_tier !== "UNRANKED") {
        throw new ConflictException({ code: "PLACEMENT_ALREADY_COMPLETED" });
      }
      const active = await transaction.query<{ id: string }>(
        `SELECT id FROM ranked_placement_runs
         WHERE player_id = $1 AND state NOT IN ('PLACEMENT_RESULT', 'CANCELLED')
         LIMIT 1 FOR UPDATE`,
        [session.playerId],
      );
      if (active.rows[0]) throw new ConflictException("An active Placement Run cannot be skipped");
      await this.assertNoPvPActivity(transaction, session.playerId);
      const configSnapshotId = await this.persistConfigSnapshot(transaction, snapshot);
      const finalized = finalizePlacementScore(150, snapshot.operational.tierBands);
      const id = this.ids.next();
      await transaction.query(
        `INSERT INTO ranked_placement_runs (
           id, player_id, config_snapshot_id, config_generation, state,
           starting_pool, current_stage_index, current_map_index, stage_pools,
           map_snapshots, stage_results, placement_rating, final_hidden_mmr,
           final_visible_ranked_score, final_displayed_tier, last_heartbeat_at,
           created_at, started_at, finalized_at, completion_kind, skipped_at
         ) VALUES (
           $1, $2, $3, $4, 'PLACEMENT_RESULT', 1, 1, 1, '[]'::jsonb,
           '[]'::jsonb, '[]'::jsonb, 150, 150, $5, $6, $7,
           $7, $7, $7, 'SKIPPED', $7
         )`,
        [
          id,
          session.playerId,
          configSnapshotId,
          snapshot.generation,
          finalized.visibleRankedScore,
          finalized.displayedTier,
          now.toISOString(),
        ],
      );
      await transaction.query(
        `UPDATE ranked_profiles
         SET hidden_mmr = 150, visible_ranked_score = $1, displayed_tier = $2,
             placement_games = $3, placement_results = '[]'::jsonb,
             result_streak = 0, promotion_source_tier = NULL, promotion_target_tier = NULL,
             promotion_cutoff = NULL, promotion_games_required = NULL,
             promotion_wins_required = NULL, promotion_results = '[]'::jsonb,
             demotion_warning = FALSE, updated_at = $4
         WHERE player_id = $5`,
        [
          finalized.visibleRankedScore,
          finalized.displayedTier,
          mmrPolicy.placementGames,
          now.toISOString(),
          session.playerId,
        ],
      );
      return id;
    });
    return this.loadPublicState(runId, session.playerId, now);
  }

  public async state(runId: string, session: RankedSessionContext) {
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      if (run.state !== "PLACEMENT_RESULT" && run.state !== "CANCELLED") {
        // Authenticated state polling is the normal client heartbeat. Persist it
        // in the same transaction as deadline advancement so a restart never
        // observes a newer phase with an older recovery liveness marker.
        await transaction.query(
          "UPDATE ranked_placement_runs SET last_heartbeat_at = $1 WHERE id = $2",
          [now.toISOString(), run.id],
        );
      }
      if (run.state === "MAP_PREPARE") {
        const map = await this.lockCurrentMap(transaction, run);
        await this.advancePreparedMap(transaction, run, map, now);
      } else if (run.state === "MAP_PLAYING" || run.state === "MAP_SETTLING") {
        const map = await this.lockCurrentMap(transaction, run);
        await this.advanceExpiredMap(transaction, run, map, now);
      }
    });
    return this.loadPublicState(runId, session.playerId, now);
  }

  public async ready(
    runId: string,
    session: RankedSessionContext,
    body: ReadyPlacementMapDto,
  ) {
    this.sessions.assertEnvironment(body.installedMods);
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      if (run.state === "MAP_PLAYING") return;
      if (run.state !== "MAP_PREPARE") {
        throw new ConflictException(`Placement is ${run.state}, not MAP_PREPARE`);
      }
      const map = await this.lockCurrentMap(transaction, run);
      await transaction.query(
        `UPDATE ranked_placement_maps
         SET ready_at = COALESCE(ready_at, $1), state_version = state_version + 1
         WHERE id = $2`,
        [now.toISOString(), map.id],
      );
      await this.advancePreparedMap(transaction, run, { ...map, ready_at: map.ready_at ?? now }, now);
    });
    return this.loadPublicState(runId, session.playerId, now);
  }

  public async startAttempt(
    runId: string,
    session: RankedSessionContext,
    body: PlacementAttemptStartDto,
  ) {
    const now = this.clock.now();
    let attemptId = "";
    let duplicate = false;
    await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      if (run.state !== "MAP_PLAYING") {
        throw new ConflictException("Placement Map is not accepting attempts");
      }
      const map = await this.lockCurrentMap(transaction, run);
      if (body.levelId !== map.playable_level_id) {
        throw new ConflictException("Attempt Level ID does not match the Placement map snapshot");
      }
      const existing = await transaction.query<{ id: string }>(
        "SELECT id FROM ranked_placement_attempts WHERE map_id = $1 AND client_start_event_id = $2",
        [map.id, body.clientEventId],
      );
      if (existing.rows[0]) {
        attemptId = existing.rows[0].id;
        duplicate = true;
        return;
      }
      const startedAt = resolveAttemptStartTime(body.clientStartedAt, now);
      const finalEndMs = map.final_window_end_at ? new Date(map.final_window_end_at).getTime() : 0;
      if (!finalEndMs || startedAt.getTime() >= finalEndMs) {
        throw new ConflictException("Placement attempt start window is closed");
      }
      const active = await transaction.query<{ id: string }>(
        "SELECT id FROM ranked_placement_attempts WHERE map_id = $1 AND ended_at IS NULL AND valid = TRUE",
        [map.id],
      );
      if (active.rows[0]) throw new ConflictException("A Placement attempt is already active");
      const sequenceResult = await transaction.query<{ next_sequence: number }>(
        "SELECT COALESCE(MAX(attempt_sequence), 0) + 1 AS next_sequence FROM ranked_placement_attempts WHERE map_id = $1",
        [map.id],
      );
      const sequence = Number(sequenceResult.rows[0]?.next_sequence ?? 1);
      attemptId = this.ids.next();
      await transaction.query(
        `INSERT INTO ranked_placement_attempts (
           id, run_id, map_id, player_id, attempt_sequence, domain_attempt_id,
           client_start_event_id, played_level_id, server_accepted_start_at, client_started_at
         ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)`,
        [
          attemptId,
          run.id,
          map.id,
          session.playerId,
          sequence,
          `P-${sequence}`,
          body.clientEventId,
          map.playable_level_id,
          startedAt.toISOString(),
          body.clientStartedAt ?? null,
        ],
      );
    });
    return {
      accepted: true,
      duplicate,
      attemptId,
      gradeChanged: false,
      serverNow: now.toISOString(),
    };
  }

  public async updateAttemptProgress(
    runId: string,
    session: RankedSessionContext,
    body: PlacementAttemptProgressDto,
  ) {
    const now = this.clock.now();
    const result = await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      const map = await this.lockCurrentMap(transaction, run);
      if (body.levelId !== map.playable_level_id) {
        throw new ConflictException("Progress Level ID does not match the Placement map snapshot");
      }
      const updated = await transaction.query<{ latest_progress_percent: number }>(
        `UPDATE ranked_placement_attempts
         SET latest_progress_percent = GREATEST(latest_progress_percent, $1)
         WHERE id = $2 AND map_id = $3 AND ended_at IS NULL AND valid = TRUE
         RETURNING latest_progress_percent`,
        [body.progressPercent, body.attemptId, map.id],
      );
      if (updated.rowCount === 0) throw new NotFoundException("Active Placement attempt not found");
      const progressPercent = Number(updated.rows[0]?.latest_progress_percent ?? body.progressPercent);
      const snapshot = await this.loadSnapshot(transaction, run.config_snapshot_id);
      const policy = this.requirePlacementPolicy(snapshot);
      const attempts = await this.completedAttemptEvidence(transaction, map.id);
      const provisionalAttempt = scoreAttempt(
        progressPercent,
        false,
        Number(map.qualifying_percent),
      );
      const provisional = calculatePlacementMapScore([
        ...attempts,
        {
          attemptId: `provisional:${body.attemptId}`,
          progressPercent,
          cleared: false,
          valid: true,
          awardedScore: provisionalAttempt,
        },
      ], policy);
      return {
        mapScore: Number(map.map_score),
        provisionalMapScore: provisional.mapScore,
        mapGrade: map.map_grade,
        provisionalMapGrade: placementGradeForMapScore(provisional.mapScore, policy, Number(map.clear_count)),
      };
    });
    return {
      ...result,
      gradeChanged: result.provisionalMapGrade !== result.mapGrade,
      serverNow: now.toISOString(),
    };
  }

  public async endAttempt(
    runId: string,
    session: RankedSessionContext,
    body: PlacementAttemptEndDto,
  ) {
    const now = this.clock.now();
    let transition: Record<string, unknown> = {};
    await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      if (run.state !== "MAP_PLAYING" && run.state !== "MAP_SETTLING") {
        const duplicate = await transaction.query<{ id: string }>(
          "SELECT id FROM ranked_placement_attempts WHERE run_id = $1 AND client_end_event_id = $2",
          [run.id, body.clientEventId],
        );
        if (duplicate.rows[0]) {
          transition = { accepted: true, duplicate: true, gradeChanged: false };
          return;
        }
        throw new ConflictException("Placement Map is not accepting attempt results");
      }
      const snapshot = await this.loadSnapshot(transaction, run.config_snapshot_id);
      const policy = this.requirePlacementPolicy(snapshot);
      const map = await this.lockCurrentMap(transaction, run);
      if (body.levelId !== map.playable_level_id) {
        throw new ConflictException("Attempt Level ID does not match the Placement map snapshot");
      }
      const duplicate = await transaction.query<{ id: string }>(
        "SELECT id FROM ranked_placement_attempts WHERE map_id = $1 AND client_end_event_id = $2",
        [map.id, body.clientEventId],
      );
      if (duplicate.rows[0]) {
        transition = { accepted: true, duplicate: true, gradeChanged: false };
        return;
      }
      const attempt = await transaction.query<PlacementAttemptRow>(
        `SELECT * FROM ranked_placement_attempts
         WHERE id = $1 AND map_id = $2 AND player_id = $3 FOR UPDATE`,
        [body.attemptId, map.id, session.playerId],
      );
      const active = attempt.rows[0];
      if (!active || active.ended_at || !active.valid) {
        throw new NotFoundException("Active Placement attempt not found");
      }
      const awardedScore = scoreAttempt(body.progressPercent, body.cleared, Number(map.qualifying_percent));
      await transaction.query(
        `UPDATE ranked_placement_attempts
         SET ended_at = $1, client_ended_at = $2, progress_percent = $3::numeric,
             latest_progress_percent = GREATEST(latest_progress_percent, FLOOR($3::numeric)::integer),
             cleared = $4, awarded_score = $5, client_end_event_id = $6
         WHERE id = $7`,
        [
          now.toISOString(),
          body.clientEndedAt ?? null,
          body.progressPercent,
          body.cleared,
          awardedScore,
          body.clientEventId,
          active.id,
        ],
      );
      const beforeGrade = map.map_grade;
      const score = await this.recalculateMap(transaction, map, policy);
      const deadlinePassed = map.final_window_end_at !== null &&
        now.getTime() >= new Date(map.final_window_end_at).getTime();
      if (score.completedByClearTarget || deadlinePassed) {
        await this.settleMap(transaction, run, map, score, now, snapshot);
      }
      transition = {
        accepted: true,
        duplicate: false,
        attemptFinalized: true,
        mapScore: score.mapScore,
        mapGrade: score.grade,
        clearCount: score.clearCount,
        gradeChanged: beforeGrade !== score.grade,
        gradeTransition: beforeGrade === score.grade
          ? "NONE"
          : this.gradeRank(score.grade) < this.gradeRank(beforeGrade) ? "UP" : "DOWN",
        mapCompleted: score.completedByClearTarget || deadlinePassed,
      };
    });
    return { ...transition, serverNow: now.toISOString() };
  }

  public async skipMap(runId: string, session: RankedSessionContext, body: SkipPlacementMapDto) {
    const now = this.clock.now();
    let duplicate = false;
    await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      const previous = await transaction.query<PlacementMapRow>(
        "SELECT * FROM ranked_placement_maps WHERE run_id = $1 AND skip_client_event_id = $2",
        [run.id, body.clientEventId],
      );
      const replay = previous.rows[0];
      if (replay && (Number(replay.stage_index) !== body.stageIndex ||
          Number(replay.map_index) !== body.mapIndex || replay.playable_level_id !== body.levelId)) {
        throw new ConflictException("Placement skip event belongs to another map");
      }
      const target = await transaction.query<PlacementMapRow>(
        `SELECT * FROM ranked_placement_maps
         WHERE run_id = $1 AND stage_index = $2 AND map_index = $3 FOR UPDATE`,
        [run.id, body.stageIndex, body.mapIndex],
      );
      const map = target.rows[0];
      if (!map || map.playable_level_id !== body.levelId) {
        throw new ConflictException("Placement skip does not match the exact map snapshot");
      }
      // A delayed retry must acknowledge its old map, never skip the next map.
      if (map.skip_client_event_id === body.clientEventId) { duplicate = true; return; }
      if (Number(run.current_stage_index) !== body.stageIndex || Number(run.current_map_index) !== body.mapIndex) {
        throw new ConflictException("Placement skip belongs to an earlier map");
      }
      if (run.state === "MAP_RESULT" && map.state === "MAP_RESULT") { duplicate = true; return; }
      if (run.state !== "MAP_PLAYING" && run.state !== "MAP_SETTLING") {
        throw new ConflictException("Placement Map is not accepting a skip");
      }
      const active = await transaction.query<{ id: string }>(
        "SELECT id FROM ranked_placement_attempts WHERE map_id = $1 AND ended_at IS NULL AND valid = TRUE",
        [map.id],
      );
      // The client freezes and delivers its current Attempt End before this
      // request. Never drop an in-flight attempt or score unacknowledged progress.
      if (active.rows[0]) throw new ConflictException("Sync the active Placement attempt before skipping");
      const snapshot = await this.loadSnapshot(transaction, run.config_snapshot_id);
      const policy = this.requirePlacementPolicy(snapshot);
      const score = await this.recalculateMap(transaction, map, policy);
      await transaction.query(
        "UPDATE ranked_placement_maps SET skip_client_event_id = $1, skipped_at = $2 WHERE id = $3",
        [body.clientEventId, now.toISOString(), map.id],
      );
      await this.settleMap(transaction, run, map, score, now, snapshot);
    });
    return {
      ...await this.loadPublicState(runId, session.playerId, now),
      accepted: true,
      duplicate,
      skippedMap: { stageIndex: body.stageIndex, mapIndex: body.mapIndex, levelId: body.levelId },
    };
  }

  public async continueResult(runId: string, session: RankedSessionContext) {
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const run = await this.lockRun(transaction, runId, session.playerId);
      const snapshot = await this.loadSnapshot(transaction, run.config_snapshot_id);
      const policy = this.requirePlacementPolicy(snapshot);
      if (run.state === "MAP_RESULT") {
        if (Number(run.current_map_index) === 1) {
          await transaction.query(
            `UPDATE ranked_placement_runs
             SET state = 'MAP_PREPARE', current_map_index = 2,
                 state_version = state_version + 1, deadline_at = NULL
             WHERE id = $1`,
            [run.id],
          );
          await this.beginMapPrepare(transaction, run.id, Number(run.current_stage_index), 2, now, policy);
          return;
        }
        const maps = await this.stageMaps(transaction, run.id, Number(run.current_stage_index));
        const stage = calculatePlacementStageScore(
          [this.mapScoreFromRow(maps[0]!), this.mapScoreFromRow(maps[1]!)],
          Number(maps[0]!.pool) as PoolNumber,
          policy,
        );
        const results = asArray(run.stage_results);
        results.push({
          stageIndex: Number(run.current_stage_index),
          pool: Number(maps[0]!.pool),
          mapScores: maps.map((map) => Number(map.map_score)),
          mapGrades: maps.map((map) => map.map_grade),
          stageScore: stage.stageScore,
          stageGrade: stage.grade,
          poolDelta: stage.poolDelta,
          nextPool: stage.nextPool,
        });
        await transaction.query(
          `UPDATE ranked_placement_runs
           SET state = 'STAGE_RESULT', state_version = state_version + 1,
               stage_results = $1::jsonb, deadline_at = NULL
           WHERE id = $2`,
          [JSON.stringify(results), run.id],
        );
        return;
      }
      if (run.state === "STAGE_RESULT") {
        if (Number(run.current_stage_index) >= Number(run.stage_count)) {
          await this.finalizePlacement(transaction, run, snapshot, policy, now);
          return;
        }
        const results = asArray(run.stage_results);
        const latest = asObject(results.at(-1));
        const nextPool = Number(latest.nextPool) as PoolNumber;
        const nextStage = Number(run.current_stage_index) + 1;
        const used = await transaction.query<{ canonical_level_id: string }>(
          "SELECT canonical_level_id FROM ranked_placement_maps WHERE run_id = $1",
          [run.id],
        );
        const maps = selectPlacementStageMaps(
          nextPool,
          snapshot.maps,
          used.rows.map((row) => row.canonical_level_id),
          this.random,
        );
        await this.insertStageMaps(transaction, run.id, nextStage, maps);
        const snapshots = asArray(run.map_snapshots);
        snapshots.push(...maps.map((map, index) => ({
          stageIndex: nextStage,
          mapIndex: index + 1,
          ...map,
        })));
        const pools = asArray(run.stage_pools);
        pools.push(nextPool);
        await transaction.query(
          `UPDATE ranked_placement_runs
           SET state = 'MAP_PREPARE', state_version = state_version + 1,
               current_stage_index = $1, current_map_index = 1,
               stage_pools = $2::jsonb, map_snapshots = $3::jsonb,
               deadline_at = NULL
           WHERE id = $4`,
          [
            nextStage,
            JSON.stringify(pools),
            JSON.stringify(snapshots),
            run.id,
          ],
        );
        await this.beginMapPrepare(transaction, run.id, nextStage, 1, now, policy);
        return;
      }
      if (run.state !== "PLACEMENT_RESULT") {
        throw new ConflictException(`Placement cannot continue from ${run.state}`);
      }
    });
    return this.loadPublicState(runId, session.playerId, now);
  }

  public async heartbeat(runId: string, session: RankedSessionContext) {
    const now = this.clock.now();
    const updated = await this.database.query(
      `UPDATE ranked_placement_runs SET last_heartbeat_at = $1
       WHERE id = $2 AND player_id = $3 AND state NOT IN ('PLACEMENT_RESULT', 'CANCELLED')`,
      [now.toISOString(), runId, session.playerId],
    );
    if (updated.rowCount === 0) await this.assertRunExists(runId, session.playerId);
    return { serverNow: now.toISOString() };
  }

  public async acknowledgeResult(runId: string, session: RankedSessionContext) {
    const now = this.clock.now();
    const updated = await this.database.query(
      `UPDATE ranked_placement_runs
       SET result_acknowledged_at = COALESCE(result_acknowledged_at, $1)
       WHERE id = $2 AND player_id = $3 AND state = 'PLACEMENT_RESULT'`,
      [now.toISOString(), runId, session.playerId],
    );
    if (updated.rowCount === 0) {
      const run = await this.database.query<PlacementRunRow>(
        "SELECT * FROM ranked_placement_runs WHERE id = $1 AND player_id = $2",
        [runId, session.playerId],
      );
      if (!run.rows[0]) throw new NotFoundException("Placement Run not found");
      if (run.rows[0].state !== "PLACEMENT_RESULT") {
        throw new ConflictException("Placement result is not finalized");
      }
    }
    return { acknowledged: true, serverNow: now.toISOString() };
  }

  private async advanceExpiredMap(
    transaction: SqlExecutor,
    run: PlacementRunRow,
    map: PlacementMapRow,
    now: Date,
  ): Promise<void> {
    if (!map.final_window_end_at || now.getTime() < new Date(map.final_window_end_at).getTime()) return;
    const active = await transaction.query<{ id: string }>(
      "SELECT id FROM ranked_placement_attempts WHERE map_id = $1 AND ended_at IS NULL AND valid = TRUE",
      [map.id],
    );
    if (active.rows[0]) {
      if (run.state !== "MAP_SETTLING") {
        await transaction.query(
          "UPDATE ranked_placement_runs SET state = 'MAP_SETTLING', state_version = state_version + 1 WHERE id = $1",
          [run.id],
        );
        await transaction.query(
          "UPDATE ranked_placement_maps SET state = 'MAP_SETTLING', state_version = state_version + 1 WHERE id = $1",
          [map.id],
        );
      }
      return;
    }
    const snapshot = await this.loadSnapshot(transaction, run.config_snapshot_id);
    const score = await this.recalculateMap(transaction, map, this.requirePlacementPolicy(snapshot));
    await this.settleMap(transaction, run, map, score, now, snapshot);
  }

  private async advancePreparedMap(
    transaction: SqlExecutor,
    run: PlacementRunRow,
    map: PlacementMapRow,
    now: Date,
  ): Promise<void> {
    if (!map.ready_at || !map.prepare_deadline_at ||
      now.getTime() < new Date(map.prepare_deadline_at).getTime()) return;
    const snapshot = await this.loadSnapshot(transaction, run.config_snapshot_id);
    const policy = this.requirePlacementPolicy(snapshot);
    const normalEnd = new Date(now.getTime() + policy.roundSeconds * 1_000);
    const finalEnd = new Date(normalEnd.getTime() + policy.finalAttemptWindowSeconds * 1_000);
    await transaction.query(
      `UPDATE ranked_placement_maps
       SET state = 'MAP_PLAYING', state_version = state_version + 1,
           started_at = COALESCE(started_at, $1), normal_end_at = COALESCE(normal_end_at, $2),
           final_window_end_at = COALESCE(final_window_end_at, $3)
       WHERE id = $4 AND state = 'MAP_PREPARE'`,
      [now.toISOString(), normalEnd.toISOString(), finalEnd.toISOString(), map.id],
    );
    await transaction.query(
      `UPDATE ranked_placement_runs
       SET state = 'MAP_PLAYING', state_version = state_version + 1,
           last_heartbeat_at = $1, deadline_at = $2
       WHERE id = $3 AND state = 'MAP_PREPARE'`,
      [now.toISOString(), finalEnd.toISOString(), run.id],
    );
  }

  private async beginMapPrepare(
    transaction: SqlExecutor,
    runId: string,
    stageIndex: number,
    mapIndex: number,
    now: Date,
    policy: PlacementPolicy,
  ): Promise<void> {
    const deadline = new Date(now.getTime() + policy.prepareSeconds * 1_000);
    await transaction.query(
      `UPDATE ranked_placement_maps
       SET state = 'MAP_PREPARE', prepare_started_at = $1, prepare_deadline_at = $2,
           ready_at = NULL, state_version = state_version + 1
       WHERE run_id = $3 AND stage_index = $4 AND map_index = $5`,
      [now.toISOString(), deadline.toISOString(), runId, stageIndex, mapIndex],
    );
    await transaction.query(
      `UPDATE ranked_placement_runs
       SET state = 'MAP_PREPARE', state_version = state_version + 1,
           deadline_at = $1, last_heartbeat_at = $2
       WHERE id = $3`,
      [deadline.toISOString(), now.toISOString(), runId],
    );
  }

  private async settleMap(
    transaction: SqlExecutor,
    run: PlacementRunRow,
    map: PlacementMapRow,
    score: PlacementMapScore,
    now: Date,
    snapshot: RankedConfigSnapshot,
  ): Promise<void> {
    // Placement Result is user-advanced. Do not append an unexplained fixed
    // five-second gameplay delay after the final-attempt window has settled.
    const resultDeadline = now;
    await transaction.query(
      `UPDATE ranked_placement_maps
       SET state = 'MAP_RESULT', state_version = state_version + 1,
           map_score = $1, map_grade = $2, clear_count = $3,
           top_attempts = $4::jsonb, settled_at = COALESCE(settled_at, $5), result_deadline_at = $6
       WHERE id = $7`,
      [
        score.mapScore,
        score.grade,
        Math.min(score.clearCount, this.requirePlacementPolicy(snapshot).clearTarget),
        JSON.stringify(score.topAttempts),
        now.toISOString(),
        resultDeadline.toISOString(),
        map.id,
      ],
    );
    await transaction.query(
      `UPDATE ranked_placement_runs
       SET state = 'MAP_RESULT', state_version = state_version + 1,
           deadline_at = $1, last_heartbeat_at = $2
       WHERE id = $3`,
      [resultDeadline.toISOString(), now.toISOString(), run.id],
    );
  }

  private async recalculateMap(
    transaction: SqlExecutor,
    map: PlacementMapRow,
    policy: PlacementPolicy,
  ): Promise<PlacementMapScore> {
    const evidence = await this.completedAttemptEvidence(transaction, map.id);
    const score = calculatePlacementMapScore(evidence, policy);
    await transaction.query(
      `UPDATE ranked_placement_maps
       SET map_score = $1, map_grade = $2, clear_count = $3,
           top_attempts = $4::jsonb, state_version = state_version + 1
       WHERE id = $5`,
      [
        score.mapScore,
        score.grade,
        Math.min(score.clearCount, policy.clearTarget),
        JSON.stringify(score.topAttempts),
        map.id,
      ],
    );
    return score;
  }

  private async completedAttemptEvidence(
    transaction: SqlExecutor,
    mapId: string,
  ): Promise<PlacementAttemptResult[]> {
    const attempts = await transaction.query<PlacementAttemptRow>(
      `SELECT id, domain_attempt_id, client_start_event_id, client_end_event_id,
              progress_percent, cleared, awarded_score, valid, ended_at
       FROM ranked_placement_attempts WHERE map_id = $1 AND ended_at IS NOT NULL`,
      [mapId],
    );
    return attempts.rows.map((attempt) => ({
      attemptId: attempt.id,
      progressPercent: Number(attempt.progress_percent ?? 0),
      cleared: Boolean(attempt.cleared),
      valid: Boolean(attempt.valid),
      awardedScore: Number(attempt.awarded_score),
    }));
  }

  private async finalizePlacement(
    transaction: SqlExecutor,
    run: PlacementRunRow,
    snapshot: RankedConfigSnapshot,
    policy: PlacementPolicy,
    now: Date,
  ): Promise<void> {
    if (run.finalized_at || run.state === "PLACEMENT_RESULT") return;
    const maps = await transaction.query<PlacementMapRow>(
      "SELECT * FROM ranked_placement_maps WHERE run_id = $1 ORDER BY stage_index, map_index FOR UPDATE",
      [run.id],
    );
    const rawStageCount = Number(run.stage_count);
    if (rawStageCount !== 3 && rawStageCount !== 5) {
      throw new Error('Placement Run has invalid stage count');
    }
    const stageCount: 3 | 5 = rawStageCount;
    const runPolicy: PlacementPolicy = { ...policy, stageCount };
    const rating = calculatePlacementRating(maps.rows.map((map) => ({
      pool: Number(map.pool) as PoolNumber,
      mapScore: Number(map.map_score),
      mapGrade: map.map_grade,
      stageIndex: Number(map.stage_index) - 1,
    })), runPolicy);
    const normalized = run.rating_policy_version === NORMALIZED_PLACEMENT_RATING_POLICY;
    const finalPlacementRating = normalized ? rating.placementRating : capPlacementRatingForStageCount(
      rating.placementRating,
      stageCount,
      snapshot.operational.tierBands,
    );
    const convertedRating = normalized ? normalizePlacementRating(rating.placementRating, stageCount, runPolicy) : capPlacementRatingForStageCount(
      rating.hiddenMmr,
      stageCount,
      snapshot.operational.tierBands,
    );
    const finalized = finalizePlacementScore(convertedRating, snapshot.operational.tierBands);
    // New placements start matchmaking on the same scale as the actual LP,
    // including the existing tier-boundary adjustment. Legacy runs keep their
    // start-time behavior, and completed profiles are never rewritten here.
    const finalHiddenMmr = normalized ? finalized.visibleRankedScore : convertedRating;
    const placementGames = snapshot.operational.mmrPolicy?.placementGames;
    if (!placementGames) throw new Error("Placement snapshot has no MMR policy");
    await this.achievements.recordPlacementCompletion(
      transaction,
      run.player_id,
      run.id,
      finalized.displayedTier === "GOLD",
      now,
    );
    await transaction.query(
      `UPDATE ranked_profiles
       SET hidden_mmr = $1, visible_ranked_score = $2, displayed_tier = $3,
           placement_games = $4, placement_results = '[]'::jsonb,
           result_streak = 0, promotion_source_tier = NULL, promotion_target_tier = NULL,
           promotion_cutoff = NULL, promotion_games_required = NULL,
           promotion_wins_required = NULL, promotion_results = '[]'::jsonb,
           demotion_warning = FALSE, updated_at = $5
       WHERE player_id = $6`,
      [
        finalHiddenMmr,
        finalized.visibleRankedScore,
        finalized.displayedTier,
        placementGames,
        now.toISOString(),
        run.player_id,
      ],
    );
    await transaction.query(
      `UPDATE ranked_placement_runs
       SET state = 'PLACEMENT_RESULT', state_version = state_version + 1,
           placement_rating = $1, final_hidden_mmr = $2,
           final_visible_ranked_score = $3, final_displayed_tier = $4,
           finalized_at = $5, deadline_at = NULL, completion_kind = 'COMPLETED'
       WHERE id = $6 AND finalized_at IS NULL`,
      [
        finalPlacementRating,
        finalHiddenMmr,
        finalized.visibleRankedScore,
        finalized.displayedTier,
        now.toISOString(),
        run.id,
      ],
    );
  }

  private async loadPublicState(runId: string, playerId: string, now: Date) {
    const runResult = await this.database.query<PlacementRunRow>(
      "SELECT * FROM ranked_placement_runs WHERE id = $1 AND player_id = $2",
      [runId, playerId],
    );
    const run = runResult.rows[0];
    if (!run) throw new NotFoundException("Placement Run not found");
    const maps = await this.database.query<PlacementMapRow>(
      "SELECT * FROM ranked_placement_maps WHERE run_id = $1 ORDER BY stage_index, map_index",
      [run.id],
    );
    const currentStageMaps = maps.rows.filter((map) =>
      Number(map.stage_index) === Number(run.current_stage_index));
    const currentMap = currentStageMaps.find((map) =>
      Number(map.map_index) === Number(run.current_map_index));
    const activeAttempt = currentMap ? await this.database.query<PlacementAttemptRow>(
      `SELECT id, domain_attempt_id, client_start_event_id, client_end_event_id,
              latest_progress_percent AS progress_percent, cleared, awarded_score, valid, ended_at
       FROM ranked_placement_attempts
       WHERE map_id = $1 AND ended_at IS NULL AND valid = TRUE LIMIT 1`,
      [currentMap.id],
    ) : { rows: [], rowCount: 0 };

    const snapshot = await this.loadSnapshot(this.database, run.config_snapshot_id);
    const policy = this.requirePlacementPolicy(snapshot);
    let provisionalMapScore = currentMap ? Number(currentMap.map_score) : 0;
    let provisionalMapGrade: PlacementGrade = currentMap?.map_grade ?? "D";
    const active = activeAttempt.rows[0];
    if (currentMap && active) {
      const attempts = await this.completedAttemptEvidence(this.database, currentMap.id);
      const progressPercent = Math.max(0, Number(active.progress_percent ?? 0));
      const provisionalAttempt = scoreAttempt(
        progressPercent,
        false,
        Number(currentMap.qualifying_percent),
      );
      const provisional = calculatePlacementMapScore([
        ...attempts,
        {
          attemptId: `provisional:${active.id}`,
          progressPercent,
          cleared: false,
          valid: true,
          awardedScore: provisionalAttempt,
        },
      ], policy);
      provisionalMapScore = provisional.mapScore;
      provisionalMapGrade = placementGradeForMapScore(
        provisional.mapScore,
        policy,
        Number(currentMap.clear_count),
      );
    }
    const achievementUnlocks = run.state === "PLACEMENT_RESULT"
      ? await this.achievements.noticesForSource(
          this.database,
          playerId,
          `placement:${run.id}:achievement:${playerId}`,
        )
      : [];

    return {
      runId: run.id,
      state: run.state,
      stateVersion: Number(run.state_version),
      startingPool: Number(run.starting_pool),
      currentStageIndex: Number(run.current_stage_index),
      currentMapIndex: Number(run.current_map_index),
      totalStages: Number(run.stage_count),
      ratingPolicyVersion: run.rating_policy_version,
      mapGradeThresholds: policy.gradeThresholds,
      mapsPerStage: 2,
      stagePools: asArray(run.stage_pools),
      stageResults: asArray(run.stage_results),
      currentStageMaps: currentStageMaps.map((map) => ({
        mapIndex: Number(map.map_index),
        mapScore: Number(map.map_score),
        mapGrade: map.map_grade,
        state: map.state,
      })),
      currentMap: currentMap ? {
        ...mapPresentationFields(snapshot.maps, currentMap.playable_level_id),
        id: currentMap.id,
        levelId: currentMap.level_id,
        canonicalLevelId: currentMap.canonical_level_id,
        alternateLevelId: currentMap.alternate_level_id,
        playableLevelId: currentMap.playable_level_id,
        title: currentMap.title,
        creator: currentMap.creator,
        difficulty: currentMap.difficulty,
        pool: Number(currentMap.pool),
        qualifyingPercent: Number(currentMap.qualifying_percent),
        mapScore: Number(currentMap.map_score),
        mapGrade: currentMap.map_grade,
        clearCount: Number(currentMap.clear_count),
        topAttempts: asArray(currentMap.top_attempts),
        provisionalMapScore,
        provisionalMapGrade,
        clearTarget: 2,
        mapScoreMaximum: 500,
        prepareStartedAt: iso(currentMap.prepare_started_at),
        prepareDeadlineAt: iso(currentMap.prepare_deadline_at),
        readyAt: iso(currentMap.ready_at),
        levelRevealRemainingSeconds: 5,
        startedAt: iso(currentMap.started_at),
        normalEndAt: iso(currentMap.normal_end_at),
        finalWindowEndAt: iso(currentMap.final_window_end_at),
      } : null,
      activeAttempt: activeAttempt.rows[0] ? {
        attemptId: activeAttempt.rows[0].id,
        domainAttemptId: activeAttempt.rows[0].domain_attempt_id,
      } : null,
      finalResult: run.state === "PLACEMENT_RESULT" ? {
        visibleRankedScore: Number(run.final_visible_ranked_score),
        displayedTier: run.final_displayed_tier,
        placed: true,
        skipped: run.completion_kind === "SKIPPED",
        finalizedAt: iso(run.finalized_at),
        acknowledgedAt: iso(run.result_acknowledged_at),
        achievementUnlocks,
      } : null,
      deadlineAt: iso(run.deadline_at),
      serverNow: now.toISOString(),
    };
  }

  private async lockRun(
    transaction: SqlExecutor,
    runId: string,
    playerId: string,
  ): Promise<PlacementRunRow> {
    const result = await transaction.query<PlacementRunRow>(
      "SELECT * FROM ranked_placement_runs WHERE id = $1 AND player_id = $2 FOR UPDATE",
      [runId, playerId],
    );
    const run = result.rows[0];
    if (!run) throw new NotFoundException("Placement Run not found");
    return run;
  }

  private async lockCurrentMap(
    transaction: SqlExecutor,
    run: PlacementRunRow,
  ): Promise<PlacementMapRow> {
    const result = await transaction.query<PlacementMapRow>(
      `SELECT * FROM ranked_placement_maps
       WHERE run_id = $1 AND stage_index = $2 AND map_index = $3 FOR UPDATE`,
      [run.id, run.current_stage_index, run.current_map_index],
    );
    const map = result.rows[0];
    if (!map) throw new Error("Placement current Map is missing");
    return map;
  }

  private async stageMaps(
    transaction: SqlExecutor,
    runId: string,
    stageIndex: number,
  ): Promise<readonly PlacementMapRow[]> {
    const result = await transaction.query<PlacementMapRow>(
      `SELECT * FROM ranked_placement_maps
       WHERE run_id = $1 AND stage_index = $2 ORDER BY map_index FOR UPDATE`,
      [runId, stageIndex],
    );
    if (result.rows.length !== 2 || result.rows.some((map) => map.state !== "MAP_RESULT")) {
      throw new ConflictException("Both Placement Stage maps must be settled");
    }
    return result.rows;
  }

  private mapScoreFromRow(map: PlacementMapRow): PlacementMapScore {
    return {
      mapScore: Number(map.map_score),
      grade: map.map_grade,
      clearCount: Number(map.clear_count),
      completedByClearTarget: Number(map.clear_count) >= 2,
      topAttempts: asArray(map.top_attempts) as unknown as PlacementAttemptResult[],
      validAttemptCount: 0,
    };
  }

  private requirePlacementPolicy(snapshot: RankedConfigSnapshot): PlacementPolicy {
    const policy = snapshot.operational.placementPolicy;
    if (!policy) throw new Error("Placement snapshot has no Placement policy");
    return policy;
  }

  private async loadSnapshot(
    transaction: SqlExecutor,
    snapshotId: string,
  ): Promise<RankedConfigSnapshot> {
    const result = await transaction.query<ConfigSnapshotRow>(
      "SELECT source_payload FROM ranked_config_snapshots WHERE id = $1",
      [snapshotId],
    );
    const payload = result.rows[0]?.source_payload;
    if (!payload) throw new Error("Placement config snapshot is missing");
    return asObject(payload) as unknown as RankedConfigSnapshot;
  }

  private async insertStageMaps(
    transaction: SqlExecutor,
    runId: string,
    stageIndex: number,
    maps: readonly [RankedMapSnapshot, RankedMapSnapshot],
  ): Promise<void> {
    for (const [index, map] of maps.entries()) {
      await transaction.query(
        `INSERT INTO ranked_placement_maps (
           id, run_id, stage_index, map_index, level_id, canonical_level_id,
           alternate_level_id, playable_level_id, title, creator, difficulty,
           pool, qualifying_percent, state
         ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, 'MAP_PREPARE')`,
        [
          this.ids.next(), runId, stageIndex, index + 1, map.levelId,
          map.canonicalLevelId, map.alternateLevelId, map.playableLevelId,
          map.title, map.creator, map.difficulty, map.pool, map.qualifyingPercent,
        ],
      );
    }
  }

  private async persistConfigSnapshot(
    transaction: SqlExecutor,
    snapshot: RankedConfigSnapshot,
  ): Promise<string> {
    const id = this.ids.next();
    const inserted = await transaction.query<{ id: string }>(
      `INSERT INTO ranked_config_snapshots (
         id, generation, rules_version, source_payload, fetched_at
       ) VALUES ($1, $2, $3, $4::jsonb, $5)
       ON CONFLICT (generation, rules_version) DO NOTHING RETURNING id`,
      [
        id,
        snapshot.generation,
        snapshot.operational.rules.rulesVersion,
        JSON.stringify(snapshot),
        snapshot.fetchedAt,
      ],
    );
    if (inserted.rows[0]) return inserted.rows[0].id;
    const existing = await transaction.query<{ id: string }>(
      "SELECT id FROM ranked_config_snapshots WHERE generation = $1 AND rules_version = $2",
      [snapshot.generation, snapshot.operational.rules.rulesVersion],
    );
    if (!existing.rows[0]) throw new Error("Failed to persist Placement config snapshot");
    return existing.rows[0].id;
  }

  private async assertNoPvPActivity(transaction: SqlExecutor, playerId: string): Promise<void> {
    const survival=await transaction.query(`SELECT 1 FROM sur_match_players p JOIN sur_matches m ON m.id=p.match_id
      WHERE p.player_id=$1 AND m.state NOT IN ('FINAL_RESULT','CANCELLED') AND (p.status='ACTIVE' OR EXISTS(SELECT 1 FROM sur_online_players o WHERE o.match_id=m.id AND o.player_id=$1 AND o.result IS NULL))
      UNION ALL SELECT 1 FROM sur_queue_entries WHERE player_id=$1 AND status='QUEUED' LIMIT 1`,[playerId]);
    if(survival.rows.length)throw new ConflictException("Finish or recover the current Survival game first");
    const match = await transaction.query<{ id: string }>(
      `SELECT id FROM ranked_matches
       WHERE (player_a_id = $1 OR player_b_id = $1)
         AND state NOT IN ('MATCH_RESULT', 'CANCELLED') LIMIT 1 FOR UPDATE`,
      [playerId],
    );
    if (match.rows[0]) throw new ConflictException("Player already has an active Ranked match");
    const queue = await transaction.query<{ player_id: string }>(
      "SELECT player_id FROM ranked_queue_entries WHERE player_id = $1 AND status = 'QUEUED' FOR UPDATE",
      [playerId],
    );
    if (queue.rows[0]) throw new ConflictException("Leave the Ranked queue before Placement");
  }

  private async assertRunExists(runId: string, playerId: string): Promise<void> {
    const result = await this.database.query<{ id: string }>(
      "SELECT id FROM ranked_placement_runs WHERE id = $1 AND player_id = $2",
      [runId, playerId],
    );
    if (!result.rows[0]) throw new NotFoundException("Placement Run not found");
  }

  private gradeRank(grade: PlacementGrade): number {
    return ["S", "A", "B", "C", "D"].indexOf(grade);
  }
}
