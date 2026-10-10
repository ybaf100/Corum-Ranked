import "reflect-metadata";
import type { INestApplication } from "@nestjs/common";
import { Test } from "@nestjs/testing";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";
import type { RankedRatingProfile, TierBand } from "@corum-ranked/rules";
import { readStandings } from "../src/leaderboard/standing.js";
import { buildScorePresentation, resultPromotionSeries } from "../src/match/score-presentation.js";
import { PgliteDatabase } from "./pglite-database.js";
import { DATABASE } from "../src/database/database.port.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { StandingController } from "../src/leaderboard/standing.controller.js";
import { environmentFixture } from "./fixtures.js";

const id = (n: number) => `00000000-0000-4000-8000-${String(n).padStart(12, "0")}`;
let client: PGlite;
let database: PgliteDatabase;
let app: INestApplication;
let base: string;
const now = new Date("2026-10-09T10:00:00Z");
const tokens = new TokenService(environmentFixture());
beforeAll(async () => {
  client = new PGlite(); database = new PgliteDatabase(client);
  await client.exec(`CREATE TABLE ranked_players (id uuid PRIMARY KEY, gd_account_id bigint NOT NULL,
    gd_username text NOT NULL, is_bot boolean NOT NULL DEFAULT FALSE);
    CREATE TABLE ranked_profiles (player_id uuid PRIMARY KEY REFERENCES ranked_players(id),
      displayed_tier text NOT NULL, visible_ranked_score integer, hidden_mmr integer NOT NULL,
      placement_games integer NOT NULL DEFAULT 5);
    CREATE TABLE ranked_sessions (id uuid PRIMARY KEY, player_id uuid REFERENCES ranked_players(id),
      token_hash text NOT NULL, client_version text NOT NULL, revoked_at timestamptz,
      expires_at timestamptz NOT NULL, last_heartbeat_at timestamptz NOT NULL);`);
  const module = await Test.createTestingModule({ controllers: [StandingController], providers: [SessionGuard,
    { provide: DATABASE, useValue: database }, { provide: SERVER_CLOCK, useValue: { now: () => now } },
    { provide: TokenService, useValue: tokens }] }).compile();
  app = module.createNestApplication(); app.useLogger(false); await app.listen(0,"127.0.0.1"); base = await app.getUrl();
}, 60_000);
afterAll(async () => { await app?.close(); await client.close(); });
beforeEach(async () => { await client.exec("DELETE FROM ranked_sessions; DELETE FROM ranked_profiles; DELETE FROM ranked_players;"); });
async function add(n: number, score: number | null, mmr = 1_000, tier = "RED", bot = false) {
  await database.query("INSERT INTO ranked_players VALUES ($1,$2,$3,$4)", [id(n), bot ? -n : n, bot ? "BOT NORMAL" : `player-${n}`, bot]);
  await database.query("INSERT INTO ranked_profiles (player_id,displayed_tier,visible_ranked_score,hidden_mmr) VALUES ($1,$2,$3,$4)", [id(n), tier, score, mmr]);
}
const profile = (score: number, extra: Partial<RankedRatingProfile> = {}): RankedRatingProfile => ({
  hiddenMmr: 1_000, visibleRankedScore: score, displayedTier: "RED", placementGames: 5,
  placementResults: [], resultStreak: 0, promotion: null, demotionWarning: false, ...extra,
});
const bands: readonly TierBand[] = [
  { tier: "RED", minInclusive: 0, maxExclusive: 300, mainPool: 1, deathmatchPool: 1, promotionGames: 3, promotionWinsRequired: 1 },
  { tier: "AQUA", minInclusive: 300, maxExclusive: 600, mainPool: 2, deathmatchPool: 2, promotionGames: 3, promotionWinsRequired: 1 },
];

describe("client standings SQL", () => {
  it("protects the standings route with the existing version, revocation and presence session gates", async () => {
    await add(1, 1500, 5000, "GOLD"); await add(2, 1300, 3000, "GOLD"); await add(3, 1300, 3500, "GOLD", true);
    await database.query(`INSERT INTO ranked_sessions VALUES ($1,$2,$3,$4,NULL,$5,$6)`,
      [id(101),id(2),tokens.hash("standing-token"),REQUIRED_RANKED_CLIENT_VERSION,
        new Date(now.getTime() + 3600_000).toISOString(),now.toISOString()]);
    const get = (token?: string) => fetch(base + "/api/ranked/session/standing", {
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    });
    expect((await get()).status).toBe(401); expect((await get("invalid")).status).toBe(401);
    const valid = await get("standing-token"); expect(valid.status).toBe(200);
    expect(valid.headers.get("cache-control")).toBe("no-store");
    const body = await valid.json(); expect(body).toMatchObject({ serverNow: now.toISOString(),
      standing: { rank: 2, total: 3, topPercent: 2 / 3 * 100, target: { gdAccountId: "1", rank: 1 } } });
    expect(JSON.stringify(body)).not.toMatch(/hiddenMmr|hidden_mmr|sessionId|player_id|token/);
    await database.query("UPDATE ranked_sessions SET client_version='v0.5.0-beta.47'");
    const outdated = await get("standing-token"); expect(outdated.status).toBe(426);
    expect(await outdated.json()).toMatchObject({ requiredVersion: REQUIRED_RANKED_CLIENT_VERSION });
    await database.query("UPDATE ranked_sessions SET client_version=$1,last_heartbeat_at=$2",
      [REQUIRED_RANKED_CLIENT_VERSION,new Date(now.getTime() - 91_000).toISOString()]);
    expect((await get("standing-token")).status).toBe(401);
    await database.query("UPDATE ranked_sessions SET last_heartbeat_at=$1,revoked_at=$1",[now.toISOString()]);
    expect((await get("standing-token")).status).toBe(401);
    await database.query("UPDATE ranked_sessions SET revoked_at=NULL,expires_at=$1",[now.toISOString()]);
    expect((await get("standing-token")).status).toBe(401);
  });

  it("uses LP competition ranks for every tier, counts all historical bots and excludes unplaced/null LP", async () => {
    await add(1, 300, 50, "GOLD"); await add(2, 200, 9_000, "AQUA");
    await add(3, 200, 1_000, "GOLD"); await add(4, 200, 9_500, "SILVER", true);
    await add(5, 150, 800, "SILVER", true); await add(6, 100);
    await add(7, 9_999, 20_000, "UNRANKED"); await add(8, null, 20_000, "GOLD");
    const all = (await readStandings(database, [1,2,3,4,5,6,7,8].map(id))).get("current")!;
    expect([1,2,3,4,5,6,7,8].map((n) => all.get(id(n))!.rank)).toEqual([1,2,2,2,5,6,null,null]);
    expect(all.get(id(2))).toMatchObject({ total: 6, topPercent: 2 / 6 * 100 });
    expect(all.get(id(1))!.target).toBeNull();
    expect(all.get(id(6))!.target).toMatchObject({ rank: 5, visibleRankedScore: 150, gdAccountId: "-5", isBot: true });
    expect(all.get(id(7))).toMatchObject({ rank: null, topPercent: null, target: null });
    expect(JSON.stringify([...all.values()])).not.toMatch(/hiddenMmr|hidden_mmr|mmr|player_id/);
  });

  it("chooses nearest higher score, then maximum MMR, and persists one random tie choice across phases", async () => {
    await add(1, 30, 1); await add(2, 25, 2_000); await add(3, 25, 2_000);
    await add(4, 25, 1_999); await add(5, 20);
    const seen = new Set<string>();
    for (let seed = 0; seed < 24; ++seed) {
      const views = await readStandings(database, [id(5)], ["before","after"], [], String(seed));
      const before = views.get("before")!.get(id(5))!;
      expect(before.rank).toBe(5); expect(before.target!.rank).toBe(2);
      expect(["2", "3"]).toContain(before.target!.gdAccountId);
      expect(views.get("after")!.get(id(5))!.target).toEqual(before.target);
      seen.add(before.target!.gdAccountId);
    }
    expect(seen.size).toBe(2);
  });

  it("returns Top 2% for shared #2 out of 100 and full-bar eligibility for every joint #1", async () => {
    for (let n = 1; n <= 100; ++n) await add(n, n === 1 ? 30 : n <= 4 ? 25 : 20);
    const views = (await readStandings(database, [id(2),id(3),id(4)])).get("current")!;
    for (const n of [2,3,4]) expect(views.get(id(n))).toMatchObject({ rank: 2, total: 100, topPercent: 2 });
    await database.query("UPDATE ranked_profiles SET visible_ranked_score = 30 WHERE player_id = $1", [id(2)]);
    const leaders = (await readStandings(database, [id(1),id(2)])).get("current")!;
    for (const n of [1,2]) expect(leaders.get(id(n))).toMatchObject({ rank: 1, target: null });
  });

  it("projects both players' old/new profiles in one snapshot without changing database ratings", async () => {
    await add(1, 350, 1_200, "AQUA"); await add(2, 300, 900, "AQUA"); await add(3, 320);
    const subjects = { A: { playerId: id(1), before: profile(299), after: profile(350, { displayedTier: "AQUA" }) },
      B: { playerId: id(2), before: profile(300, { displayedTier: "AQUA" }), after: profile(250) } };
    const snapshot = await buildScorePresentation(database, bands, subjects, "A");
    expect(snapshot.players.A.before).toMatchObject({ rank: 3, total: 3 });
    expect(snapshot.players.A.after).toMatchObject({ rank: 1, target: null });
    expect(snapshot.players.B.before).toMatchObject({ rank: 2 });
    expect(snapshot.players.B.after).toMatchObject({ rank: 3, target: { visibleRankedScore: 320 } });
    expect(snapshot.tierBands).toEqual([{tier: "RED", minInclusive: 0}, {tier: "AQUA", minInclusive: 300}]);
    const stored = await database.query<{ visible_ranked_score: number }>("SELECT visible_ranked_score FROM ranked_profiles WHERE player_id = $1", [id(2)]);
    expect(stored.rows[0]!.visible_ranked_score).toBe(300);
    const serialized = JSON.stringify(snapshot);
    await database.query("UPDATE ranked_profiles SET visible_ranked_score = 99 WHERE player_id = $1", [id(3)]);
    expect(JSON.stringify(snapshot)).toBe(serialized);
  });

  it("handles an empty ranked population", async () => {
    await add(1, null, 1_000, "UNRANKED");
    expect((await readStandings(database, [id(1)])).get("current")!.get(id(1)))
      .toEqual({ rank: null, total: 0, topPercent: null, target: null });
  });

  it("captures compact GOLD score groups and the moving opponent in the same immutable result", async () => {
    await add(1, 1400, 1800, "GOLD"); await add(2, 1380, 1700, "GOLD");
    await add(3, 1700, 1700, "GOLD"); await add(4, 1500, 1500, "GOLD");
    await add(5, 1390, 1390, "GOLD"); await add(6, 1350, 1200, "GOLD");
    await add(7, 1350, 1600, "GOLD", true); await add(8, 1320, 1320, "GOLD");
    await add(9, 1250, 1250, "GOLD"); await add(10, 150, 150);
    await add(11, 2000, 2000, "UNRANKED");
    const subjects = {
      A: { playerId: id(1), before: profile(1300,{displayedTier:"GOLD"}), after: profile(1400,{displayedTier:"GOLD"}) },
      B: { playerId: id(2), before: profile(1420,{displayedTier:"GOLD"}), after: profile(1380,{displayedTier:"GOLD"}) },
    };
    let reads = 0;
    const snapshot = await buildScorePresentation({query: async (...args) => {++reads; return database.query(...args);}}, bands, subjects, "A");
    expect(reads).toBe(1);
    expect(snapshot.players.A.before).toMatchObject({rank:8,total:10});
    expect(snapshot.players.A.after).toMatchObject({rank:3,total:10,target:{gdAccountId:"4",visibleRankedScore:1500}});
    const timeline = snapshot.players.A.goldTimeline!;
    expect(timeline).toMatchObject({fixedTotal:8,higherFixed:1,
      moving:{beforeScore:1420,afterScore:1380,target:{gdAccountId:"2"}}});
    expect(timeline.groups.map((g) => [g.score,g.count])).toEqual([[1500,1],[1390,1],[1350,2],[1320,1]]);
    expect(timeline.groups[2]!.target).toMatchObject({gdAccountId:"-7",isBot:true});
    expect(timeline.overtakes?.map(t=>t.gdAccountId)).toEqual(["8","6","-7","5","2"]);
    expect(snapshot.players.B.goldTimeline).not.toBeNull();
    expect(snapshot.players.A.before).not.toHaveProperty("goldTimeline");
    expect(snapshot.players.A.after).not.toHaveProperty("goldTimeline");
    const serialized = JSON.stringify(snapshot);
    expect(serialized).not.toMatch(/hiddenMmr|hidden_mmr|player_id|preferMovingMmr/);
    await database.query("UPDATE ranked_profiles SET visible_ranked_score=9000");
    expect(JSON.stringify(snapshot)).toBe(serialized);
  });

  it("keeps a stable maximum-MMR icon per shared LP and handles unranked moving participants", async () => {
    await add(1, 1320, 1000, "GOLD"); await add(2, null, 1000, "UNRANKED");
    await add(3, 1300, 900, "GOLD"); await add(4, 1300, 1100, "GOLD");
    await add(5, 1400, 1000, "GOLD");
    const snapshot = await buildScorePresentation(database, bands, {
      A:{playerId:id(1),before:profile(1250,{displayedTier:"GOLD"}),after:profile(1320,{displayedTier:"GOLD"})},
      B:{playerId:id(2),before:profile(0,{displayedTier:"UNRANKED"}),after:profile(0,{displayedTier:"UNRANKED"})},
    },"A");
    expect(snapshot.players.A.goldTimeline).toMatchObject({fixedTotal:3,higherFixed:0,moving:null});
    expect(snapshot.players.A.goldTimeline!.groups.map((g) => g.target.gdAccountId)).toEqual(["5","4"]);
    expect(snapshot.players.B.goldTimeline).toBeNull();
    expect(snapshot.players.A.after).toMatchObject({rank:2,total:4,target:{gdAccountId:"5"}});
  });

  it("selects only the highest-ranked five passed players, including separate tied bots, and excludes final LP ties", async () => {
    await add(1,1490,1000,"GOLD"); await add(2,1600,1000,"GOLD");
    for (let n=3;n<=10;++n) await add(n,1500-n*10,1000+n,"GOLD",n===4||n===5);
    await add(11,1460,3000,"GOLD",true);
    await add(12,1490,2000,"GOLD"); // final tie: joint rank, not passed
    await add(13,1450,4000,"UNRANKED");
    const result=await buildScorePresentation(database,bands,{
      A:{playerId:id(1),before:profile(1200,{displayedTier:"GOLD"}),after:profile(1490,{displayedTier:"GOLD"})},
      B:{playerId:id(2),before:profile(1600,{displayedTier:"GOLD"}),after:profile(1600,{displayedTier:"GOLD"})},
    },"A");
    const selected=result.players.A.goldTimeline!.overtakes!;
    expect(selected.map(t=>t.gdAccountId)).toEqual(["6","-5","-4","-11","3"]);
    expect(selected.map(t=>t.visibleRankedScore)).toEqual([1440,1450,1460,1460,1470]);
    expect(selected.filter(t=>t.isBot)).toHaveLength(3);
    expect(selected).toHaveLength(5);
    expect(result.players.B.goldTimeline!.overtakes).toEqual([]);
    expect(JSON.stringify(selected)).not.toMatch(/mmr|player_id/);
  });
});

it("retains the last WIN/LOSS after a series succeeds/fails and does not duplicate ongoing marks", () => {
  const promotion = { sourceTier: "RED" as const, targetTier: "AQUA" as const, cutoff: 300,
    gamesRequired: 3, winsRequired: 1, results: ["LOSS", "LOSS"] as const };
  const before = profile(299, { promotion });
  expect(resultPromotionSeries(before, profile(350, { displayedTier: "AQUA" }), "A", "A")!.results).toEqual(["LOSS","LOSS","WIN"]);
  expect(resultPromotionSeries(before, profile(250), "A", "B")!.results).toEqual(["LOSS","LOSS","LOSS"]);
  const ongoing = profile(299, { promotion: { ...promotion, results: ["WIN"] } });
  expect(resultPromotionSeries(profile(299, { promotion: { ...promotion, results: [] } }), ongoing, "A", "A")!.results).toEqual(["WIN"]);
  expect(before.promotion!.results).toEqual(["LOSS","LOSS"]);
});
