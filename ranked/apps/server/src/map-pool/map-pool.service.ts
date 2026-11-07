import { Inject, Injectable, NotFoundException } from "@nestjs/common";
import type { RankedMap } from "@corum-ranked/rules";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import {
  mapDifficultyRank, mapDetailStats, myMapRecord, percentOf,
  type AttemptRow, type DeathmatchAttemptRow, type DeathmatchRow, type MapDetailStats, type RoundRow,
} from "./map-pool-stats.js";

/** Pool rows and per-map statistics are shared by every viewer for this long. */
export const MAP_POOL_CACHE_MS = 30_000;

export interface MapPoolEntry {
  readonly canonicalLevelId: string;
  readonly levelId: string;
  readonly title: string;
  readonly creator: string;
  readonly difficulty: string;
  /** GD difficulty label for the face and name ("Extreme Demon"), when known. */
  readonly gdDifficulty: string | null;
  /** The map sheet's numeric Corum difficulty rating, when it is numeric. */
  readonly rating: number | null;
  readonly difficultyRank: number;
  readonly pool: number;
  readonly qualifyingPercent: number;
  readonly kind: string;
  readonly stars: number | null;
  readonly gddlTier: number | null;
  readonly gddlColor: string | null;
  readonly durationSeconds: number | null;
  readonly rounds: number;
  readonly share: number | null;
  readonly draws: number;
  readonly drawRate: number | null;
  readonly banOffers: number;
  readonly bans: number;
  readonly banRate: number | null;
}
export interface MapPoolList {
  readonly generatedAt: string;
  readonly totals: { readonly rounds: number; readonly draws: number; readonly drawRate: number | null };
  readonly mostPlayed: string | null;
  readonly mostBanned: string | null;
  readonly maps: readonly MapPoolEntry[];
}

interface Cached<T> { readonly at: number; readonly generation: string; readonly value: T }
type Time = Date | string | null;
const epoch = (value: Time): number | null => value === null ? null : new Date(value).getTime();
const num = (value: unknown): number => Number(value ?? 0);

// Every settled match counts: bot matches are treated as ordinary matches.
// Unsettled rounds never do.
const SETTLED_ROUNDS = `ranked_rounds r JOIN ranked_matches m ON m.id = r.match_id
  WHERE m.match_type IN ('RANKED_PVP', 'DEBUG_BOT') AND r.result IS NOT NULL`;

@Injectable()
export class MapPoolService {
  private list: Cached<MapPoolList> | null = null;
  private listLoad: Promise<MapPoolList> | null = null;
  private readonly details = new Map<string, Cached<{ stats: MapDetailStats; rounds: RoundRow[]; attempts: AttemptRow[] }>>();

  public constructor(
    @Inject(RankedConfigService) private readonly config: RankedConfigService,
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
  ) {}

  private activeMaps(): { generation: string; maps: RankedMap[] } {
    const snapshot = this.config.getSnapshot();
    return { generation: snapshot.generation, maps: snapshot.maps.filter((map) => map.active) };
  }

  public async pool(): Promise<MapPoolList> {
    const { generation } = this.activeMaps();
    const now = this.clock.now().getTime();
    if (this.list && this.list.generation === generation && now - this.list.at < MAP_POOL_CACHE_MS) return this.list.value;
    // One shared read while the cache refills; concurrent refreshes wait for it.
    this.listLoad ??= this.readPool().finally(() => { this.listLoad = null; });
    return this.listLoad;
  }

  private async readPool(): Promise<MapPoolList> {
    const { generation, maps } = this.activeMaps();
    const now = this.clock.now();
    const [rounds, bans] = await Promise.all([
      this.database.query<{ id: string; rounds: number; draws: number }>(
        `SELECT r.canonical_level_id AS id, COUNT(*)::int AS rounds,
                COUNT(*) FILTER (WHERE r.result = 'DRAW')::int AS draws
         FROM ${SETTLED_ROUNDS}
         GROUP BY r.canonical_level_id`),
      // A ban counts once per match even when both players banned the same map.
      // Offers only count series with bans on whose ban phase finished.
      this.database.query<{ id: string; offers: number; bans: number }>(
        `SELECT c->>'canonicalLevelId' AS id, COUNT(*)::int AS offers,
                COUNT(*) FILTER (WHERE m.ban_a_canonical_id = c->>'canonicalLevelId'
                                    OR m.ban_b_canonical_id = c->>'canonicalLevelId')::int AS bans
         FROM ranked_matches m
         CROSS JOIN LATERAL jsonb_array_elements(CASE WHEN jsonb_typeof(m.candidate_maps_snapshot) = 'array'
           THEN m.candidate_maps_snapshot ELSE '[]'::jsonb END) c
         WHERE m.match_type IN ('RANKED_PVP', 'DEBUG_BOT') AND m.selected_round_maps_snapshot IS NOT NULL
           AND COALESCE(m.series_state->>'mapBan', 'true') <> 'false'
         GROUP BY 1`),
    ]);
    const roundsById = new Map(rounds.rows.map((row) => [row.id, { rounds: num(row.rounds), draws: num(row.draws) }]));
    const bansById = new Map(bans.rows.map((row) => [row.id, { offers: num(row.offers), bans: num(row.bans) }]));
    const totalRounds = rounds.rows.reduce((sum, row) => sum + num(row.rounds), 0);
    const totalDraws = rounds.rows.reduce((sum, row) => sum + num(row.draws), 0);
    const entries = maps.map((map): MapPoolEntry => {
      const played = roundsById.get(map.canonicalLevelId) ?? { rounds: 0, draws: 0 };
      const banned = bansById.get(map.canonicalLevelId) ?? { offers: 0, bans: 0 };
      return {
        canonicalLevelId: map.canonicalLevelId,
        levelId: map.playableLevelId,
        title: map.title,
        creator: map.creator,
        difficulty: map.difficulty,
        gdDifficulty: map.metadata?.gdDifficulty ?? null,
        rating: map.difficulty.trim() !== "" && Number.isFinite(Number(map.difficulty)) ? Number(map.difficulty) : null,
        difficultyRank: mapDifficultyRank(map.metadata?.gdDifficulty, map.metadata?.kind, map.metadata?.stars),
        pool: map.pool,
        qualifyingPercent: map.qualifyingPercent,
        kind: map.metadata?.kind ?? "UNKNOWN",
        stars: map.metadata?.stars ?? null,
        gddlTier: map.metadata?.gddlTier ?? null,
        gddlColor: map.metadata?.gddlColor ?? null,
        durationSeconds: map.metadata?.durationSeconds ?? null,
        rounds: played.rounds,
        share: percentOf(played.rounds, totalRounds),
        draws: played.draws,
        drawRate: percentOf(played.draws, played.rounds),
        banOffers: banned.offers,
        bans: banned.bans,
        banRate: percentOf(banned.bans, banned.offers),
      };
    }).sort((a, b) => a.difficultyRank - b.difficultyRank || (a.gddlTier ?? 0) - (b.gddlTier ?? 0) ||
      (a.rating ?? 0) - (b.rating ?? 0) ||
      a.pool - b.pool || a.title.localeCompare(b.title) || a.canonicalLevelId.localeCompare(b.canonicalLevelId));
    const top = (value: (entry: MapPoolEntry) => number) => entries.reduce<MapPoolEntry | null>(
      (best, entry) => value(entry) > 0 && (!best || value(entry) > value(best)) ? entry : best, null)?.canonicalLevelId ?? null;
    const list: MapPoolList = {
      generatedAt: now.toISOString(),
      totals: { rounds: totalRounds, draws: totalDraws, drawRate: percentOf(totalDraws, totalRounds) },
      mostPlayed: top((entry) => entry.rounds),
      mostBanned: top((entry) => entry.banRate !== null && entry.banOffers > 0 ? entry.bans : 0),
      maps: entries,
    };
    this.list = { at: now.getTime(), generation, value: list };
    return list;
  }

  public async detail(canonicalLevelId: string, playerId: string) {
    const list = await this.pool();
    const map = list.maps.find((entry) => entry.canonicalLevelId === canonicalLevelId);
    if (!map) throw new NotFoundException("This map is not in the Solo map pool");
    const { generation } = this.activeMaps();
    const now = this.clock.now().getTime();
    let cached = this.details.get(canonicalLevelId);
    if (!cached || cached.generation !== generation || now - cached.at >= MAP_POOL_CACHE_MS) {
      const value = await this.readDetail(canonicalLevelId);
      cached = { at: now, generation, value };
      this.details.set(canonicalLevelId, cached);
      // The pool is small; drop entries of maps that left it.
      for (const id of this.details.keys()) if (!list.maps.some((entry) => entry.canonicalLevelId === id)) this.details.delete(id);
    }
    const ranked = [...list.maps].sort((a, b) => b.rounds - a.rounds || a.canonicalLevelId.localeCompare(b.canonicalLevelId));
    return {
      generatedAt: list.generatedAt,
      map,
      shareRank: map.rounds > 0 ? ranked.findIndex((entry) => entry.canonicalLevelId === canonicalLevelId) + 1 : null,
      mapCount: list.maps.length,
      totals: list.totals,
      stats: cached.value.stats,
      mine: myMapRecord(playerId, cached.value.rounds, cached.value.attempts),
    };
  }

  private async readDetail(canonicalLevelId: string) {
    const [roundRows, attemptRows, deathmatchRows, deathmatchAttemptRows] = await Promise.all([
      this.database.query<{ id: string; result: "A" | "B" | "DRAW"; effective_tier: string; player_a_id: string; player_b_id: string }>(
        `SELECT r.id, r.result, m.effective_tier, m.player_a_id, m.player_b_id
         FROM ${SETTLED_ROUNDS} AND r.canonical_level_id = $1`, [canonicalLevelId]),
      this.database.query<{ round_id: string; player_id: string; attempt_sequence: number; ended_at: Time;
        progress_percent: string | number | null; cleared: boolean; awarded_score: string | number }>(
        `SELECT a.round_id, a.player_id, a.attempt_sequence, a.ended_at, a.progress_percent, a.cleared, a.awarded_score
         FROM ranked_attempts a JOIN ranked_rounds r ON r.id = a.round_id JOIN ranked_matches m ON m.id = r.match_id
         WHERE m.match_type IN ('RANKED_PVP', 'DEBUG_BOT') AND r.result IS NOT NULL
           AND r.canonical_level_id = $1 AND a.valid = TRUE AND a.ended_at IS NOT NULL`, [canonicalLevelId]),
      this.database.query<{ id: string; player_a_id: string; player_b_id: string; winner_id: string; started_at: Time; finished_at: Time }>(
        `SELECT d.id, m.player_a_id, m.player_b_id, d.winner_id, d.started_at, d.finished_at
         FROM ranked_deathmatches d JOIN ranked_matches m ON m.id = d.match_id
         WHERE m.match_type IN ('RANKED_PVP', 'DEBUG_BOT') AND d.winner_id IS NOT NULL
           AND d.map_snapshot->>'canonicalLevelId' = $1`, [canonicalLevelId]),
      this.database.query<{ deathmatch_id: string; player_id: string; ended_at: Time; progress_percent: string | number | null; cleared: boolean }>(
        `SELECT a.deathmatch_id, a.player_id, a.ended_at, a.progress_percent, a.cleared
         FROM ranked_deathmatch_attempts a JOIN ranked_deathmatches d ON d.id = a.deathmatch_id
         JOIN ranked_matches m ON m.id = d.match_id
         WHERE m.match_type IN ('RANKED_PVP', 'DEBUG_BOT') AND d.winner_id IS NOT NULL
           AND d.map_snapshot->>'canonicalLevelId' = $1 AND a.valid = TRUE AND a.ended_at IS NOT NULL`, [canonicalLevelId]),
    ]);
    const rounds: RoundRow[] = roundRows.rows.map((row) => ({
      id: row.id, result: row.result, effectiveTier: row.effective_tier, playerA: row.player_a_id, playerB: row.player_b_id }));
    const attempts: AttemptRow[] = attemptRows.rows.map((row) => ({
      roundId: row.round_id, playerId: row.player_id, sequence: num(row.attempt_sequence), endedAt: epoch(row.ended_at) ?? 0,
      progress: num(row.progress_percent), cleared: row.cleared, score: num(row.awarded_score) }));
    const deathmatches: DeathmatchRow[] = deathmatchRows.rows.map((row) => ({
      id: row.id, playerA: row.player_a_id, playerB: row.player_b_id, winner: row.winner_id,
      startedAt: epoch(row.started_at), finishedAt: epoch(row.finished_at) }));
    const deathmatchAttempts: DeathmatchAttemptRow[] = deathmatchAttemptRows.rows.map((row) => ({
      deathmatchId: row.deathmatch_id, playerId: row.player_id, endedAt: epoch(row.ended_at) ?? 0,
      progress: num(row.progress_percent), cleared: row.cleared }));
    const players = [...new Set([...rounds.flatMap((row) => [row.playerA, row.playerB]), ...deathmatches.map((row) => row.winner)])];
    const names = new Map<string, string>();
    if (players.length > 0) {
      const result = await this.database.query<{ id: string; gd_username: string }>(
        "SELECT id, gd_username FROM ranked_players WHERE id = ANY($1::uuid[])", [players]);
      for (const row of result.rows) names.set(row.id, row.gd_username);
    }
    return { stats: mapDetailStats({ rounds, attempts, deathmatches, deathmatchAttempts, names }), rounds, attempts };
  }
}
