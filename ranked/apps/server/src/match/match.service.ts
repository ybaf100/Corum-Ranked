import {
  ConflictException,
  Inject,
  Injectable,
  NotFoundException,
} from "@nestjs/common";
import {
  advanceRoundClock,
  applyDeathmatchWinner,
  applyRoundResult,
  bannerForRound,
  calculateMmrUpdate,
  createRound,
  displayedTierForProfile,
  evaluateDeathmatch,
  endRoundAttempt,
  invalidateRoundAttempt,
  resolveBans,
  selectDeathmatchMap,
  scoreAttempt,
  roundDeadlineAtMs,
  startRoundAttempt,
  tierBandFor,
  type MatchSeriesState,
  type PlayerSide,
  type RandomSource,
  type RankedMapSnapshot,
  type RoundState,
} from "@corum-ranked/rules";
import {
  ID_GENERATOR,
  RANDOM_SOURCE,
  SERVER_CLOCK,
  type IdGenerator,
  type ServerClock,
} from "../common/runtime.module.js";
import type { RankedConfigSnapshot } from "../config/ranked-config.document.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { OutboxService } from "../relay/outbox.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SessionService } from "../session/session.service.js";
import { MatchAccessService } from "./match-access.service.js";
import type {
  AttemptEndDto,
  AttemptProgressDto,
  AttemptStartDto,
  ReadyMatchDto,
  ResourceFailureDto,
  SubmitBanDto,
} from "./match.dto.js";
import {
  MATCH_RUNTIME_STATE,
  type MatchRuntimeStatePort,
} from "./match-runtime-state.js";

type MatchState =
  | "MATCHED"
  | "BAN_PHASE"
  | "ROUND_PREPARE"
  | "ROUND_PLAYING"
  | "FINAL_ATTEMPT_WINDOW"
  | "LAST_ATTEMPT_WINDOW"
  | "ROUND_SETTLING"
  | "ROUND_RESULT"
  | "DEATHMATCH_PREPARE"
  | "DEATHMATCH_PLAYING"
  | "DEATHMATCH_RESULT"
  | "MATCH_RESULT"
  | "CANCELLED";

interface LockedMatchRow {
  id: string;
  player_a_id: string;
  player_b_id: string;
  mmr_a_before: number;
  mmr_b_before: number;
  effective_tier: string;
  candidate_maps_snapshot: unknown;
  selected_round_maps_snapshot: unknown | null;
  series_state: unknown;
  state: MatchState;
  state_version: number;
  current_round_number: number | null;
  ready_a_at: Date | string | null;
  ready_b_at: Date | string | null;
  ready_deadline_at: Date | string | null;
  last_heartbeat_a_at: Date | string | null;
  last_heartbeat_b_at: Date | string | null;
  ban_a_canonical_id: string | null;
  ban_b_canonical_id: string | null;
  ban_a_confirmed_at: Date | string | null;
  ban_b_confirmed_at: Date | string | null;
  ban_deadline_at: Date | string | null;
  deadline_at: Date | string | null;
  winner_id: string | null;
  mmr_delta_a: number | null;
  mmr_delta_b: number | null;
  mmr_a_after: number | null;
  mmr_b_after: number | null;
  result_applied_at: Date | string | null;
  current_deathmatch_id: string | null;
  source_payload: unknown;
  match_type: "PVP" | "DEBUG_BOT";
  debug_bot_config: unknown | null;
  discord_events_enabled: boolean;
  cancellation_reason: string | null;
  finished_at: Date | string | null;
}

interface RoundRow {
  id: string;
  match_id: string;
  round_number: 1 | 2 | 3;
  level_id: string;
  canonical_level_id: string;
  alternate_level_id: string | null;
  playable_level_id: string;
  title: string;
  creator: string;
  difficulty: string;
  pool: 1 | 2 | 3 | 4 | 5 | 6;
  qualifying_percent: number;
  phase: string;
  domain_state: unknown | null;
  ready_a_at: Date | string | null;
  ready_b_at: Date | string | null;
  ready_deadline_at: Date | string | null;
  result_deadline_at: Date | string | null;
  result: PlayerSide | "DRAW" | null;
}

interface ProfileResultRow {
  player_id: string;
  hidden_mmr: number;
  placement_games: number;
}

interface PublicProfileRow {
  player_id: string;
  displayed_tier: string;
  visible_ranked_score: number | null;
  placement_games: number;
}

interface RoundSummaryRow {
  round_number: number;
  title: string;
  difficulty: string;
  score_a: number;
  score_b: number;
  clears_a: number;
  clears_b: number;
  result: PlayerSide | "DRAW" | null;
}

interface HistoryMatchRow {
  id: string;
  player_a_id: string;
  player_b_id: string;
  effective_tier: string;
  winner_id: string | null;
  mmr_delta_a: number | null;
  mmr_delta_b: number | null;
  mmr_a_after: number | null;
  mmr_b_after: number | null;
  series_state: unknown;
  finished_at: Date | string | null;
  opponent_name: string;
}


interface DeathmatchRow {
  id: string;
  sequence: number;
  map_snapshot: unknown;
  started_at: Date | string | null;
  finished_at: Date | string | null;
}

interface DeathmatchAttemptRow {
  id: string;
  player_id: string;
  attempt_sequence: number;
  server_accepted_start_at: Date | string;
  ended_at: Date | string | null;
  progress_percent: number | null;
  cleared: boolean;
  awarded_score: number;
  valid: boolean;
  client_start_event_id: string;
  client_end_event_id: string | null;
}

const parseJson = <T>(value: unknown): T => {
  if (typeof value === "string") return JSON.parse(value) as T;
  return structuredClone(value) as T;
};

const dateIso = (value: Date | string | null): string | null =>
  value === null ? null : new Date(value).toISOString();

const sideColumn = (side: PlayerSide, prefix: string): string =>
  `${prefix}_${side.toLowerCase()}`;

// alpha.10 client flow: 10s start countdown, map download may take 30s,
// and a custom song may be allowed up to 20s. Keep server preparation
// deadlines comfortably beyond the client-side resource window.
const resourcePrepareSeconds = (configuredReadySeconds: number): number =>
  Math.max(configuredReadySeconds, 60);

@Injectable()
export class MatchService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    @Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    private readonly access: MatchAccessService,
    private readonly sessions: SessionService,
    private readonly outbox: OutboxService,
    @Inject(MATCH_RUNTIME_STATE) private readonly runtimeState: MatchRuntimeStatePort,
  ) {}

  public async history(session: RankedSessionContext) {
    const now = this.clock.now();
    const matches = await this.database.query<HistoryMatchRow>(
      `SELECT m.*,
              CASE WHEN m.player_a_id = $1 THEN pb.gd_username ELSE pa.gd_username END AS opponent_name
       FROM ranked_matches m
       JOIN ranked_players pa ON pa.id = m.player_a_id
       JOIN ranked_players pb ON pb.id = m.player_b_id
       WHERE (m.player_a_id = $1 OR m.player_b_id = $1)
         AND m.state = 'MATCH_RESULT'
       ORDER BY m.finished_at DESC NULLS LAST, m.created_at DESC
       LIMIT 20`,
      [session.playerId],
    );
    const items = [];
    for (const match of matches.rows) {
      const side: PlayerSide = match.player_a_id === session.playerId ? "A" : "B";
      const winnerSide: PlayerSide | null =
        match.winner_id === match.player_a_id
          ? "A"
          : match.winner_id === match.player_b_id
            ? "B"
            : null;
      items.push({
        matchId: match.id,
        finishedAt: dateIso(match.finished_at),
        side,
        opponentName: match.opponent_name,
        effectiveTier: match.effective_tier,
        winnerSide,
        mmrDelta: { A: match.mmr_delta_a, B: match.mmr_delta_b },
        ratingAfter: { A: match.mmr_a_after, B: match.mmr_b_after },
        series: parseJson<MatchSeriesState>(match.series_state),
        rounds: await this.roundSummaries(this.database, match.id),
        deathmatches: await this.deathmatchSummaries(this.database, match.id),
      });
    }
    return { matches: items, serverNow: now.toISOString() };
  }

  public async ready(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: ReadyMatchDto,
  ) {
    this.sessions.assertEnvironment(body.installedMods);
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state === "MATCHED") {
        const column = sideColumn(authorization.side, "ready");
        await transaction.query(
          `UPDATE ranked_matches
           SET ${column}_at = COALESCE(${column}_at, $2), state_version = state_version + 1
           WHERE id = $1`,
          [matchId, now.toISOString()],
        );
        match = await this.lockMatch(transaction, matchId);
        if (match.ready_a_at && match.ready_b_at) {
          const config = this.matchConfig(match);
          const deadline = new Date(
            now.getTime() + config.operational.rules.banSeconds * 1_000,
          );
          await transaction.query(
            `UPDATE ranked_matches
             SET state = 'BAN_PHASE', ban_deadline_at = $2, deadline_at = $2,
                 ready_deadline_at = NULL, state_version = state_version + 1,
                 started_at = COALESCE(started_at, $3)
             WHERE id = $1`,
            [matchId, deadline.toISOString(), now.toISOString()],
          );
        }
        return;
      }

      if (match.state !== "ROUND_PREPARE" || !match.current_round_number) {
        if (match.state === "DEATHMATCH_PREPARE" && match.current_deathmatch_id) {
          const column = sideColumn(authorization.side, "ready");
          await transaction.query(
            `UPDATE ranked_matches
             SET ${column}_at = COALESCE(${column}_at, $2), state_version = state_version + 1
             WHERE id = $1`,
            [matchId, now.toISOString()],
          );
          match = await this.lockMatch(transaction, matchId);
          if (match.ready_a_at && match.ready_b_at) {
            const deathmatch = await this.currentDeathmatch(transaction, match);
            const deathmatchMap = parseJson<RankedMapSnapshot>(deathmatch.map_snapshot);
            await transaction.query(
              "UPDATE ranked_deathmatches SET started_at = $2 WHERE id = $1 AND started_at IS NULL",
              [match.current_deathmatch_id, now.toISOString()],
            );
            await transaction.query(
              `UPDATE ranked_matches
               SET state = 'DEATHMATCH_PLAYING', ready_deadline_at = NULL,
                   deadline_at = NULL, state_version = state_version + 1
               WHERE id = $1`,
              [matchId],
            );
            await this.outbox.enqueueMatchEvent(
              transaction,
              match,
              "DEATHMATCH_START",
              `match:${match.id}:deathmatch:${deathmatch.id}:start`,
              {
                sequence: Number(deathmatch.sequence),
                mapTitle: deathmatchMap.title,
                qualifyingPercent: deathmatchMap.qualifyingPercent,
              },
              now,
            );
          }
          return;
        }
        throw new ConflictException(`Ready is not accepted in ${match.state}`);
      }
      const round = await this.lockCurrentRound(transaction, match);
      const column = sideColumn(authorization.side, "ready");
      await transaction.query(
        `UPDATE ranked_rounds
         SET ${column}_at = COALESCE(${column}_at, $2), state_version = state_version + 1
         WHERE id = $1`,
        [round.id, now.toISOString()],
      );
      const updatedRound = await this.lockCurrentRound(transaction, match);
      if (updatedRound.ready_a_at && updatedRound.ready_b_at) {
        await this.startPreparedRound(transaction, match, updatedRound, now);
      }
    });
    return this.state(matchId, matchToken, session);
  }

  public async reportResourceFailure(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: ResourceFailureDto,
  ) {
    if (body.resource !== "MAP") {
      throw new ConflictException("Only map download failure is a Ranked-fatal resource failure");
    }
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state === "ROUND_PREPARE" && match.current_round_number) {
        const round = await this.lockCurrentRound(transaction, match);
        if (round.round_number === 1) {
          await this.cancelMatch(transaction, match.id, "ROUND_1_MAP_DOWNLOAD_TIMEOUT", now);
          return;
        }
        await this.finalizeMatch(
          transaction,
          match,
          authorization.side === "A" ? "B" : "A",
          now,
        );
        return;
      }
      if (match.state === "DEATHMATCH_PREPARE") {
        await this.finalizeMatch(
          transaction,
          match,
          authorization.side === "A" ? "B" : "A",
          now,
        );
        return;
      }
      if (match.state === "MATCH_RESULT" || match.state === "CANCELLED") return;
      throw new ConflictException(`Map download failure is not accepted in ${match.state}`);
    });
    return this.state(matchId, matchToken, session);
  }

  public async submitBan(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: SubmitBanDto,
  ) {
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state !== "BAN_PHASE") {
        throw new ConflictException(`Ban is not accepted in ${match.state}`);
      }
      const canonicalLevelId = body.canonicalLevelId?.trim() || null;
      const candidates = parseJson<RankedMapSnapshot[]>(match.candidate_maps_snapshot);
      if (
        canonicalLevelId &&
        !candidates.some((candidate) => candidate.canonicalLevelId === canonicalLevelId)
      ) {
        throw new ConflictException("Selected ban is outside the candidate set");
      }
      const confirmedColumn = sideColumn(authorization.side, "ban") + "_confirmed_at";
      const banColumn = sideColumn(authorization.side, "ban") + "_canonical_id";
      const existingConfirmed = authorization.side === "A"
        ? match.ban_a_confirmed_at
        : match.ban_b_confirmed_at;
      const existingBan = authorization.side === "A"
        ? match.ban_a_canonical_id
        : match.ban_b_canonical_id;
      if (existingConfirmed) {
        if (existingBan !== canonicalLevelId) {
          throw new ConflictException("A confirmed ban cannot be changed");
        }
        return;
      }
      await transaction.query(
        `UPDATE ranked_matches
         SET ${banColumn} = $2, ${confirmedColumn} = $3, state_version = state_version + 1
         WHERE id = $1`,
        [matchId, canonicalLevelId, now.toISOString()],
      );
      match = await this.lockMatch(transaction, matchId);
      if (match.ban_a_confirmed_at && match.ban_b_confirmed_at) {
        await this.finalizeBans(transaction, match, now);
      }
    });
    return this.state(matchId, matchToken, session);
  }

  public async state(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
  ) {
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state !== "MATCH_RESULT" && match.state !== "CANCELLED") {
        const column = sideColumn(authorization.side, "last_heartbeat");
        await transaction.query(
          `UPDATE ranked_matches SET ${column}_at = $2 WHERE id = $1`,
          [matchId, now.toISOString()],
        );
        match = await this.lockMatch(transaction, matchId);
      }
      return this.buildPublicState(transaction, match, session.playerId, now);
    });
  }

  public async heartbeat(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
  ) {
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state === "MATCH_RESULT" || match.state === "CANCELLED") return;
      const column = sideColumn(authorization.side, "last_heartbeat");
      await transaction.query(
        `UPDATE ranked_matches SET ${column}_at = $2 WHERE id = $1`,
        [matchId, now.toISOString()],
      );
    });
    return this.state(matchId, matchToken, session);
  }

  public async startAttempt(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: AttemptStartDto,
  ) {
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state === "DEATHMATCH_PLAYING") {
        return this.startDeathmatchAttempt(
          transaction,
          match,
          authorization.side,
          session.playerId,
          body,
          now,
        );
      }
      if (!this.isRoundPlaying(match.state)) {
        throw new ConflictException(`Attempt start is not accepted in ${match.state}`);
      }
      const round = await this.lockCurrentRound(transaction, match);
      this.assertPlayableLevelId(body.levelId, round.playable_level_id);
      const roundState = this.roundState(round);
      const decision = startRoundAttempt(
        roundState,
        authorization.side,
        now.getTime(),
        body.clientEventId,
      );
      if (decision.accepted && !decision.duplicate && decision.attemptId) {
        const attempt = decision.state.attempts[authorization.side].find(
          (candidate) => candidate.id === decision.attemptId,
        );
        if (!attempt) throw new Error("Accepted attempt was not persisted in domain state");
        await transaction.query(
          `INSERT INTO ranked_attempts (
             id, round_id, player_id, attempt_sequence, domain_attempt_id,
             server_accepted_start_at, client_started_at, level_id, client_start_event_id
           ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
           ON CONFLICT (round_id, player_id, client_start_event_id) DO NOTHING`,
          [
            this.ids.next(),
            round.id,
            session.playerId,
            attempt.sequence,
            attempt.id,
            now.toISOString(),
            body.clientStartedAt ?? null,
            body.levelId,
            body.clientEventId,
          ],
        );
      }
      await this.persistRoundState(transaction, match, round, decision.state, now);
      if (
        decision.accepted &&
        decision.attemptId &&
        !this.runtimeState.progress(match.id, round.round_number, authorization.side)
      ) {
        this.runtimeState.beginAttempt(
          match.id,
          round.round_number,
          authorization.side,
          decision.attemptId,
          now.getTime(),
        );
      }
      return {
        accepted: decision.accepted,
        duplicate: decision.duplicate,
        attemptId: decision.attemptId,
        reason: decision.reason,
        stateVersion: decision.state.stateVersion,
        deadlineAt: this.domainDeadlineIso(decision.state),
        serverNow: now.toISOString(),
      };
    });
  }

  public async endAttempt(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: AttemptEndDto,
  ) {
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      match = await this.advanceLockedMatch(transaction, match, now);
      if (match.state === "DEATHMATCH_PLAYING") {
        return this.endDeathmatchAttempt(
          transaction,
          match,
          authorization.side,
          session.playerId,
          body,
          now,
        );
      }
      if (!this.isRoundPlaying(match.state)) {
        throw new ConflictException(`Attempt end is not accepted in ${match.state}`);
      }
      const round = await this.lockCurrentRound(transaction, match);
      this.assertPlayableLevelId(body.levelId, round.playable_level_id);
      const decision = endRoundAttempt(
        this.roundState(round),
        authorization.side,
        body.attemptId,
        now.getTime(),
        body.progressPercent,
        body.cleared,
        body.clientEventId,
      );
      if (decision.accepted && !decision.duplicate) {
        const attempt = decision.state.attempts[authorization.side].find(
          (candidate) => candidate.id === body.attemptId,
        );
        if (!attempt) throw new Error("Ended attempt is missing from domain state");
        await transaction.query(
          `UPDATE ranked_attempts
           SET ended_at = $4, client_ended_at = $5, progress_percent = $6,
               cleared = $7, awarded_score = $8, valid = $9,
               invalid_reason = $10, client_end_event_id = $11
           WHERE round_id = $1 AND player_id = $2 AND domain_attempt_id = $3
             AND ended_at IS NULL`,
          [
            round.id,
            session.playerId,
            body.attemptId,
            now.toISOString(),
            body.clientEndedAt ?? null,
            attempt.progressPercent,
            attempt.cleared,
            attempt.awardedScore,
            attempt.valid,
            attempt.invalidReason,
            body.clientEventId,
          ],
        );
        if (attempt.valid && attempt.cleared) {
          await this.outbox.enqueueMatchEvent(
            transaction,
            match,
            "CLEAR_EVENT",
            `match:${match.id}:round:${round.round_number}:clear:${attempt.id}`,
            {
              phase: "ROUND",
              roundNumber: Number(round.round_number),
              mapTitle: round.title,
              side: authorization.side,
              scores: decision.state.scores,
              clears: decision.state.clears,
            },
            now,
          );
        }
        const previousState = this.roundState(round);
        if (!previousState.lastAttemptWindow && decision.state.lastAttemptWindow) {
          await this.outbox.enqueueMatchEvent(
            transaction,
            match,
            "LAST_ATTEMPT",
            `match:${match.id}:round:${round.round_number}:last-attempt`,
            {
              roundNumber: Number(round.round_number),
              targetSide: decision.state.lastAttemptWindow.targetSide,
              windowSeconds: Math.round(
                (decision.state.lastAttemptWindow.endsAtMs -
                  decision.state.lastAttemptWindow.startedAtMs) /
                  1_000,
              ),
            },
            now,
          );
        }
      }
      await this.persistRoundState(transaction, match, round, decision.state, now);
      if (decision.accepted) {
        this.runtimeState.endAttempt(
          match.id,
          round.round_number,
          authorization.side,
          body.attemptId,
        );
      }
      return {
        accepted: decision.accepted,
        duplicate: decision.duplicate,
        reason: decision.reason,
        stateVersion: decision.state.stateVersion,
        deadlineAt: this.domainDeadlineIso(decision.state),
        serverNow: now.toISOString(),
      };
    });
  }

  public async updateAttemptProgress(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: AttemptProgressDto,
  ) {
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);
      let match = await this.lockMatch(transaction, matchId);
      if (match.deadline_at && now.getTime() >= new Date(match.deadline_at).getTime()) {
        match = await this.advanceLockedMatch(transaction, match, now);
      }
      if (!this.isRoundPlaying(match.state)) {
        throw new ConflictException(`Attempt progress is not accepted in ${match.state}`);
      }
      const round = await this.lockCurrentRound(transaction, match);
      this.assertPlayableLevelId(body.levelId, round.playable_level_id);
      const state = this.roundState(round);
      const activeAttempt = state.attempts[authorization.side].find(
        (attempt) =>
          attempt.id === body.attemptId &&
          attempt.valid &&
          attempt.endedAtMs === null,
      );
      if (!activeAttempt) {
        throw new ConflictException("Attempt progress requires the active server attempt");
      }
      const stored = this.runtimeState.updateProgress(
        match.id,
        round.round_number,
        authorization.side,
        body.attemptId,
        body.progressPercent,
        now.getTime(),
      );
      return {
        accepted: true,
        stored,
        serverNow: now.toISOString(),
      };
    });
  }

  private async lockMatch(transaction: SqlExecutor, matchId: string): Promise<LockedMatchRow> {
    const result = await transaction.query<LockedMatchRow>(
      `SELECT m.*, c.source_payload
       FROM ranked_matches m
       JOIN ranked_config_snapshots c ON c.id = m.config_snapshot_id
       WHERE m.id = $1
       FOR UPDATE OF m`,
      [matchId],
    );
    const match = result.rows[0];
    if (!match) throw new NotFoundException("Ranked match not found");
    return match;
  }

  private matchConfig(match: LockedMatchRow): RankedConfigSnapshot {
    return parseJson<RankedConfigSnapshot>(match.source_payload);
  }

  private async lockCurrentRound(
    transaction: SqlExecutor,
    match: LockedMatchRow,
  ): Promise<RoundRow> {
    if (!match.current_round_number) throw new ConflictException("Match has no current round");
    const result = await transaction.query<RoundRow>(
      `SELECT * FROM ranked_rounds
       WHERE match_id = $1 AND round_number = $2
       FOR UPDATE`,
      [match.id, match.current_round_number],
    );
    const round = result.rows[0];
    if (!round) throw new Error("Current round row is missing");
    return round;
  }

  private roundState(round: RoundRow): RoundState {
    if (!round.domain_state) throw new ConflictException("Round has not started");
    return parseJson<RoundState>(round.domain_state);
  }

  private isRoundPlaying(state: MatchState): boolean {
    return [
      "ROUND_PLAYING",
      "FINAL_ATTEMPT_WINDOW",
      "LAST_ATTEMPT_WINDOW",
      "ROUND_SETTLING",
    ].includes(state);
  }

  private domainDeadlineIso(state: RoundState): string | null {
    const deadline = roundDeadlineAtMs(state);
    return deadline === null ? null : new Date(deadline).toISOString();
  }

  private assertPlayableLevelId(actual: string, expected: string): void {
    if (actual.trim() !== expected) {
      throw new ConflictException("Attempt Level ID does not match the Round playableLevelId snapshot");
    }
  }

  private async advanceLockedMatch(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<LockedMatchRow> {
    const config = this.matchConfig(match);
    if (!config.operational.timeouts || !config.operational.failurePolicy) {
      throw new Error("Match snapshot is missing timeout/failure policy");
    }

    if (
      !["MATCH_RESULT", "CANCELLED"].includes(match.state) &&
      match.last_heartbeat_a_at &&
      match.last_heartbeat_b_at
    ) {
      const graceMs = config.operational.timeouts.reconnectGraceSeconds * 1_000;
      const staleA = now.getTime() - new Date(match.last_heartbeat_a_at).getTime() >= graceMs;
      const staleB = now.getTime() - new Date(match.last_heartbeat_b_at).getTime() >= graceMs;
      if (staleA || staleB) {
        if (
          config.operational.failurePolicy.reconnectTimeoutAction === "FORFEIT_DISCONNECTED" &&
          staleA !== staleB
        ) {
          await this.finalizeMatch(transaction, match, staleA ? "B" : "A", now);
        } else {
          await this.cancelMatch(transaction, match.id, "RECONNECT_TIMEOUT", now);
        }
        return this.lockMatch(transaction, match.id);
      }
    }

    if (
      match.state === "MATCHED" &&
      match.ready_deadline_at &&
      now.getTime() >= new Date(match.ready_deadline_at).getTime()
    ) {
      await this.resolveReadyTimeout(transaction, match, now);
      return this.lockMatch(transaction, match.id);
    }
    if (
      match.state === "BAN_PHASE" &&
      match.ban_deadline_at &&
      now.getTime() >= new Date(match.ban_deadline_at).getTime()
    ) {
      await this.finalizeBans(transaction, match, now);
      return this.lockMatch(transaction, match.id);
    }
    if (match.state === "ROUND_PREPARE") {
      const round = await this.lockCurrentRound(transaction, match);
      if (
        round.ready_deadline_at &&
        now.getTime() >= new Date(round.ready_deadline_at).getTime()
      ) {
        await this.resolveRoundReadyTimeout(transaction, match, round, now);
        return this.lockMatch(transaction, match.id);
      }
    }
    if (
      match.state === "DEATHMATCH_PREPARE" &&
      match.ready_deadline_at &&
      now.getTime() >= new Date(match.ready_deadline_at).getTime()
    ) {
      await this.resolveDeathmatchReadyTimeout(transaction, match, now);
      return this.lockMatch(transaction, match.id);
    }
    if (this.isRoundPlaying(match.state)) {
      const round = await this.lockCurrentRound(transaction, match);
      const initial = this.roundState(round);
      const withoutOrphans = await this.expireOrphanRoundAttempts(
        transaction,
        match,
        round,
        initial,
        now,
      );
      const advanced = advanceRoundClock(withoutOrphans, now.getTime());
      if (
        advanced.phase === initial.phase &&
        advanced.stateVersion === initial.stateVersion &&
        advanced.outcome?.result === initial.outcome?.result
      ) {
        return match;
      }
      await this.persistRoundState(transaction, match, round, advanced, now);
      return this.lockMatch(transaction, match.id);
    }
    if (match.state === "ROUND_RESULT" && match.deadline_at) {
      if (now.getTime() >= new Date(match.deadline_at).getTime()) {
        await this.prepareNextRound(transaction, match, now);
        return this.lockMatch(transaction, match.id);
      }
    }
    if (match.state === "DEATHMATCH_PLAYING") {
      await this.expireOrphanDeathmatchAttempts(transaction, match, now);
      await this.settleDeathmatchIfComplete(transaction, match, now);
      return this.lockMatch(transaction, match.id);
    }
    if (match.state === "DEATHMATCH_RESULT" && match.deadline_at) {
      if (now.getTime() >= new Date(match.deadline_at).getTime()) {
        await this.prepareDeathmatch(transaction, match, now);
        return this.lockMatch(transaction, match.id);
      }
    }
    return match;
  }

  private async expireOrphanRoundAttempts(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    round: RoundRow,
    initial: RoundState,
    now: Date,
  ): Promise<RoundState> {
    const timeoutSeconds = this.matchConfig(match).operational.timeouts?.orphanAttemptSeconds;
    if (!timeoutSeconds) return initial;
    let state = initial;
    for (const side of ["A", "B"] as const) {
      const active = state.attempts[side].filter(
        (attempt) =>
          attempt.valid &&
          attempt.endedAtMs === null &&
          now.getTime() - attempt.serverAcceptedStartAtMs >= timeoutSeconds * 1_000,
      );
      for (const attempt of active) {
        state = invalidateRoundAttempt(
          state,
          side,
          attempt.id,
          now.getTime(),
          "ORPHAN_ATTEMPT_TIMEOUT",
        );
        await transaction.query(
          `UPDATE ranked_attempts
           SET ended_at = $4, valid = FALSE, invalid_reason = 'ORPHAN_ATTEMPT_TIMEOUT'
           WHERE round_id = $1 AND player_id = $2 AND domain_attempt_id = $3
             AND ended_at IS NULL`,
          [
            round.id,
            side === "A" ? match.player_a_id : match.player_b_id,
            attempt.id,
            now.toISOString(),
          ],
        );
      }
    }
    return state;
  }

  private async expireOrphanDeathmatchAttempts(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    if (!match.current_deathmatch_id) return;
    const timeoutSeconds = this.matchConfig(match).operational.timeouts?.orphanAttemptSeconds;
    if (!timeoutSeconds) return;
    const cutoff = new Date(now.getTime() - timeoutSeconds * 1_000);
    await transaction.query(
      `UPDATE ranked_deathmatch_attempts
       SET ended_at = $2, valid = FALSE, invalid_reason = 'ORPHAN_ATTEMPT_TIMEOUT'
       WHERE deathmatch_id = $1 AND ended_at IS NULL AND server_accepted_start_at <= $3`,
      [match.current_deathmatch_id, now.toISOString(), cutoff.toISOString()],
    );
  }

  private async resolveReadyTimeout(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    const policy = this.matchConfig(match).operational.failurePolicy;
    if (
      policy?.readyTimeoutAction === "FORFEIT_UNREADY" &&
      Boolean(match.ready_a_at) !== Boolean(match.ready_b_at)
    ) {
      await this.finalizeMatch(
        transaction,
        match,
        match.ready_a_at ? "A" : "B",
        now,
      );
      return;
    }
    await this.cancelMatch(transaction, match.id, "READY_TIMEOUT", now);
  }

  private async resolveRoundReadyTimeout(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    round: RoundRow,
    now: Date,
  ): Promise<void> {
    // Round 1 resource failure invalidates the match completely. From Round 2
    // onward, a one-sided map preparation failure is a match forfeit. If both
    // sides are unready, there is no fair winner, so the match is cancelled.
    if (round.round_number === 1) {
      await this.cancelMatch(transaction, match.id, "ROUND_1_MAP_DOWNLOAD_TIMEOUT", now);
      return;
    }
    if (Boolean(round.ready_a_at) !== Boolean(round.ready_b_at)) {
      await this.finalizeMatch(
        transaction,
        match,
        round.ready_a_at ? "A" : "B",
        now,
      );
      return;
    }
    await this.cancelMatch(transaction, match.id, "ROUND_RESOURCE_TIMEOUT_BOTH", now);
  }

  private async resolveDeathmatchReadyTimeout(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    if (Boolean(match.ready_a_at) !== Boolean(match.ready_b_at)) {
      await this.finalizeMatch(
        transaction,
        match,
        match.ready_a_at ? "A" : "B",
        now,
      );
      return;
    }
    await this.cancelMatch(transaction, match.id, "DEATHMATCH_RESOURCE_TIMEOUT_BOTH", now);
  }

  private async cancelMatch(
    transaction: SqlExecutor,
    matchId: string,
    reason: string,
    now: Date,
  ): Promise<void> {
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'CANCELLED', cancellation_reason = $2, finished_at = $3,
           deadline_at = NULL, state_version = state_version + 1
       WHERE id = $1 AND state NOT IN ('MATCH_RESULT', 'CANCELLED')`,
      [matchId, reason, now.toISOString()],
    );
  }

  private async finalizeBans(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    if (match.state !== "BAN_PHASE") return;
    const config = this.matchConfig(match);
    if (!config.operational.timeouts) throw new Error("Missing timeout policy");
    const candidates = parseJson<RankedMapSnapshot[]>(match.candidate_maps_snapshot);
    const resolution = resolveBans(
      candidates,
      match.ban_a_confirmed_at ? match.ban_a_canonical_id : null,
      match.ban_b_confirmed_at ? match.ban_b_canonical_id : null,
      this.random,
    );
    const readyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET ban_a_confirmed_at = COALESCE(ban_a_confirmed_at, $2),
           ban_b_confirmed_at = COALESCE(ban_b_confirmed_at, $2),
           selected_round_maps_snapshot = $3::jsonb,
           state = 'ROUND_PREPARE', current_round_number = 1,
           deadline_at = $4, ban_deadline_at = NULL,
           state_version = state_version + 1
       WHERE id = $1 AND state = 'BAN_PHASE'`,
      [
        match.id,
        now.toISOString(),
        JSON.stringify(resolution.selectedRoundMaps),
        readyDeadline.toISOString(),
      ],
    );
    await this.insertPreparedRound(
      transaction,
      match.id,
      1,
      resolution.selectedRoundMaps[0]!,
      readyDeadline,
    );
  }

  private async insertPreparedRound(
    transaction: SqlExecutor,
    matchId: string,
    roundNumber: 1 | 2 | 3,
    map: RankedMapSnapshot,
    readyDeadline: Date,
  ): Promise<void> {
    await transaction.query(
      `INSERT INTO ranked_rounds (
         id, match_id, round_number, level_id, canonical_level_id,
         alternate_level_id, playable_level_id,
         title, creator, difficulty, pool, qualifying_percent,
         phase, ready_deadline_at
       ) VALUES (
         $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12,
         'ROUND_PREPARE', $13
       )
       ON CONFLICT (match_id, round_number) DO NOTHING`,
      [
        this.ids.next(),
        matchId,
        roundNumber,
        map.playableLevelId,
        map.canonicalLevelId,
        map.alternateLevelId,
        map.playableLevelId,
        map.title,
        map.creator,
        map.difficulty,
        map.pool,
        map.qualifyingPercent,
        readyDeadline.toISOString(),
      ],
    );
  }

  private async startPreparedRound(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    round: RoundRow,
    now: Date,
  ): Promise<void> {
    const config = this.matchConfig(match);
    const map: RankedMapSnapshot = {
      levelId: round.level_id,
      canonicalLevelId: round.canonical_level_id,
      alternateLevelId: round.alternate_level_id,
      playableLevelId: round.playable_level_id,
      title: round.title,
      creator: round.creator,
      difficulty: round.difficulty,
      pool: Number(round.pool) as RankedMapSnapshot["pool"],
      qualifyingPercent: Number(round.qualifying_percent),
    };
    const domain = createRound(round.round_number, map, config.operational.rules, now.getTime());
    await transaction.query(
      `UPDATE ranked_rounds
       SET phase = 'ROUND_PLAYING', domain_state = $2::jsonb,
           started_at = $3, normal_end_at = $4, final_window_end_at = $5,
           ready_deadline_at = NULL, state_version = $6
       WHERE id = $1`,
      [
        round.id,
        JSON.stringify(domain),
        now.toISOString(),
        new Date(domain.normalEndAtMs).toISOString(),
        new Date(domain.finalWindowEndAtMs).toISOString(),
        domain.stateVersion,
      ],
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'ROUND_PLAYING', deadline_at = $2,
           state_version = state_version + 1
       WHERE id = $1`,
      [match.id, new Date(domain.normalEndAtMs).toISOString()],
    );
    await this.outbox.enqueueMatchEvent(
      transaction,
      match,
      "ROUND_START",
      `match:${match.id}:round:${round.round_number}:start`,
      {
        roundNumber: Number(round.round_number),
        banner: bannerForRound(round.round_number),
        mapTitle: map.title,
        qualifyingPercent: map.qualifyingPercent,
      },
      now,
    );
  }

  private async persistRoundState(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    round: RoundRow,
    state: RoundState,
    now: Date,
  ): Promise<void> {
    await transaction.query(
      `UPDATE ranked_rounds
       SET phase = $2, domain_state = $3::jsonb,
           score_a = $4, score_b = $5, clears_a = $6, clears_b = $7,
           result = $8, result_reason = $9,
           two_clear_rule_triggered = $10,
           last_attempt_target = $11,
           last_attempt_window_start = $12,
           last_attempt_window_end = $13,
           settled_at = $14, state_version = $15
       WHERE id = $1`,
      [
        round.id,
        state.phase,
        JSON.stringify(state),
        state.scores.A,
        state.scores.B,
        state.clears.A,
        state.clears.B,
        state.outcome?.result ?? null,
        state.outcome?.reason ?? null,
        Boolean(state.lastAttemptWindow),
        state.lastAttemptWindow?.targetSide ?? null,
        state.lastAttemptWindow
          ? new Date(state.lastAttemptWindow.startedAtMs).toISOString()
          : null,
        state.lastAttemptWindow
          ? new Date(state.lastAttemptWindow.endsAtMs).toISOString()
          : null,
        state.outcome ? new Date(state.outcome.settledAtMs).toISOString() : null,
        state.stateVersion,
      ],
    );
    if (state.outcome) {
      this.runtimeState.clearRound(match.id, round.round_number);
      await this.settleRound(transaction, match, round, state, now);
      return;
    }
    await transaction.query(
      `UPDATE ranked_matches
       SET state = $2, deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, state.phase, this.domainDeadlineIso(state)],
    );
  }

  private async settleRound(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    round: RoundRow,
    roundState: RoundState,
    now: Date,
  ): Promise<void> {
    if (!roundState.outcome) return;
    if (round.result) return;
    const config = this.matchConfig(match);
    if (!config.operational.timeouts) throw new Error("Missing timeout policy");
    const series = parseJson<MatchSeriesState>(match.series_state);
    const decision = applyRoundResult(series, roundState.outcome.result);
    await transaction.query(
      `UPDATE ranked_rounds
       SET result = $2, result_reason = $3, phase = 'ROUND_RESULT',
           result_deadline_at = $4
       WHERE id = $1`,
      [
        round.id,
        roundState.outcome.result,
        roundState.outcome.reason,
        new Date(now.getTime() + config.operational.timeouts.roundResultSeconds * 1_000).toISOString(),
      ],
    );
    await this.outbox.enqueueMatchEvent(
      transaction,
      match,
      "ROUND_RESULT",
      `match:${match.id}:round:${round.round_number}:result`,
      {
        roundNumber: Number(round.round_number),
        result: roundState.outcome.result,
        reason: roundState.outcome.reason,
        scores: roundState.scores,
        clears: roundState.clears,
        roundWins: decision.state.roundWins,
        roundResults: decision.state.roundResults,
      },
      now,
    );

    if (decision.state.phase === "MATCH_RESULT" && decision.state.winner) {
      await transaction.query(
        "UPDATE ranked_matches SET series_state = $2::jsonb WHERE id = $1",
        [match.id, JSON.stringify(decision.state)],
      );
      await this.finalizeMatch(transaction, match, decision.state.winner, now);
      return;
    }
    if (decision.state.phase === "DEATHMATCH_REQUIRED") {
      await transaction.query(
        "UPDATE ranked_matches SET series_state = $2::jsonb WHERE id = $1",
        [match.id, JSON.stringify(decision.state)],
      );
      await this.prepareDeathmatch(transaction, match, now);
      return;
    }
    const resultDeadline = new Date(
      now.getTime() + config.operational.timeouts.roundResultSeconds * 1_000,
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET series_state = $2::jsonb, state = 'ROUND_RESULT',
           deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, JSON.stringify(decision.state), resultDeadline.toISOString()],
    );
  }

  private async prepareNextRound(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    const config = this.matchConfig(match);
    if (!config.operational.timeouts) throw new Error("Missing timeout policy");
    const series = parseJson<MatchSeriesState>(match.series_state);
    const roundNumber = (series.roundResults.length + 1) as 1 | 2 | 3;
    if (roundNumber < 2 || roundNumber > 3) {
      throw new Error("Series does not require another standard round");
    }
    const selected = parseJson<RankedMapSnapshot[]>(match.selected_round_maps_snapshot);
    const map = selected[roundNumber - 1];
    if (!map) throw new Error("Selected round map snapshot is missing");
    const readyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    await this.insertPreparedRound(transaction, match.id, roundNumber, map, readyDeadline);
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'ROUND_PREPARE', current_round_number = $2,
           deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, roundNumber, readyDeadline.toISOString()],
    );
  }

  private async finalizeMatch(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    winner: PlayerSide,
    now: Date,
  ): Promise<void> {
    const current = await this.lockMatch(transaction, match.id);
    if (current.result_applied_at) return;
    const config = this.matchConfig(current);
    if (!config.operational.mmrPolicy) throw new Error("Missing MMR policy");
    const profiles = await transaction.query<ProfileResultRow>(
      `SELECT player_id, hidden_mmr, placement_games
       FROM ranked_profiles
       WHERE player_id = $1 OR player_id = $2
       ORDER BY player_id
       FOR UPDATE`,
      [current.player_a_id, current.player_b_id],
    );
    const profileA = profiles.rows.find((profile) => profile.player_id === current.player_a_id);
    const profileB = profiles.rows.find((profile) => profile.player_id === current.player_b_id);
    if (!profileA || !profileB) throw new Error("Match profile is missing");
    const update = calculateMmrUpdate(
      {
        ratingA: Number(profileA.hidden_mmr),
        ratingB: Number(profileB.hidden_mmr),
        placementGamesA: Number(profileA.placement_games),
        placementGamesB: Number(profileB.placement_games),
        winner,
      },
      config.operational.mmrPolicy,
    );
    const placementA = Number(profileA.placement_games) + 1;
    const placementB = Number(profileB.placement_games) + 1;
    const displayedA = displayedTierForProfile(
      update.ratingAfterA,
      placementA,
      config.operational.mmrPolicy,
      config.operational.tierBands,
    );
    const displayedB = displayedTierForProfile(
      update.ratingAfterB,
      placementB,
      config.operational.mmrPolicy,
      config.operational.tierBands,
    );
    await transaction.query(
      `UPDATE ranked_profiles
       SET hidden_mmr = $2, visible_ranked_score = $2,
           displayed_tier = $3, placement_games = $4,
           wins = wins + $5, losses = losses + $6, updated_at = $7
       WHERE player_id = $1`,
      [
        current.player_a_id,
        update.ratingAfterA,
        displayedA,
        placementA,
        winner === "A" ? 1 : 0,
        winner === "B" ? 1 : 0,
        now.toISOString(),
      ],
    );
    await transaction.query(
      `UPDATE ranked_profiles
       SET hidden_mmr = $2, visible_ranked_score = $2,
           displayed_tier = $3, placement_games = $4,
           wins = wins + $5, losses = losses + $6, updated_at = $7
       WHERE player_id = $1`,
      [
        current.player_b_id,
        update.ratingAfterB,
        displayedB,
        placementB,
        winner === "B" ? 1 : 0,
        winner === "A" ? 1 : 0,
        now.toISOString(),
      ],
    );
    const winnerId = winner === "A" ? current.player_a_id : current.player_b_id;
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'MATCH_RESULT', winner_id = $2,
           mmr_delta_a = $3, mmr_delta_b = $4,
           mmr_a_after = $5, mmr_b_after = $6,
           result_applied_at = $7, finished_at = $7,
           deadline_at = NULL, state_version = state_version + 1
       WHERE id = $1 AND result_applied_at IS NULL`,
      [
        current.id,
        winnerId,
        update.deltaA,
        update.deltaB,
        update.ratingAfterA,
        update.ratingAfterB,
        now.toISOString(),
      ],
    );
    const finalSeries = parseJson<MatchSeriesState>(current.series_state);
    await this.outbox.enqueueMatchEvent(
      transaction,
      current,
      "MATCH_RESULT",
      `match:${current.id}:result`,
      {
        winnerSide: winner,
        roundResults: finalSeries.roundResults,
        mmrDelta: { A: update.deltaA, B: update.deltaB },
        ratingAfter: { A: update.ratingAfterA, B: update.ratingAfterB },
      },
      now,
    );
  }

  private async currentDeathmatch(
    transaction: SqlExecutor,
    match: LockedMatchRow,
  ): Promise<DeathmatchRow> {
    if (!match.current_deathmatch_id) throw new Error("Current deathmatch ID is missing");
    const result = await transaction.query<DeathmatchRow>(
      "SELECT * FROM ranked_deathmatches WHERE id = $1 FOR UPDATE",
      [match.current_deathmatch_id],
    );
    const deathmatch = result.rows[0];
    if (!deathmatch) throw new Error("Current deathmatch row is missing");
    return deathmatch;
  }

  private async startDeathmatchAttempt(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    side: PlayerSide,
    playerId: string,
    body: AttemptStartDto,
    now: Date,
  ) {
    const deathmatch = await this.currentDeathmatch(transaction, match);
    const map = parseJson<RankedMapSnapshot>(deathmatch.map_snapshot);
    this.assertPlayableLevelId(body.levelId, map.playableLevelId);
    const duplicate = await transaction.query<DeathmatchAttemptRow>(
      `SELECT * FROM ranked_deathmatch_attempts
       WHERE deathmatch_id = $1 AND player_id = $2 AND client_start_event_id = $3`,
      [deathmatch.id, playerId, body.clientEventId],
    );
    if (duplicate.rows[0]) {
      return {
        accepted: true,
        duplicate: true,
        attemptId: duplicate.rows[0].id,
        reason: null,
        serverNow: now.toISOString(),
      };
    }
    const attempts = await transaction.query<DeathmatchAttemptRow>(
      `SELECT * FROM ranked_deathmatch_attempts
       WHERE deathmatch_id = $1 AND player_id = $2
       ORDER BY attempt_sequence FOR UPDATE`,
      [deathmatch.id, playerId],
    );
    if (attempts.rows.some((attempt) => attempt.ended_at === null)) {
      return {
        accepted: false,
        duplicate: false,
        attemptId: null,
        reason: "ATTEMPT_ALREADY_ACTIVE",
        serverNow: now.toISOString(),
      };
    }
    if (attempts.rows.length >= 3) {
      return {
        accepted: false,
        duplicate: false,
        attemptId: null,
        reason: "DEATHMATCH_ATTEMPTS_EXHAUSTED",
        serverNow: now.toISOString(),
      };
    }
    const attemptId = this.ids.next();
    await transaction.query(
      `INSERT INTO ranked_deathmatch_attempts (
         id, deathmatch_id, player_id, attempt_sequence,
         server_accepted_start_at, level_id, client_start_event_id
       ) VALUES ($1, $2, $3, $4, $5, $6, $7)`,
      [
        attemptId,
        deathmatch.id,
        playerId,
        attempts.rows.length + 1,
        now.toISOString(),
        body.levelId,
        body.clientEventId,
      ],
    );
    await transaction.query(
      "UPDATE ranked_matches SET state_version = state_version + 1 WHERE id = $1",
      [match.id],
    );
    return {
      accepted: true,
      duplicate: false,
      attemptId,
      attemptNumber: attempts.rows.length + 1,
      side,
      reason: null,
      serverNow: now.toISOString(),
    };
  }

  private async endDeathmatchAttempt(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    _side: PlayerSide,
    playerId: string,
    body: AttemptEndDto,
    now: Date,
  ) {
    const deathmatch = await this.currentDeathmatch(transaction, match);
    const deathmatchMap = parseJson<RankedMapSnapshot>(deathmatch.map_snapshot);
    this.assertPlayableLevelId(body.levelId, deathmatchMap.playableLevelId);
    const duplicate = await transaction.query<DeathmatchAttemptRow>(
      `SELECT * FROM ranked_deathmatch_attempts
       WHERE deathmatch_id = $1 AND player_id = $2 AND client_end_event_id = $3`,
      [deathmatch.id, playerId, body.clientEventId],
    );
    if (duplicate.rows[0]) {
      return {
        accepted: true,
        duplicate: true,
        reason: null,
        serverNow: now.toISOString(),
      };
    }
    const attemptResult = await transaction.query<DeathmatchAttemptRow>(
      `SELECT * FROM ranked_deathmatch_attempts
       WHERE id = $1 AND deathmatch_id = $2 AND player_id = $3
       FOR UPDATE`,
      [body.attemptId, deathmatch.id, playerId],
    );
    const attempt = attemptResult.rows[0];
    if (!attempt) {
      return {
        accepted: false,
        duplicate: false,
        reason: "ATTEMPT_NOT_FOUND",
        serverNow: now.toISOString(),
      };
    }
    if (attempt.ended_at !== null) {
      return {
        accepted: false,
        duplicate: false,
        reason: "ATTEMPT_ALREADY_ENDED",
        serverNow: now.toISOString(),
      };
    }
    const map = deathmatchMap;
    const awardedScore = scoreAttempt(
      body.progressPercent,
      body.cleared,
      map.qualifyingPercent,
    );
    await transaction.query(
      `UPDATE ranked_deathmatch_attempts
       SET ended_at = $2, progress_percent = $3, cleared = $4,
           awarded_score = $5, client_end_event_id = $6
       WHERE id = $1`,
      [
        attempt.id,
        now.toISOString(),
        body.progressPercent,
        body.cleared,
        awardedScore,
        body.clientEventId,
      ],
    );
    if (attempt.valid && body.cleared) {
      const totals = await transaction.query<{
        score_a: number;
        score_b: number;
        clears_a: number;
        clears_b: number;
      }>(
        `SELECT
           COALESCE(SUM(awarded_score) FILTER (WHERE player_id = $2 AND valid), 0)::int AS score_a,
           COALESCE(SUM(awarded_score) FILTER (WHERE player_id = $3 AND valid), 0)::int AS score_b,
           COUNT(*) FILTER (WHERE player_id = $2 AND valid AND cleared)::int AS clears_a,
           COUNT(*) FILTER (WHERE player_id = $3 AND valid AND cleared)::int AS clears_b
         FROM ranked_deathmatch_attempts
         WHERE deathmatch_id = $1`,
        [deathmatch.id, match.player_a_id, match.player_b_id],
      );
      const currentTotals = totals.rows[0];
      await this.outbox.enqueueMatchEvent(
        transaction,
        match,
        "CLEAR_EVENT",
        `match:${match.id}:deathmatch:${deathmatch.id}:clear:${attempt.id}`,
        {
          phase: "DEATHMATCH",
          deathmatchSequence: Number(deathmatch.sequence),
          attemptNumber: Number(attempt.attempt_sequence),
          mapTitle: map.title,
          side: playerId === match.player_a_id ? "A" : "B",
          scores: { A: Number(currentTotals?.score_a ?? 0), B: Number(currentTotals?.score_b ?? 0) },
          clears: {
            A: Number(currentTotals?.clears_a ?? 0),
            B: Number(currentTotals?.clears_b ?? 0),
          },
        },
        now,
      );
    }
    await transaction.query(
      "UPDATE ranked_matches SET state_version = state_version + 1 WHERE id = $1",
      [match.id],
    );
    await this.settleDeathmatchIfComplete(transaction, match, now);
    return {
      accepted: true,
      duplicate: false,
      reason: null,
      awardedScore,
      serverNow: now.toISOString(),
    };
  }

  private async settleDeathmatchIfComplete(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    if (match.state !== "DEATHMATCH_PLAYING" || !match.current_deathmatch_id) return;
    const deathmatch = await this.currentDeathmatch(transaction, match);
    if (deathmatch.finished_at) return;
    const attempts = await transaction.query<DeathmatchAttemptRow>(
      `SELECT * FROM ranked_deathmatch_attempts
       WHERE deathmatch_id = $1
       ORDER BY player_id, attempt_sequence`,
      [deathmatch.id],
    );
    const attemptsA = attempts.rows.filter((attempt) => attempt.player_id === match.player_a_id);
    const attemptsB = attempts.rows.filter((attempt) => attempt.player_id === match.player_b_id);
    if (
      attemptsA.length !== 3 ||
      attemptsB.length !== 3 ||
      attemptsA.some((attempt) => attempt.ended_at === null) ||
      attemptsB.some((attempt) => attempt.ended_at === null)
    ) return;
    const map = parseJson<RankedMapSnapshot>(deathmatch.map_snapshot);
    const evaluation = evaluateDeathmatch(
      map,
      attemptsA.map((attempt) => ({
        progressPercent: attempt.valid ? Number(attempt.progress_percent ?? 0) : 0,
        cleared: attempt.valid ? Boolean(attempt.cleared) : false,
      })),
      attemptsB.map((attempt) => ({
        progressPercent: attempt.valid ? Number(attempt.progress_percent ?? 0) : 0,
        cleared: attempt.valid ? Boolean(attempt.cleared) : false,
      })),
    );
    const winnerId = evaluation.winner === "A"
      ? match.player_a_id
      : evaluation.winner === "B"
        ? match.player_b_id
        : null;
    await transaction.query(
      `UPDATE ranked_deathmatches
       SET score_a = $2, score_b = $3, winner_id = $4, finished_at = $5
       WHERE id = $1 AND finished_at IS NULL`,
      [
        deathmatch.id,
        evaluation.scoreA,
        evaluation.scoreB,
        winnerId,
        now.toISOString(),
      ],
    );
    await this.outbox.enqueueMatchEvent(
      transaction,
      match,
      "DEATHMATCH_RESULT",
      `match:${match.id}:deathmatch:${deathmatch.id}:result`,
      {
        sequence: Number(deathmatch.sequence),
        scoreA: evaluation.scoreA,
        scoreB: evaluation.scoreB,
        winnerSide: evaluation.winner,
        repeatRequired: evaluation.repeatRequired,
      },
      now,
    );
    if (evaluation.repeatRequired) {
      const resultSeconds = this.matchConfig(match).operational.timeouts?.roundResultSeconds;
      if (!resultSeconds) throw new Error("Missing result display timeout");
      const deadline = new Date(now.getTime() + resultSeconds * 1_000);
      await transaction.query(
        `UPDATE ranked_matches
         SET state = 'DEATHMATCH_RESULT', deadline_at = $2,
             state_version = state_version + 1
         WHERE id = $1`,
        [match.id, deadline.toISOString()],
      );
      return;
    }
    if (!evaluation.winner) throw new Error("Deathmatch result has no winner");
    const series = applyDeathmatchWinner(
      parseJson<MatchSeriesState>(match.series_state),
      evaluation.winner,
    );
    await transaction.query(
      "UPDATE ranked_matches SET series_state = $2::jsonb WHERE id = $1",
      [match.id, JSON.stringify(series)],
    );
    await this.finalizeMatch(transaction, match, evaluation.winner, now);
  }

  private async prepareDeathmatch(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    now: Date,
  ): Promise<void> {
    const config = this.matchConfig(match);
    if (!config.operational.timeouts) throw new Error("Missing timeout policy");
    const prior = await transaction.query<{ map_snapshot: unknown; sequence: number }>(
      "SELECT map_snapshot, sequence FROM ranked_deathmatches WHERE match_id = $1 ORDER BY sequence",
      [match.id],
    );
    const priorCanonicalIds = prior.rows.map(
      (row) => parseJson<RankedMapSnapshot>(row.map_snapshot).canonicalLevelId,
    );
    const effectiveTier = match.effective_tier as RankedConfigSnapshot["operational"]["tierBands"][number]["tier"];
    const band = tierBandFor(effectiveTier, config.operational.tierBands);
    const map = selectDeathmatchMap(
      band.deathmatchPool,
      config.maps,
      priorCanonicalIds,
      this.random,
    );
    const deathmatchId = this.ids.next();
    const sequence = prior.rows.length + 1;
    const readyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    await transaction.query(
      `INSERT INTO ranked_deathmatches (id, match_id, sequence, map_snapshot)
       VALUES ($1, $2, $3, $4::jsonb)`,
      [deathmatchId, match.id, sequence, JSON.stringify(map)],
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'DEATHMATCH_PREPARE', current_deathmatch_id = $2,
           ready_a_at = NULL, ready_b_at = NULL, ready_deadline_at = $3,
           deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, deathmatchId, readyDeadline.toISOString()],
    );
  }

  private async roundSummaries(executor: SqlExecutor, matchId: string) {
    const rows = await executor.query<RoundSummaryRow>(
      `SELECT round_number, title, difficulty, score_a, score_b, clears_a, clears_b, result
       FROM ranked_rounds WHERE match_id = $1 ORDER BY round_number`,
      [matchId],
    );
    return rows.rows.map((round) => ({
      roundNumber: Number(round.round_number),
      mapTitle: round.title,
      difficulty: round.difficulty,
      scoreA: Number(round.score_a ?? 0),
      scoreB: Number(round.score_b ?? 0),
      clearsA: Number(round.clears_a ?? 0),
      clearsB: Number(round.clears_b ?? 0),
      result: round.result,
    }));
  }

  private async deathmatchSummaries(executor: SqlExecutor, matchId: string) {
    const rows = await executor.query<{
      sequence: number;
      map_snapshot: unknown;
      score_a: number | null;
      score_b: number | null;
      winner_id: string | null;
      finished_at: Date | string | null;
    }>(
      `SELECT sequence, map_snapshot, score_a, score_b, winner_id, finished_at
       FROM ranked_deathmatches WHERE match_id = $1 ORDER BY sequence`,
      [matchId],
    );
    const match = await executor.query<{ player_a_id: string; player_b_id: string }>(
      "SELECT player_a_id, player_b_id FROM ranked_matches WHERE id = $1",
      [matchId],
    );
    const participants = match.rows[0];
    return rows.rows.map((deathmatch) => {
      const map = parseJson<RankedMapSnapshot>(deathmatch.map_snapshot);
      return {
        sequence: Number(deathmatch.sequence),
        mapTitle: map.title,
        difficulty: map.difficulty,
        scoreA: deathmatch.score_a === null ? null : Number(deathmatch.score_a),
        scoreB: deathmatch.score_b === null ? null : Number(deathmatch.score_b),
        winnerSide:
          deathmatch.winner_id && deathmatch.winner_id === participants?.player_a_id
            ? "A"
            : deathmatch.winner_id && deathmatch.winner_id === participants?.player_b_id
              ? "B"
              : null,
        finishedAt: dateIso(deathmatch.finished_at),
      };
    });
  }

  private async buildPublicState(
    transaction: SqlExecutor,
    match: LockedMatchRow,
    viewerPlayerId: string,
    now: Date,
  ) {
    const players = await transaction.query<{
      id: string;
      gd_account_id: string;
      gd_username: string;
      displayed_tier: string;
      visible_ranked_score: number | null;
    }>(
      `SELECT p.id, p.gd_account_id::text AS gd_account_id, p.gd_username,
              rp.displayed_tier, rp.visible_ranked_score
       FROM ranked_players p
       JOIN ranked_profiles rp ON rp.player_id = p.id
       WHERE p.id = $1 OR p.id = $2`,
      [match.player_a_id, match.player_b_id],
    );
    const playerA = players.rows.find((player) => player.id === match.player_a_id);
    const playerB = players.rows.find((player) => player.id === match.player_b_id);
    const viewerSide: PlayerSide = viewerPlayerId === match.player_a_id ? "A" : "B";
    const opponentSide: PlayerSide = viewerSide === "A" ? "B" : "A";
    const config = this.matchConfig(match);
    let currentRound: object | null = null;
    let spectator: {
      active: boolean;
      opponentName?: string;
      currentProgress?: number | null;
    } = { active: false };
    let ready = { A: Boolean(match.ready_a_at), B: Boolean(match.ready_b_at) };
    if (match.current_round_number) {
      const roundResult = await transaction.query<RoundRow>(
        "SELECT * FROM ranked_rounds WHERE match_id = $1 AND round_number = $2",
        [match.id, match.current_round_number],
      );
      const round = roundResult.rows[0];
      if (round) {
        ready = { A: Boolean(round.ready_a_at), B: Boolean(round.ready_b_at) };
        const domain = round.domain_state ? this.roundState(round) : null;
        const progressA = this.runtimeState.progress(match.id, round.round_number, "A");
        const progressB = this.runtimeState.progress(match.id, round.round_number, "B");
        const qualifyingPercent = Number(round.qualifying_percent);
        const committedScores = domain?.scores ?? { A: 0, B: 0 };
        const displayScores = {
          A: committedScores.A + (progressA
            ? scoreAttempt(progressA.progressPercent, false, qualifyingPercent)
            : 0),
          B: committedScores.B + (progressB
            ? scoreAttempt(progressB.progressPercent, false, qualifyingPercent)
            : 0),
        };
        currentRound = {
          roundNumber: Number(round.round_number),
          banner: bannerForRound(round.round_number),
          phase: round.phase,
          map: {
            levelId: round.level_id,
            canonicalLevelId: round.canonical_level_id,
            alternateLevelId: round.alternate_level_id,
            playableLevelId: round.playable_level_id,
            title: round.title,
            creator: round.creator,
            difficulty: round.difficulty,
            pool: Number(round.pool),
            qualifyingPercent,
          },
          scores: committedScores,
          displayScores,
          clears: domain?.clears ?? { A: 0, B: 0 },
          lastAttemptWindow: domain?.lastAttemptWindow ?? null,
          outcome: domain?.outcome ?? null,
        };
        if (
          (domain?.phase === "LAST_ATTEMPT_WINDOW" || domain?.phase === "ROUND_SETTLING") &&
          domain.lastAttemptWindow?.triggerSide === viewerSide &&
          domain.lastAttemptWindow.targetSide === opponentSide &&
          domain.clears[viewerSide] === 2 &&
          domain.clears[opponentSide] <= 1
        ) {
          const progress = this.runtimeState.progress(
            match.id,
            round.round_number,
            opponentSide,
          );
          const opponent = opponentSide === "A" ? playerA : playerB;
          spectator = {
            active: true,
            opponentName: opponent?.gd_username ?? "Opponent",
            currentProgress: progress?.progressPercent ?? null,
          };
        }
      }
    }
    let deathmatch: object | null = null;
    if (match.state.startsWith("DEATHMATCH")) {
      const result = await transaction.query<{
        sequence: number;
        map_snapshot: unknown;
        score_a: number | null;
        score_b: number | null;
        winner_id: string | null;
      }>(
        "SELECT sequence, map_snapshot, score_a, score_b, winner_id FROM ranked_deathmatches WHERE id = $1",
        [match.current_deathmatch_id],
      );
      const current = result.rows[0];
      if (current) {
        deathmatch = {
          sequence: Number(current.sequence),
          map: parseJson<RankedMapSnapshot>(current.map_snapshot),
          scoreA: current.score_a === null ? null : Number(current.score_a),
          scoreB: current.score_b === null ? null : Number(current.score_b),
          winnerSide:
            current.winner_id === match.player_a_id
              ? "A"
              : current.winner_id === match.player_b_id
                ? "B"
                : null,
        };
      }
    }
    const series = parseJson<MatchSeriesState>(match.series_state);
    const bansVisible = !["MATCHED", "BAN_PHASE"].includes(match.state);
    const rounds = await this.roundSummaries(transaction, match.id);
    const deathmatches = await this.deathmatchSummaries(transaction, match.id);
    let profileAfter: Record<PlayerSide, object | null> | null = null;
    let profileBefore: Record<PlayerSide, object | null> | null = null;
    if (match.state === "MATCH_RESULT") {
      const profiles = await transaction.query<PublicProfileRow>(
        `SELECT player_id, displayed_tier, visible_ranked_score, placement_games
         FROM ranked_profiles WHERE player_id = $1 OR player_id = $2`,
        [match.player_a_id, match.player_b_id],
      );
      const a = profiles.rows.find((profile) => profile.player_id === match.player_a_id);
      const b = profiles.rows.find((profile) => profile.player_id === match.player_b_id);
      profileAfter = {
        A: a
          ? { displayedTier: a.displayed_tier, visibleRankedScore: Number(a.visible_ranked_score ?? 0), placementGames: Number(a.placement_games) }
          : null,
        B: b
          ? { displayedTier: b.displayed_tier, visibleRankedScore: Number(b.visible_ranked_score ?? 0), placementGames: Number(b.placement_games) }
          : null,
      };
      if (!config.operational.mmrPolicy) throw new Error("Missing MMR policy");
      profileBefore = {
        A: a
          ? {
              displayedTier: displayedTierForProfile(
                Number(match.mmr_a_before),
                Math.max(0, Number(a.placement_games) - 1),
                config.operational.mmrPolicy,
                config.operational.tierBands,
              ),
              visibleRankedScore: Number(match.mmr_a_before),
            }
          : null,
        B: b
          ? {
              displayedTier: displayedTierForProfile(
                Number(match.mmr_b_before),
                Math.max(0, Number(b.placement_games) - 1),
                config.operational.mmrPolicy,
                config.operational.tierBands,
              ),
              visibleRankedScore: Number(match.mmr_b_before),
            }
          : null,
      };
    }
    return {
      matchId: match.id,
      state: match.state,
      stateVersion: Number(match.state_version),
      deadlineAt: dateIso(match.deadline_at),
      serverNow: now.toISOString(),
      rulesVersion: config.operational.rules.rulesVersion,
      configGeneration: config.generation,
      effectiveTier: match.effective_tier,
      matchType: match.match_type,
      debug: match.match_type === "DEBUG_BOT",
      side: viewerSide,
      ready,
      players: {
        A: playerA
          ? {
              gdAccountId: playerA.gd_account_id,
              gdUsername: playerA.gd_username,
              displayedTier: playerA.displayed_tier,
              visibleRankedScore: Number(playerA.visible_ranked_score ?? 0),
            }
          : null,
        B: playerB
          ? {
              gdAccountId: playerB.gd_account_id,
              gdUsername: playerB.gd_username,
              displayedTier: playerB.displayed_tier,
              visibleRankedScore: Number(playerB.visible_ranked_score ?? 0),
            }
          : null,
      },
      candidateMaps:
        match.state === "BAN_PHASE"
          ? parseJson<RankedMapSnapshot[]>(match.candidate_maps_snapshot)
          : null,
      bans: bansVisible
        ? { A: match.ban_a_canonical_id, B: match.ban_b_canonical_id }
        : null,
      series,
      rounds,
      currentRound,
      spectator,
      deathmatch,
      deathmatches,
      result:
        match.state === "MATCH_RESULT"
          ? {
              winnerSide: match.winner_id === match.player_a_id ? "A" : "B",
              mmrDelta: { A: match.mmr_delta_a, B: match.mmr_delta_b },
              ratingAfter: { A: match.mmr_a_after, B: match.mmr_b_after },
              profileBefore,
              profileAfter,
            }
          : null,
      cancellation:
        match.state === "CANCELLED"
          ? { cancelled: true, reason: match.cancellation_reason }
          : null,
    };
  }
}
