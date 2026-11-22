import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { SeededRandom, canonicalActiveMaps, type CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import type { ServerEnvironment } from "../src/config/server-environment.js";
import { DebugChallengeService } from "../src/debug-bot/debug-challenge.service.js";
import { MatchAccessService } from "../src/match/match-access.service.js";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { QueueService } from "../src/queue/queue.service.js";
import { OutboxService } from "../src/relay/outbox.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";
import { AchievementService } from "../src/achievement/achievement.service.js";

let pglite: PGlite;
let database: PgliteDatabase;

class SequenceIds implements IdGenerator {
  private value = 8_000;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8030-${String(this.value).padStart(12, "0")}`;
  }
}

class MutableClock implements ServerClock {
  private milliseconds = Date.parse("2026-08-20T04:00:00.000Z");
  public now(): Date {
    return new Date(this.milliseconds);
  }
  public advance(milliseconds: number): void {
    this.milliseconds += milliseconds;
  }
}

class FixedCsmpSource implements CsmpTierSource {
  public async fetchCurrentTier(): Promise<CsmpTier> {
    return "BRONZE";
  }
}

const cleanMods: CreateSessionDto["installedMods"] = [
  {
    id: "hwanhee1.corum_ranked",
    version: "v0.5.0-beta.14",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
  {
    id: "syzzi.click_between_frames",
    version: "v1.5.0",
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
  {
    id: "geode.node-ids",
    version: "v1.23.3",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
];

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const migrationName of (await readdir(new URL("../../../migrations/", import.meta.url))).filter(n => n.endsWith(".sql")).sort()) {
    const path = fileURLToPath(new URL(`../../../migrations/${migrationName}`, import.meta.url));
    await pglite.exec(await readFile(path, "utf8"));
  }
  await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01',overhaul_activated_at='2000-01-01'");
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

describe("debug lobby challenge: designated maps, scoring PvP", () => {
  const ids = new SequenceIds();
  const clock = new MutableClock();
  const random = new SeededRandom(20261122);
  const document = configDocumentFixture("debug-challenge");
  const snapshot = { ...document, fetchedAt: "2026-08-20T04:00:00.000Z" };
  const config = { withAdmission: async <T>(operation: () => Promise<T>): Promise<T> => operation(),
      getSnapshot: () => structuredClone(snapshot) } as RankedConfigService;
  const environment: ServerEnvironment = {
    ...environmentFixture(),
    debugBot: {
      password: "2008", tickMs: 125, attemptDelayMs: 200,
      difficulties: {
        EASY: { mmrOffset: -250, qualifyingChance: 0.3, clearChance: 0, paceMultiplier: 0.96 },
        NORMAL: { mmrOffset: 0, qualifyingChance: 0.6, clearChance: 0.1, paceMultiplier: 1.00 },
        HARD: { mmrOffset: 250, qualifyingChance: 0.8, clearChance: 0.2, paceMultiplier: 1.05 },
      },
    },
  };
  let tokens: TokenService;
  let sessions: SessionService;
  let queue: QueueService;
  let matches: MatchService;
  let challenges: DebugChallengeService;
  const maps = canonicalActiveMaps(snapshot.maps);

  beforeAll(() => {
    tokens = new TokenService(environment);
    sessions = new SessionService(database, new FixedCsmpSource(), clock, ids, config, tokens);
    matches = new MatchService(database, clock, ids, random, new MatchAccessService(tokens, clock), sessions,
      new OutboxService(ids), new AchievementService(database, clock, ids), new InMemoryMatchRuntimeState());
    queue = new QueueService(database, clock, ids, random, config, sessions, tokens, matches);
    challenges = new DebugChallengeService(environment, database, clock, ids, config, sessions, queue);
  });

  const contextFor = async (accountId: string, channel: "debug" | "release" = "debug"): Promise<RankedSessionContext> => {
    const created = await sessions.create({
      gdAccountId: accountId, gdUsername: `Debug${accountId}`, clientVersion: "v0.5.0-beta.14",
      clientInstanceId: `instance-${accountId}`, installedMods: structuredClone(cleanMods), clientBuildChannel: channel,
    });
    await database.query(
      `UPDATE ranked_profiles SET placement_games = 5, displayed_tier = 'BRONZE', hidden_mmr = 2500, visible_ranked_score = 2500
       WHERE player_id = (SELECT player_id FROM ranked_sessions WHERE token_hash = $1) AND placement_games = 0`,
      [tokens.hash(created.sessionToken)],
    );
    const row = (await database.query<{ session_id: string; player_id: string; gd_username: string;
      displayed_tier: RankedSessionContext["displayedTier"]; hidden_mmr: number; placement_games: number }>(
      `SELECT s.id AS session_id, p.id AS player_id, p.gd_username, r.displayed_tier, r.hidden_mmr, r.placement_games
       FROM ranked_sessions s JOIN ranked_players p ON p.id = s.player_id JOIN ranked_profiles r ON r.player_id = p.id
       WHERE s.token_hash = $1`, [tokens.hash(created.sessionToken)])).rows[0]!;
    return { sessionId: row.session_id, playerId: row.player_id, gdAccountId: accountId, gdUsername: row.gd_username,
      displayedTier: row.displayed_tier, hiddenMmr: Number(row.hidden_mmr), placementGames: Number(row.placement_games),
      clientBuildChannel: channel };
  };
  const send = (from: RankedSessionContext, to: RankedSessionContext, rounds: string[], deathmatch = "", password = "2008") =>
    challenges.send(from, { password, targetPlayerId: to.playerId, fixedMaps: { rounds: rounds.map((id) => id || null), deathmatch: deathmatch || null },
      installedMods: structuredClone(cleanMods) });

  it("lists idle debug players, then plays the accepted challenge's maps in order with no ban phase", async () => {
    const a = await contextFor("9501"), b = await contextFor("9502"), release = await contextFor("9503", "release");
    // Only debug clients idle in the lobby are offered, never the caller.
    const lobby = (await challenges.lobby(a)).players.map((p) => p.playerId);
    expect(lobby).toContain(b.playerId);
    expect(lobby).not.toContain(a.playerId);
    expect(lobby).not.toContain(release.playerId);
    await expect(challenges.lobby(release)).rejects.toThrow(/debug client/);
    await expect(send(a, b, [maps[0]!.canonicalLevelId], "", "wrong")).rejects.toThrow(/Incorrect password/);
    await expect(send(a, release, [maps[0]!.canonicalLevelId])).rejects.toThrow(/not idle in the lobby/);
    await expect(send(a, b, ["no-such-map"])).rejects.toThrow(/not in the active map pool/);

    const chosen = [maps[2]!, maps[0]!];
    const sent = await send(a, b, [chosen[0]!.canonicalLevelId, chosen[1]!.canonicalLevelId, ""], maps[1]!.canonicalLevelId);
    expect(sent.challenge).toMatchObject({ status: "PENDING", from: a.gdUsername, scoring: true });
    expect(sent.challenge.maps.map((m) => m.slot)).toEqual(["R1", "R2", "R3", "DM"]);
    expect(sent.challenge.maps[2]!.title).toBeNull(); // random slot
    await expect(send(a, b, [maps[0]!.canonicalLevelId])).rejects.toThrow(/pending challenge/);
    const incoming = challenges.state(b).incoming;
    expect(incoming).toHaveLength(1);
    expect(incoming[0]).not.toHaveProperty("matchToken");

    const accepted = await challenges.accept(b, incoming[0]!.id, { installedMods: structuredClone(cleanMods) });
    expect(accepted.side).toBe("B");
    const outgoing = challenges.state(a).outgoing!;
    expect(outgoing).toMatchObject({ status: "ACCEPTED", matchId: accepted.matchId, side: "A" });
    expect(challenges.state(b).incoming).toHaveLength(0);
    await expect(challenges.accept(b, incoming[0]!.id, { installedMods: structuredClone(cleanMods) })).rejects.toThrow(/accepted/);

    const row = (await database.query<{ match_type: string; fixed_maps: any; series_state: any }>(
      "SELECT match_type, fixed_maps, series_state FROM ranked_matches WHERE id = $1", [accepted.matchId])).rows[0]!;
    expect(row.match_type).toBe("RANKED_PVP"); // scores like any Ranked match
    const fixed = typeof row.fixed_maps === "string" ? JSON.parse(row.fixed_maps) : row.fixed_maps;
    expect(fixed.rounds.slice(0, 2).map((m: any) => m.canonicalLevelId)).toEqual(chosen.map((m) => m.canonicalLevelId));
    expect(fixed.deathmatch.canonicalLevelId).toBe(maps[1]!.canonicalLevelId);

    await matches.ready(accepted.matchId, outgoing.matchToken!, a, { installedMods: structuredClone(cleanMods) });
    await matches.ready(accepted.matchId, accepted.matchToken, b, { installedMods: structuredClone(cleanMods) });
    const state = await matches.state(accepted.matchId, accepted.matchToken, b) as Record<string, any>;
    // No ban phase: Round 1 is the first chosen map.
    expect(state.state).toBe("ROUND_PREPARE");
    expect(state.currentRound.map.canonicalLevelId).toBe(chosen[0]!.canonicalLevelId);
    const selected = (await database.query<{ selected_round_maps_snapshot: any }>(
      "SELECT selected_round_maps_snapshot FROM ranked_matches WHERE id = $1", [accepted.matchId])).rows[0]!.selected_round_maps_snapshot;
    const rounds = (typeof selected === "string" ? JSON.parse(selected) : selected).map((m: any) => m.canonicalLevelId);
    expect(rounds.slice(0, 2)).toEqual(chosen.map((m) => m.canonicalLevelId));
    expect(new Set(rounds).size).toBe(rounds.length);
  });

  it("declines, cancels and expires pending challenges", async () => {
    const a = await contextFor("9511"), b = await contextFor("9512");
    const first = await send(a, b, [maps[0]!.canonicalLevelId]);
    expect(challenges.decline(b, first.challenge.id).challenge.status).toBe("DECLINED");
    expect(challenges.state(a).outgoing!.status).toBe("DECLINED");
    const second = await send(a, b, [maps[0]!.canonicalLevelId]);
    expect(() => challenges.cancel(b, second.challenge.id)).toThrow(/Not your challenge/);
    expect(challenges.cancel(a, second.challenge.id).challenge.status).toBe("CANCELLED");
    const third = await send(a, b, [maps[0]!.canonicalLevelId]);
    clock.advance(61_000);
    expect(challenges.state(b).incoming).toHaveLength(0);
    expect(challenges.state(a).outgoing!.status).toBe("EXPIRED");
    await expect(challenges.accept(b, third.challenge.id, { installedMods: structuredClone(cleanMods) })).rejects.toThrow(/expired/);
  });
});
