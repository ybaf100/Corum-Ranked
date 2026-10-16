import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import type { ForumRecordsService } from "../src/forum/forum-records.service.js";
import type { CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { SessionService } from "../src/session/session.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const migrationName of (await readdir(new URL("../../../migrations/", import.meta.url))).filter(n => n.endsWith(".sql")).sort()) {
    const migrationPath = fileURLToPath(
      new URL(`../../../migrations/${migrationName}`, import.meta.url),
    );
    await pglite.exec(await readFile(migrationPath, "utf8"));
  }
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

beforeEach(async () => {
  await pglite.exec(
    "TRUNCATE TABLE ranked_sessions, ranked_profiles, ranked_players CASCADE",
  );
});

class SequenceIds implements IdGenerator {
  private value = 10;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8000-${String(this.value).padStart(12, "0")}`;
  }
}

class FixedClock implements ServerClock {
  public milliseconds = Date.parse("2026-08-20T00:00:00.000Z");
  public now(): Date {
    return new Date(this.milliseconds);
  }
  public advance(milliseconds: number) { this.milliseconds += milliseconds; }
}

class FakeCsmpSource implements CsmpTierSource {
  public tier: CsmpTier = "BRONZE";
  public calls = 0;
  public async fetchCurrentTier(): Promise<CsmpTier> {
    this.calls += 1;
    return this.tier;
  }
}

const cleanDto = (accountId: string): CreateSessionDto => ({
  gdAccountId: accountId,
  gdUsername: `Player${accountId}`,
  clientVersion: "v0.1.0",
  clientInstanceId: "test-process-instance",
  installedMods: [
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
    {
      id: "geode.node-ids",
      version: "v1.23.3",
      enabled: true,
      loaded: true,
      internal: false,
      system: false,
    },
  ],
});

const buildService = (source: FakeCsmpSource, clock = new FixedClock(), records?: ForumRecordsService, document = configDocumentFixture()) => {
  const config = {
    getSnapshot: () => ({ ...structuredClone(document), fetchedAt: "2026-08-20T00:00:00Z" }),
  } as RankedConfigService;
  return new SessionService(
    database,
    source,
    clock,
    new SequenceIds(),
    config,
    new TokenService(environmentFixture()),
    records,
  );
};

describe("Ranked session/profile creation", () => {
  it("returns the saved Ranked win/loss record without seeding or resetting it on login", async () => {
    const service = buildService(new FakeCsmpSource());
    const first = await service.create(cleanDto("7001"));
    expect(first.player).toMatchObject({wins:0,losses:0});
    await database.query("UPDATE ranked_profiles SET wins=8,losses=2 WHERE player_id=$1",[(await contextFor(first.sessionToken)).playerId]);
    const next = await service.create(cleanDto("7001"));
    expect(next.player).toMatchObject({wins:8,losses:2});
  });
  it("queues the first forum fetch only after the profile/session transaction commits", async () => {
    let inTransaction = false;
    const original = database.transaction.bind(database);
    const transaction = vi.spyOn(database, 'transaction').mockImplementation(async (operation) => {
      inTransaction = true;
      try { return await original(operation); }
      finally { inTransaction = false; }
    });
    const enqueue = vi.fn(() => { expect(inTransaction).toBe(false); });
    try {
      const dto = cleanDto('7044');
      await buildService(new FakeCsmpSource(), new FixedClock(), { enqueue } as unknown as ForumRecordsService).create(dto);
      expect(enqueue).toHaveBeenCalledWith(dto.gdAccountId, dto.gdUsername);
    } finally { transaction.mockRestore(); }
  });
  const contextFor = async (token: string): Promise<RankedSessionContext> => {
    const result = await database.query<{ id: string; player_id: string }>(
      "SELECT id, player_id FROM ranked_sessions WHERE token_hash = $1",
      [new TokenService(environmentFixture()).hash(token)],
    );
    return { sessionId: result.rows[0]!.id, playerId: result.rows[0]!.player_id,
      gdAccountId: "7001", gdUsername: "Player7001", displayedTier: "UNRANKED", hiddenMmr: 2500, placementGames: 0 };
  };

  it("blocks a second device at lobby admission and permits it immediately after Close", async () => {
    const clock = new FixedClock(), service = buildService(new FakeCsmpSource(), clock);
    const first = await service.create(cleanDto("7001"));
    const other = { ...cleanDto("7001"), clientInstanceId: "second-device-instance" };
    await expect(service.create(other)).rejects.toMatchObject({ response: { code: "RANKED_ACCOUNT_ACTIVE" } });
    await service.close(await contextFor(first.sessionToken));
    const second = await service.create(other);
    expect(second.sessionToken).not.toBe(first.sessionToken);
    await expect(service.heartbeat(await contextFor(first.sessionToken))).rejects.toMatchObject({ status: 401 });
    expect((await service.heartbeat(await contextFor(second.sessionToken))).sessionLeaseSeconds).toBe(90);
  });

  it("explicit takeover revokes the old lobby session; a late Close cannot release the new admission", async () => {
    const service=buildService(new FakeCsmpSource());
    const first=await service.create(cleanDto("7001")),old=await contextFor(first.sessionToken);
    const other={...cleanDto("7001"),clientInstanceId:"second-device-instance",takeOver:true};
    const second=await service.create(other),fresh=await contextFor(second.sessionToken);
    expect(second.activeMatch).toBe(false);
    await expect(service.heartbeat(old)).rejects.toMatchObject({status:401});
    await service.close(old);await expect(service.heartbeat(fresh)).resolves.toBeDefined();
    await expect(service.create({...cleanDto("7001"),clientInstanceId:"third-device-instance"})).rejects.toMatchObject({status:409});
    expect((await database.query("SELECT id FROM ranked_sessions WHERE revoked_at IS NULL")).rows).toHaveLength(1);
  });

  it("takeover and same-process retries recover the assignment and rotate match tokens without modifying match state", async () => {
    const clock=new FixedClock(),service=buildService(new FakeCsmpSource(),clock),tokens=new TokenService(environmentFixture());
    const first=await service.create(cleanDto("7001")),old=await contextFor(first.sessionToken);
    const opponent=await service.create(cleanDto("7002")),other=await contextFor(opponent.sessionToken);
    const matchId="00000000-0000-4000-8000-000000000090",snapshotId="00000000-0000-4000-8000-000000000091";
    await database.query("INSERT INTO ranked_config_snapshots (id,generation,rules_version,source_payload,fetched_at) VALUES ($1,'takeover','test','{}',$2)",[snapshotId,clock.now()]);
    await database.query(`INSERT INTO ranked_matches (id,player_a_id,player_b_id,config_snapshot_id,mmr_a_before,mmr_b_before,
      effective_rating_average,effective_tier,candidate_maps_snapshot,series_state,state,state_version,rules_version)
      VALUES ($1,$2,$3,$4,2500,2500,2500,'BRONZE','[]','{}','ROUND_PLAYING',7,'test')`,[matchId,old.playerId,other.playerId,snapshotId]);
    const oldToken=tokens.deriveMatchToken(matchId,old.playerId,old.sessionId);
    await database.query("INSERT INTO ranked_match_tokens (match_id,player_id,token_hash,expires_at) VALUES ($1,$2,$3,$4)",
      [matchId,old.playerId,tokens.hash(oldToken),new Date(clock.now().getTime()+3600000)]);
    await database.query(`INSERT INTO ranked_queue_entries (player_id,session_id,hidden_mmr_snapshot,joined_at,last_heartbeat_at,status,matched_match_id)
      VALUES ($1,$2,2500,$3,$3,'MATCHED',$4)`,[old.playerId,old.sessionId,clock.now(),matchId]);
    const {QueueService}=await import("../src/queue/queue.service.js");
    const queue=Object.assign(Object.create(QueueService.prototype),{database,clock,tokens}) as InstanceType<typeof QueueService>;
    const {MatchAccessService}=await import("../src/match/match-access.service.js");const access=new MatchAccessService(tokens,clock);
    const dto={...cleanDto("7001"),clientInstanceId:"second-device-instance",takeOver:true};
    const takeover=await service.create(dto),replacement=await contextFor(takeover.sessionToken);
    expect(takeover.activeMatch).toBe(true);
    await service.close(old);
    await expect(queue["lockAdmission"](database,old)).rejects.toMatchObject({status:401});
    await expect(queue["lockAdmission"](database,replacement)).resolves.toBeUndefined();
    const recovered=await queue.status(replacement);expect(recovered).toMatchObject({status:"MATCHED",matchId,matchState:"ROUND_PLAYING",stateVersion:7});
    expect((recovered as {matchToken:string}).matchToken).not.toBe(oldToken);
    expect((await queue.status(old)).status).toBe("NOT_QUEUED");
    await database.query("UPDATE ranked_queue_entries SET status='QUEUED' WHERE player_id=$1",[old.playerId]);
    const before=(await database.query("SELECT last_heartbeat_at FROM ranked_queue_entries WHERE player_id=$1",[old.playerId])).rows[0];
    clock.advance(1000);await queue.heartbeat(old);expect((await queue.leave(old)).left).toBe(false);
    expect((await database.query("SELECT last_heartbeat_at FROM ranked_queue_entries WHERE player_id=$1",[old.playerId])).rows[0]).toEqual(before);
    await database.query("UPDATE ranked_queue_entries SET status='MATCHED' WHERE player_id=$1",[old.playerId]);
    await expect(access.authorize(database,matchId,old,oldToken)).rejects.toMatchObject({status:401});
    await expect(access.authorize(database,matchId,replacement,(recovered as {matchToken:string}).matchToken)).resolves.toMatchObject({side:"A"});
    const retry=await service.create({...dto,takeOver:false}),latest=await contextFor(retry.sessionToken);
    expect(retry.activeMatch).toBe(true);await service.close(replacement);
    expect(await queue.status(latest)).toMatchObject({status:"MATCHED",stateVersion:7});
    await expect(service.heartbeat(replacement)).rejects.toMatchObject({status:401});
    await expect(service.heartbeat(latest)).resolves.toBeDefined();
    expect((await database.query("SELECT state,state_version FROM ranked_matches WHERE id=$1",[matchId])).rows[0]).toMatchObject({state:"ROUND_PLAYING",state_version:7});
  });

  it("concurrent forced admissions leave exactly one live token", async () => {
    const service=buildService(new FakeCsmpSource());await service.create(cleanDto("7001"));
    const results=await Promise.all(["second-device-instance","third-device-instance"].map(clientInstanceId=>service.create({...cleanDto("7001"),clientInstanceId,takeOver:true})));
    const beats=await Promise.allSettled(results.map(async result=>service.heartbeat(await contextFor(result.sessionToken))));
    expect(beats.filter(result=>result.status==="fulfilled")).toHaveLength(1);
    expect((await database.query("SELECT id FROM ranked_sessions WHERE revoked_at IS NULL")).rows).toHaveLength(1);
  });

  it("expires a crashed process in 90s; heartbeat renews and an expired token cannot revive", async () => {
    const clock = new FixedClock(), service = buildService(new FakeCsmpSource(), clock);
    const first = await service.create(cleanDto("7001")), firstContext = await contextFor(first.sessionToken);
    const other = { ...cleanDto("7001"), clientInstanceId: "second-device-instance" };
    clock.advance(30000);await service.heartbeat(firstContext);
    clock.advance(89000);await expect(service.create(other)).rejects.toMatchObject({ status: 409 });
    clock.advance(1000);await expect(service.heartbeat(firstContext)).rejects.toMatchObject({ status: 401 });
    await expect(service.create(other)).resolves.toMatchObject({ sessionLeaseSeconds: 90, sessionHeartbeatSeconds: 5 });
    await expect(service.heartbeat(firstContext)).rejects.toMatchObject({ status: 401 });
  });

  it("same-process creation retries rotate tokens; late Close cannot release the replacement", async () => {
    const service = buildService(new FakeCsmpSource());
    const first = await service.create(cleanDto("7001")), old = await contextFor(first.sessionToken);
    const second = await service.create(cleanDto("7001")), replacement = await contextFor(second.sessionToken);
    await service.close(old);await expect(service.heartbeat(replacement)).resolves.toBeDefined();
    await expect(service.create({ ...cleanDto("7001"), clientInstanceId: "another-device-instance" }))
      .rejects.toMatchObject({ status: 409 });
  });

  it("guards all authenticated routes against expired/revoked leases, independently of token TTL", async () => {
    const clock = new FixedClock(), service = buildService(new FakeCsmpSource(), clock);
    const first = await service.create(cleanDto("7001"));
    const guard = new SessionGuard(database, new TokenService(environmentFixture()), clock);
    const request = { headers: { authorization: `Bearer ${first.sessionToken}` } };
    const context = { switchToHttp: () => ({ getRequest: () => request }) } as any;
    // An admitted old build remains valid until its lease ends during a game.
    await expect(guard.canActivate(context)).resolves.toBe(true);
    clock.advance(90000);await expect(guard.canActivate(context)).rejects.toMatchObject({ status: 401 });
    await service.create(cleanDto("7001"));await expect(guard.canActivate(context)).rejects.toMatchObject({ status: 401 });
  });
  it("admits a new profile without CSMP and preserves its placeholder across logins", async () => {
    const source = new FakeCsmpSource();
    const service = buildService(source);
    const first = await service.create(cleanDto("7001"));
    source.tier = "GOLD";
    const second = await service.create(cleanDto("7001"));
    const profile = await database.query<{
      hidden_mmr: number;
      initial_csmp_tier: string;
      initial_seed_mmr: number;
    }>(
      `SELECT hidden_mmr, initial_csmp_tier, initial_seed_mmr
       FROM ranked_profiles rp
       JOIN ranked_players p ON p.id = rp.player_id
       WHERE p.gd_account_id = $1`,
      ["7001"],
    );
    expect(source.calls).toBe(0);
    expect(profile.rows[0]).toMatchObject({
      hidden_mmr: 500,
      initial_csmp_tier: "NONE",
      initial_seed_mmr: 500,
    });
    expect(first.player.displayedTier).toBe("UNRANKED");
    expect(second.sessionToken).not.toBe(first.sessionToken);
  });

  it("ignores an unallowed disabled package when creating a profile", async () => {
    const source = new FakeCsmpSource();
    const service = buildService(source);
    const dto = cleanDto("7002");
    dto.installedMods.push({
      id: "not.allowed",
      version: "1.0.0",
      enabled: false,
      loaded: false,
      internal: false,
      system: false,
    });
    await expect(service.create(dto)).resolves.toBeDefined();
    const players = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_players WHERE gd_account_id = $1",
      ["7002"],
    );
    expect(players.rows[0]?.count).toBe(1);
  });
  it.each(["HwanHee","hwanhee","HWANHEE"])("stores original GD username %s without changing account identity", async gdUsername => {
    const service=buildService(new FakeCsmpSource());
    await service.create({...cleanDto("7001"),gdUsername});
    const rows=await database.query<{gd_username:string;gd_account_id:string}>("SELECT gd_username,gd_account_id::text FROM ranked_players WHERE gd_account_id=7001");
    expect(rows.rows).toEqual([{gd_username:gdUsername,gd_account_id:"7001"}]);
  });
  it("applies dynamic mod policy without deleting an admitted session or its profile", async () => {
    const document=configDocumentFixture();
    const service=buildService(new FakeCsmpSource(),new FixedClock(),undefined,document);
    const dto=cleanDto("7001");dto.installedMods.splice(1); // missing Node IDs / CBF
    dto.installedMods.push({id:"other.mod",version:"1",enabled:true,loaded:true,internal:false,system:false});
    await expect(service.create(dto)).rejects.toMatchObject({status:403});
    Object.assign(document.operational,{modRestrictionEnabled:false});
    const created=await service.create(dto),context=await contextFor(created.sessionToken);
    expect(()=>service.assertEnvironment(dto.installedMods)).not.toThrow();
    Object.assign(document.operational,{modRestrictionEnabled:true});
    expect(()=>service.assertEnvironment(dto.installedMods)).toThrow();
    await expect(service.heartbeat(context)).resolves.toBeDefined();
    Object.assign(document.operational,{modRestrictionEnabled:false});
    expect(()=>service.assertEnvironment([])).not.toThrow();
    await expect(service.heartbeat(context)).resolves.toBeDefined();
    expect((await database.query<{count:number}>("SELECT COUNT(*)::int AS count FROM ranked_players")).rows[0]!.count).toBe(1);
  });

});
