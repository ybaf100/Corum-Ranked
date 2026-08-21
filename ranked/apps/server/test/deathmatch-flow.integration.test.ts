import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import {
  SeededRandom,
  createMatchSeries,
  type CsmpTier,
  type RankedMapSnapshot,
} from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { MatchAccessService } from "../src/match/match-access.service.js";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { OutboxService } from "../src/relay/outbox.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  const migrationPath = fileURLToPath(
    new URL("../../../migrations/0001_initial_ranked.sql", import.meta.url),
  );
  await pglite.exec(await readFile(migrationPath, "utf8"));
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

class SequenceIds implements IdGenerator {
  private value = 2_000;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8003-${String(this.value).padStart(12, "0")}`;
  }
}

class MutableClock implements ServerClock {
  private milliseconds = Date.parse("2026-08-20T03:00:00.000Z");
  public now(): Date {
    return new Date(this.milliseconds);
  }
  public advance(seconds: number): void {
    this.milliseconds += seconds * 1_000;
  }
}

class UnusedCsmpSource implements CsmpTierSource {
  public async fetchCurrentTier(): Promise<CsmpTier> {
    return "NONE";
  }
}

const cleanMods: CreateSessionDto["installedMods"] = [
  {
    id: "hwanhee1.corum_ranked",
    version: "0.1.0",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
  {
    id: "syzzi.click_between_frames",
    version: "1.5.0",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
    settings: {
      "soft-toggle": false,
      "click-on-steps": false,
      "physics-bypass": false,
    },
  },
];

describe("Round 3 draw and repeating deathmatch", () => {
  it("repeats a tied three-attempt deathmatch on a different map and finalizes a winner", async () => {
    const baseDocument = configDocumentFixture("deathmatch-flow");
    const document = {
      ...baseDocument,
      operational: {
        ...baseDocument.operational,
        timeouts: {
          ...baseDocument.operational.timeouts!,
          reconnectGraceSeconds: 10_000,
        },
      },
    };
    const snapshot = {
      ...structuredClone(document),
      fetchedAt: "2026-08-20T03:00:00.000Z",
    };
    const config = { getSnapshot: () => structuredClone(snapshot) } as RankedConfigService;
    const clock = new MutableClock();
    const ids = new SequenceIds();
    const tokens = new TokenService(environmentFixture());
    const sessions = new SessionService(
      database,
      new UnusedCsmpSource(),
      clock,
      ids,
      config,
      tokens,
    );
    const service = new MatchService(
      database,
      clock,
      ids,
      new SeededRandom(5150),
      new MatchAccessService(tokens, clock),
      sessions,
      new OutboxService(ids),
      new InMemoryMatchRuntimeState(),
    );

    const playerAId = "00000000-0000-4000-8010-000000000001";
    const playerBId = "00000000-0000-4000-8010-000000000002";
    const matchId = "00000000-0000-4000-8010-000000000003";
    const configId = "00000000-0000-4000-8010-000000000004";
    const sessionAId = "00000000-0000-4000-8010-000000000005";
    const sessionBId = "00000000-0000-4000-8010-000000000006";
    const contextA: RankedSessionContext = {
      sessionId: sessionAId,
      playerId: playerAId,
      gdAccountId: "9201",
      gdUsername: "DeathA",
      displayedTier: "UNRANKED",
      hiddenMmr: 2500,
      placementGames: 0,
    };
    const contextB: RankedSessionContext = {
      sessionId: sessionBId,
      playerId: playerBId,
      gdAccountId: "9202",
      gdUsername: "DeathB",
      displayedTier: "UNRANKED",
      hiddenMmr: 2500,
      placementGames: 0,
    };
    const matchTokenA = tokens.deriveMatchToken(matchId, playerAId, sessionAId);
    const matchTokenB = tokens.deriveMatchToken(matchId, playerBId, sessionBId);
    const selectedMaps: RankedMapSnapshot[] = document.maps.slice(0, 3).map((map) => ({
      levelId: map.levelId,
      canonicalLevelId: map.canonicalLevelId,
      alternateLevelId: map.alternateLevelId,
      playableLevelId: map.playableLevelId,
      title: map.title,
      creator: map.creator,
      difficulty: map.difficulty,
      pool: map.pool,
      qualifyingPercent: map.qualifyingPercent,
    }));
    const now = clock.now().toISOString();

    for (const [id, accountId, username] of [
      [playerAId, "9201", "DeathA"],
      [playerBId, "9202", "DeathB"],
    ]) {
      await database.query(
        "INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, $2, $3)",
        [id, accountId, username],
      );
      await database.query(
        `INSERT INTO ranked_profiles (
           player_id, hidden_mmr, initial_csmp_tier, initial_seed_mmr, seed_applied_at
         ) VALUES ($1, 2500, 'BRONZE', 2500, $2)`,
        [id, now],
      );
    }
    await database.query(
      `INSERT INTO ranked_config_snapshots (
         id, generation, rules_version, source_payload, fetched_at
       ) VALUES ($1, $2, $3, $4::jsonb, $5)`,
      [configId, snapshot.generation, snapshot.operational.rules.rulesVersion, JSON.stringify(snapshot), now],
    );
    const readyDeadline = new Date(clock.now().getTime() + 30_000).toISOString();
    await database.query(
      `INSERT INTO ranked_matches (
         id, player_a_id, player_b_id, config_snapshot_id,
         mmr_a_before, mmr_b_before, effective_rating_average, effective_tier,
         candidate_maps_snapshot, selected_round_maps_snapshot, series_state,
         current_round_number, state, deadline_at, rules_version,
         last_heartbeat_a_at, last_heartbeat_b_at, started_at
       ) VALUES (
         $1, $2, $3, $4, 2500, 2500, 2500, 'BRONZE',
         $5::jsonb, $5::jsonb, $6::jsonb,
         1, 'ROUND_PREPARE', $7, $8, $9, $9, $9
       )`,
      [
        matchId,
        playerAId,
        playerBId,
        configId,
        JSON.stringify(selectedMaps),
        JSON.stringify(createMatchSeries()),
        readyDeadline,
        snapshot.operational.rules.rulesVersion,
        now,
      ],
    );
    await database.query(
      `INSERT INTO ranked_match_tokens (match_id, player_id, token_hash, expires_at)
       VALUES ($1, $2, $3, '2027-01-01T00:00:00Z'),
              ($1, $4, $5, '2027-01-01T00:00:00Z')`,
      [matchId, playerAId, tokens.hash(matchTokenA), playerBId, tokens.hash(matchTokenB)],
    );
    const firstMap = selectedMaps[0]!;
    await database.query(
      `INSERT INTO ranked_rounds (
         id, match_id, round_number, level_id, canonical_level_id,
         alternate_level_id, playable_level_id,
         title, creator, difficulty, pool, qualifying_percent,
         phase, ready_deadline_at
       ) VALUES ($1, $2, 1, $3, $4, $5, $6, $7, $8, $9, $10, $11, 'ROUND_PREPARE', $12)`,
      [
        ids.next(),
        matchId,
        firstMap.levelId,
        firstMap.canonicalLevelId,
        firstMap.alternateLevelId,
        firstMap.playableLevelId,
        firstMap.title,
        firstMap.creator,
        firstMap.difficulty,
        firstMap.pool,
        firstMap.qualifyingPercent,
        readyDeadline,
      ],
    );

    for (let round = 1; round <= 3; round += 1) {
      await service.ready(matchId, matchTokenA, contextA, {
        installedMods: structuredClone(cleanMods),
      });
      await service.ready(matchId, matchTokenB, contextB, {
        installedMods: structuredClone(cleanMods),
      });
      clock.advance(191);
      const resultState = (await service.state(matchId, matchTokenA, contextA)) as Record<string, any>;
      if (round < 3) {
        expect(resultState.state).toBe("ROUND_RESULT");
        expect(resultState.currentRound.outcome.result).toBe("DRAW");
        clock.advance(6);
        const next = (await service.state(matchId, matchTokenA, contextA)) as Record<string, any>;
        expect(next.state).toBe("ROUND_PREPARE");
        expect(next.currentRound.roundNumber).toBe(round + 1);
      } else {
        expect(resultState.state).toBe("DEATHMATCH_PREPARE");
      }
    }

    const playDeathmatch = async (scoreA: number, scoreB: number) => {
      await service.ready(matchId, matchTokenA, contextA, {
        installedMods: structuredClone(cleanMods),
      });
      await service.ready(matchId, matchTokenB, contextB, {
        installedMods: structuredClone(cleanMods),
      });
      const playing = (await service.state(matchId, matchTokenA, contextA)) as Record<string, any>;
      const levelId = playing.deathmatch.map.playableLevelId as string;
      const qualifying = Number(playing.deathmatch.map.qualifyingPercent);
      for (const [context, token, progress, prefix] of [
        [contextA, matchTokenA, scoreA, "a"],
        [contextB, matchTokenB, scoreB, "b"],
      ] as const) {
        for (let attempt = 1; attempt <= 3; attempt += 1) {
          clock.advance(1);
          const started = await service.startAttempt(matchId, token, context, {
            levelId,
            clientEventId: `${prefix}-${clock.now().getTime()}-${attempt}-start`,
          });
          expect(started.deathmatchSnapshot.attemptsUsed[context === contextA ? "A" : "B"]).toBe(attempt);
          if (attempt === 1) {
            const live = await service.updateAttemptProgress(matchId, token, context, {
              levelId,
              attemptId: started.attemptId!,
              progressPercent: progress,
            });
            expect(live.deathmatchSnapshot.displayScores[context === contextA ? "A" : "B"]).toBe(
              progress >= qualifying ? Math.floor(progress) : 0,
            );
          }
          clock.advance(1);
          const ended = await service.endAttempt(matchId, token, context, {
            levelId,
            attemptId: started.attemptId!,
            clientEventId: `${prefix}-${clock.now().getTime()}-${attempt}-end`,
            progressPercent: progress,
            cleared: false,
          });
          expect(ended.deathmatchSnapshot.attemptsCompleted[context === contextA ? "A" : "B"]).toBe(attempt);
        }
        if (context === contextA) {
          // Player A has spent exactly three attempts while B has not started yet,
          // so the deathmatch is still live and the authoritative 4th-attempt
          // rejection can be asserted directly.
          clock.advance(1);
          await expect(service.startAttempt(matchId, token, context, {
            levelId,
            clientEventId: `${prefix}-${clock.now().getTime()}-4-start`,
          })).resolves.toMatchObject({
            accepted: false,
            reason: "DEATHMATCH_ATTEMPTS_EXHAUSTED",
          });
        }
      }
      return service.state(matchId, matchTokenA, contextA) as Promise<Record<string, any>>;
    };

    let deathState = await playDeathmatch(70, 70);
    expect(deathState.state).toBe("DEATHMATCH_RESULT");
    const firstDeathMap = deathState.deathmatch.map.canonicalLevelId;
    clock.advance(6);
    deathState = (await service.state(matchId, matchTokenA, contextA)) as Record<string, any>;
    expect(deathState.state).toBe("DEATHMATCH_PREPARE");
    expect(deathState.deathmatch.sequence).toBe(2);
    expect(deathState.deathmatch.map.canonicalLevelId).not.toBe(firstDeathMap);

    deathState = await playDeathmatch(80, 70);
    expect(deathState.state).toBe("MATCH_RESULT");
    expect(deathState.result.winnerSide).toBe("A");
    const deathmatches = await database.query<{
      sequence: number;
      score_a: number;
      score_b: number;
      winner_id: string | null;
    }>("SELECT sequence, score_a, score_b, winner_id FROM ranked_deathmatches ORDER BY sequence");
    expect(deathmatches.rows).toHaveLength(2);
    expect(deathmatches.rows[0]!.winner_id).toBeNull();
    expect(deathmatches.rows[1]!.winner_id).toBe(playerAId);

    const relayEvents = await database.query<{ event_type: string; count: number }>(
      `SELECT event_type, COUNT(*)::int AS count
       FROM ranked_outbox_events
       GROUP BY event_type
       ORDER BY event_type`,
    );
    expect(relayEvents.rows).toEqual([
      { event_type: "DEATHMATCH_RESULT", count: 2 },
      { event_type: "DEATHMATCH_START", count: 2 },
      { event_type: "MATCH_RESULT", count: 1 },
      { event_type: "ROUND_RESULT", count: 3 },
      { event_type: "ROUND_START", count: 3 },
    ]);
  });
});
