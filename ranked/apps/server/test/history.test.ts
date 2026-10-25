import "reflect-metadata";
import { ValidationPipe, type INestApplication } from "@nestjs/common";
import { Test } from "@nestjs/testing";
import { afterAll, beforeAll, describe, expect, it, vi } from "vitest";
import { TokenService } from "../src/common/token.service.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import { DATABASE, type DatabasePort } from "../src/database/database.port.js";
import { HistoryController } from "../src/history/history.controller.js";
import { HistoryService } from "../src/history/history.service.js";
import type { MatchService } from "../src/match/match.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import type { SurvivalService } from "../src/survival/survival.service.js";
import { environmentFixture } from "./fixtures.js";

const clock = { now: () => new Date("2026-10-11T12:00:00.000Z") };
const session: RankedSessionContext = {
  sessionId: "session", playerId: "player", gdAccountId: "100", gdUsername: "Player",
  displayedTier: "SILVER", hiddenMmr: 1000, placementGames: 5,
  clientVersion: REQUIRED_RANKED_CLIENT_VERSION, clientBuildChannel: "release",
};
const soloMatch = { matchId: "solo", finishedAt: "2026-10-10T12:00:00.000Z",
  opponentName: "Opponent", side: "A", winnerSide: "A", rounds: [{ highestPercentA: 100 }],
  deathmatches: [], rankedScoreDelta: { A: 10, B: -10 }, profileAfter: { A: { visibleRankedScore: 950 } } };

function fixture(soloMatches: unknown[] = [soloMatch], survivalMatches: unknown[] = []) {
  const solo = { history: vi.fn(async () => ({ matches: soloMatches, serverNow: clock.now().toISOString() })) };
  const survival = { history: vi.fn(async () => ({ matches: survivalMatches, lpEvents: [{ actual_delta: 50 }] })) };
  const service = new HistoryService(solo as unknown as MatchService, survival as unknown as SurvivalService, clock);
  return { solo, survival, service };
}

describe("unified history", () => {
  it("keeps Solo fields and filters Survival without reading Solo history", async () => {
    const f = fixture();
    expect(await f.service.history(session, "SURVIVAL")).toEqual({ currentPlayers:{},scope: "SURVIVAL", matches: [], serverNow: clock.now().toISOString() });
    expect(f.solo.history).not.toHaveBeenCalled();
    expect(f.survival.history).toHaveBeenCalledWith(session.playerId,{});
  });
  it("keeps the original Solo details without reading Survival history", async () => {
    const f = fixture();
    expect((await f.service.history(session, "SOLO_RANKED")).matches).toEqual([{ ...soloMatch, mode: "SOLO_RANKED" }]);
    expect(f.solo.history).toHaveBeenCalledWith(session,{});
    expect(f.survival.history).not.toHaveBeenCalled();
  });
  it("merges actual matches in reverse chronological order and never exposes LP audit as matches", async () => {
    const f = fixture([soloMatch], [{ matchId: "sur", finishedAt: "2026-10-11T11:00:00.000Z" }]);
    const result = await f.service.history(session, "ALL");
    expect(result.matches).toEqual([
      { matchId: "sur", finishedAt: "2026-10-11T11:00:00.000Z", mode: "SURVIVAL" },
      { ...soloMatch, mode: "SOLO_RANKED" },
    ]);
    expect(result).not.toHaveProperty("lpEvents");
  });
  it("limits combined history to 30 with a stable order for equal timestamps", async () => {
    const rows = Array.from({ length: 35 }, (_, n) => ({ ...soloMatch, matchId: String(35-n).padStart(2, "0") }));
    const result = await fixture(rows).service.history(session, "ALL");
    expect(result.matches.map((match) => match.matchId)).toEqual(Array.from({ length: 30 }, (_, n) => String(n+1).padStart(2, "0")));
  });
  it("returns an honest empty result for M1 Survival and fully empty histories", async () => {
    expect((await fixture([], []).service.history(session, "ALL")).matches).toEqual([]);
    expect((await fixture().service.history(session, "ALL")).matches).toEqual([{ ...soloMatch, mode: "SOLO_RANKED" }]);
  });
});

describe("unified history HTTP session and query validation", () => {
  let app: INestApplication, base: string;
  const token = "history-session-fixture";
  const tokens = new TokenService(environmentFixture());
  const f = fixture();
  let sessionValid = true;
  const database: DatabasePort = {
    query: async <Row>(sql: string, values?: readonly unknown[]) => {
      expect(sql).toContain("FROM ranked_sessions");
      expect(values?.[0]).toBe(tokens.hash(token));
      const rows = sessionValid ? [{ session_id: session.sessionId, player_id: session.playerId,
        gd_account_id: session.gdAccountId, gd_username: session.gdUsername,
        displayed_tier: session.displayedTier, hidden_mmr: session.hiddenMmr,
        placement_games: session.placementGames, client_version: session.clientVersion, build_channel: "release" }] : [];
      return { rows: rows as Row[], rowCount: rows.length };
    },
    transaction: async () => { throw new Error("History must never mutate player data"); },
    ping: async () => true,
  };
  beforeAll(async () => {
    const module = await Test.createTestingModule({ controllers: [HistoryController], providers: [SessionGuard,
      { provide: DATABASE, useValue: database }, { provide: TokenService, useValue: tokens },
      { provide: SERVER_CLOCK, useValue: clock }, { provide: HistoryService, useValue: f.service }] }).compile();
    app = module.createNestApplication(); app.useLogger(false);
    app.useGlobalPipes(new ValidationPipe({ transform: true, whitelist: true, forbidNonWhitelisted: true }));
    await app.listen(0, "127.0.0.1"); base = await app.getUrl();
  });
  afterAll(async () => { await app?.close(); });
  const get = (query = "", authenticated = true) => fetch(base + "/api/ranked/history" + query,
    { headers: authenticated ? { Authorization: `Bearer ${token}` } : {} });

  it("requires a current authenticated session", async () => {
    expect((await get("", false)).status).toBe(401);
    sessionValid = false;
    try { expect((await get()).status).toBe(401); } finally { sessionValid = true; }
  });
  it("defaults to ALL, tags Solo records and disables response caching", async () => {
    const result = await get(); expect(result.status).toBe(200);
    expect(result.headers.get("cache-control")).toBe("no-store");
    expect(await result.json()).toMatchObject({ scope: "ALL", matches: [{ matchId: "solo", mode: "SOLO_RANKED", opponentName: "Opponent" }] });
  });
  it("validates all three tabs and rejects arbitrary scopes or player identity overrides", async () => {
    for (const scope of ["ALL", "SOLO_RANKED", "SURVIVAL"]) {
      const result = await get(`?scope=${scope}`); expect(result.status).toBe(200);
      expect((await result.json()).scope).toBe(scope);
    }
    expect((await get("?scope=FAKE")).status).toBe(400);
    expect((await get("?playerId=someone-else")).status).toBe(400);
    expect(f.survival.history).toHaveBeenLastCalledWith(session.playerId,{order:undefined,result:undefined,summary:false});
  });
});
