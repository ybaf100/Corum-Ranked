import "reflect-metadata";
import { readFile, mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { Controller, Get, UseGuards, type INestApplication } from "@nestjs/common";
import { Test } from "@nestjs/testing";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it, vi } from "vitest";
import { DATABASE } from "../src/database/database.port.js";
import { SERVER_ENVIRONMENT } from "../src/config/server-environment.js";
import { SERVER_CLOCK, ID_GENERATOR } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { DebugSpectateController } from "../src/debug-spectate/debug-spectate.controller.js";
import { DebugSpectateService } from "../src/debug-spectate/debug-spectate.service.js";
import { DEBUG_SPECTATE_LEVEL_ID, DEBUG_SPECTATE_FORMAT_VERSION } from "../src/debug-spectate/debug-spectate.config.js";
import { SpectatorLiveService } from "../src/match/spectator-live.service.js";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import type { MatchService } from "../src/match/match.service.js";
import { loadServerEnvironment } from "../src/config/server-environment.js";
import { environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

class AuthorityProbe { public get() { return { authorized: true }; } }
Controller("api/ranked")(AuthorityProbe);
UseGuards(SessionGuard)(AuthorityProbe);
Get("authority-probe")(AuthorityProbe.prototype, "get", Object.getOwnPropertyDescriptor(AuthorityProbe.prototype, "get")!);

describe("server-backed Debug Spectate over real HTTP and persistent PostgreSQL", () => {
  let app: INestApplication, pg: PGlite, database: PgliteDatabase, dir: string, base: string;
  const environment = { ...environmentFixture(), debugSpectate: { password: "fixture-secret" } };
  const tokens = new TokenService(environment);
  let now = Date.now();
  const clock = { now: () => new Date(now) };
  let id = 30_000;
  const ids = { next: () => `00000000-0000-4000-8000-${String(++id).padStart(12, "0")}` };
  const rankedState = vi.fn(() => { throw new Error("Debug must never call Ranked Match authority"); });
  const rankedMatch = { state: rankedState } as unknown as MatchService;
  const createBody = { password: "fixture-secret", levelId: DEBUG_SPECTATE_LEVEL_ID, formatVersion: DEBUG_SPECTATE_FORMAT_VERSION };
  const sessionToken = "real-ranked-session-for-debug-http";
  const headers = { "content-type": "application/json", "x-corum-debug-spectate": "1", Authorization: `Bearer ${sessionToken}` };
  const startApp = async (enabled = true) => {
    const module = await Test.createTestingModule({ controllers: [DebugSpectateController, AuthorityProbe],
      providers: [DebugSpectateService, SessionGuard, { provide: DATABASE, useValue: database },
        { provide: SERVER_ENVIRONMENT, useValue: { ...environment, debugSpectate: enabled ? environment.debugSpectate : null } },
        { provide: SERVER_CLOCK, useValue: clock }, { provide: ID_GENERATOR, useValue: ids },
        { provide: TokenService, useValue: tokens }, { provide: SpectatorLiveService,
          useValue: new SpectatorLiveService(rankedMatch, new InMemoryMatchRuntimeState()) }] }).compile();
    app = module.createNestApplication();
    app.useLogger(false);
    await app.listen(0, "127.0.0.1");
    base = await app.getUrl();
  };
  const post = async (path: string, body: unknown, token?: string) => {
    const response = await fetch(base + "/api/ranked/debug/spectate/" + path, { method: "POST", headers: {
      ...headers, ...(token ? { "x-debug-spectate-token": token } : {}) }, body: JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  const poll = async (playback: any, query = "") => {
    const response = await fetch(base + `/api/ranked/debug/spectate/playbacks/${playback.playbackId}/spectator-live?protocolVersion=1${query}`,
      { headers: { ...headers, "x-debug-spectate-token": playback.token } });
    return { status: response.status, body: await response.json() as any };
  };
  const frame = (sequence: number, elapsedMs: number, dead = false, finished = false) => ({ sequence, elapsedMs,
    keyframe: { x: 100 + sequence * 10, y: 40 + sequence, rotation: sequence * 3, dead, finished } });
  const startRecord = async () => {
    const response = await post("recordings", createBody);
    expect(response.status).toBe(201); return response.body;
  };
  const completedRecord = async (dead = true) => {
    const recording = await startRecord();
    expect((await post(`recordings/${recording.recordingId}/frames`, { formatVersion: 1,
      frames: [frame(0, 0), frame(1, 50), frame(2, 100, dead, !dead)] }, recording.token)).status).toBe(201);
    expect((await post(`recordings/${recording.recordingId}/finalize`, { completed: true }, recording.token)).status).toBe(201);
    return recording;
  };
  const rankedSnapshot = async () => {
    const names = (await database.query<{ tablename: string }>(`SELECT tablename FROM pg_tables
      WHERE schemaname = 'public' AND tablename NOT LIKE 'debug_spectate_%' ORDER BY tablename`)).rows;
    return Promise.all(names.map(async ({ tablename }) => ({ table: tablename,
      rows: (await database.query(`SELECT * FROM "${tablename}" ORDER BY 1`)).rows })));
  };
  beforeAll(async () => {
    dir = await mkdtemp(join(tmpdir(), "corum-debug-spectate-"));
    pg = new PGlite(dir); database = new PgliteDatabase(pg);
    for (const migration of ["0001_initial_ranked", "0002_attempt_start_leases", "0002_debug_bot_match",
      "0003_debug_rating_playable_maps", "0004_alpha37_schema_hardening", "0005_v050_rating_system",
      "0006_v050_dynamic_csmp_tiers", "0007_solo_stage_placement", "0008_placement_beta9",
      "0009_placement_stage_count", "0010_achievements_v1", "0011_debug_spectate"]) {
      await pg.exec(await readFile(fileURLToPath(new URL(`../../../migrations/${migration}.sql`, import.meta.url)), "utf8"));
    }
    const player = ids.next();
    await database.query(`INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, 30000, 'Recorder')`, [player]);
    await database.query(`INSERT INTO ranked_profiles (player_id, hidden_mmr, displayed_tier, placement_games,
      initial_csmp_tier, initial_seed_mmr, seed_applied_at) VALUES ($1, 2500, 'BRONZE', 5, 'BRONZE', 2500, CURRENT_TIMESTAMP)`, [player]);
    await database.query(`INSERT INTO ranked_sessions (id, player_id, token_hash, client_version, expires_at, environment_snapshot, last_heartbeat_at)
      VALUES ($1, $2, $3, $4, $5, '{}'::jsonb, CURRENT_TIMESTAMP)`, [ids.next(), player, tokens.hash(sessionToken), REQUIRED_RANKED_CLIENT_VERSION, new Date(now + 3_600_000)]);
    await startApp();
  }, 60_000);
  afterAll(async () => { await app?.close(); await pg?.close(); if (dir) await rm(dir, { recursive: true, force: true }); });

  it("requires server opt-in, debug header/password/session, and fixed map/format", async () => {
    expect(loadServerEnvironment({ ...process.env, NODE_ENV: "test", DATABASE_URL: "postgres://localhost/test", RANKED_CONFIG_URL: "http://localhost/config",
      RANKED_SESSION_TOKEN_SECRET: "secret", RANKED_CONFIG_REFRESH_MS: "5000", RANKED_CONFIG_FETCH_TIMEOUT_MS: "1000" }).debugSpectate).toBeNull();
    expect((await post("recordings", { ...createBody, levelId: 123 })).status).toBe(400);
    expect((await post("recordings", { ...createBody, password: "wrong" })).status).toBe(401);
    expect((await fetch(base + "/api/ranked/debug/spectate/recordings", { method: "POST",
      headers: { "content-type": "application/json", Authorization: headers.Authorization }, body: JSON.stringify(createBody) })).status).toBe(404);
    expect((await fetch(base + "/api/ranked/debug/spectate/recordings", { method: "POST", headers: {
      "content-type": "application/json", "x-corum-debug-spectate": "1" }, body: JSON.stringify(createBody) })).status).toBe(401);
    expect((await post("playbacks", createBody)).status).toBe(404);
  });
  it("validates sequential time/pose/terminal batches atomically, with idempotent retries", async () => {
    const recording = await startRecord(); const path = `recordings/${recording.recordingId}`;
    expect((await post(`${path}/frames`, { formatVersion: 1, frames: [frame(1, 0)] }, recording.token)).status).toBe(409);
    expect((await post(`${path}/frames`, { formatVersion: 1, frames: [frame(0, 0), frame(1, 0)] }, recording.token)).status).toBe(409);
    expect((await post(`${path}/frames`, { formatVersion: 1, frames: [frame(0, 0)] }, recording.token)).body.frameCount).toBe(1);
    expect((await post(`${path}/frames`, { formatVersion: 1, frames: [frame(0, 0)] }, recording.token)).body.frameCount).toBe(1);
    expect((await post(`${path}/finalize`, { completed: true }, recording.token)).status).toBe(409);
    expect((await post(`${path}/frames`, { formatVersion: 1, frames: [frame(1, 50, true)] }, recording.token)).status).toBe(201);
    expect((await post(`${path}/frames`, { formatVersion: 1, frames: [frame(2, 100)] }, recording.token)).status).toBe(409);
    const results = await Promise.all([post(`${path}/finalize`, { completed: true }, recording.token), post(`${path}/finalize`, { completed: true }, recording.token)]);
    expect(results.map(r => r.status)).toEqual([201, 201]);
    expect((await post(`${path}/finalize`, { completed: false }, recording.token)).status).toBe(409);
    expect((await database.query("SELECT * FROM debug_spectate_frames WHERE recording_id = $1", [recording.recordingId])).rows).toHaveLength(2);
  });
  it("persists Clear and quit separately, excludes incomplete latest and survives server + database restart", async () => {
    const recording = await completedRecord(false);
    now += 1000;
    const cancelled = await startRecord();
    await post(`recordings/${cancelled.recordingId}/finalize`, { completed: false }, cancelled.token);
    await app.close(); await pg.close();
    pg = new PGlite(dir); database = new PgliteDatabase(pg); await startApp();
    const playback = await post("playbacks", createBody);
    expect(playback.status).toBe(201); expect(playback.body.recordingId).toBe(recording.recordingId);
    expect(playback.body.frameCount).toBe(3); expect(playback.body.levelId).toBe(DEBUG_SPECTATE_LEVEL_ID);
    expect(playback.body.frames).toBeUndefined(); expect(playback.body.packets).toBeUndefined();
    expect((await post(`recordings/${recording.recordingId}/finalize`, { completed: true }, recording.token)).status).toBe(201);
  }, 60_000);
  it("starts WAITING over HTTP, only server clock advances packets, reuses production stream/terminal/sequence semantics", async () => {
    const before = await rankedSnapshot(); const playback = (await post("playbacks", createBody)).body;
    const first = await poll(playback); expect(first.status).toBe(200);
    expect(first.body.waiting).toBe(true); expect(first.body.packets).toEqual([]);
    now += 50;
    const initial = (await poll(playback)).body;
    expect(initial.reset).toBe(true); expect(initial.packets).toHaveLength(1);
    expect(initial.stream.matchId).toBe(playback.contextId);
    expect(initial.packets[0]).toMatchObject({ protocolVersion: 1, generation: 1, sequence: 0, elapsedMs: 0, roundNumber: 1, deathmatchSequence: null });
    const query = `&attemptId=${initial.stream.attemptId}&generation=1&afterSequence=0`;
    expect((await poll(playback, query)).body.packets).toEqual([]); // No client clock / FPS progression.
    now += 50;
    expect((await poll(playback, query)).body.packets.map((p: any) => p.sequence)).toEqual([1]);
    now += 50;
    const terminal = (await poll(playback, "&attemptId=" + initial.stream.attemptId + "&generation=1&afterSequence=1")).body;
    expect(terminal.playbackComplete).toBe(true); expect(terminal.packets[0].keyframe.finished).toBe(true);
    now += 5000;
    expect((await poll(playback)).body.packets.at(-1).keyframe.finished).toBe(true); // Terminal survives normal relay timeout.
    const stale = (await poll(playback, `&attemptId=${initial.stream.attemptId}&generation=99&afterSequence=999`)).body;
    expect(stale.reset).toBe(true); // Production identity reset, not unvalidated pose injection.
    const gap = (await poll(playback, `&attemptId=${initial.stream.attemptId}&generation=1&afterSequence=999`)).body;
    expect(gap.fallback).toBe(true); expect(gap.reason).toBe("SEQUENCE_GAP");
    const recovered = (await poll(playback, query)).body; expect(recovered.reset).toBe(true); expect(recovered.stream.generation).toBe(2);
    expect((await fetch(base + "/api/ranked/authority-probe", { headers: { Authorization: `Bearer ${playback.token}` } })).status).toBe(401);
    expect((await poll({ ...playback, token: "invalid" })).status).toBe(401);
    expect((await post(`playbacks/${playback.playbackId}/close`, {}, playback.token)).status).toBe(201);
    expect((await poll(playback)).status).toBe(404);
    expect(await rankedSnapshot()).toEqual(before); expect(rankedState).not.toHaveBeenCalled();
  });
  it("emits a persisted death terminal, validates protocol, expires scoped playback and bounds concurrent admission", async () => {
    await completedRecord(true);
    const playback = (await post("playbacks", createBody)).body;
    await poll(playback); now += 150;
    const terminal = (await poll(playback)).body;
    expect(terminal.packets.at(-1).keyframe.dead).toBe(true);
    expect(terminal.playbackComplete).toBe(true);
    const badProtocol = await fetch(base + `/api/ranked/debug/spectate/playbacks/${playback.playbackId}/spectator-live?protocolVersion=99`,
      { headers: { ...headers, "x-debug-spectate-token": playback.token } });
    expect((await badProtocol.json() as any).reason).toBe("UNSUPPORTED_PROTOCOL_VERSION");
    const admitted = await Promise.all(Array.from({ length: 20 }, () => post("playbacks", createBody)));
    expect(admitted.filter(r => r.status === 201).length).toBeLessThanOrEqual(16);
    expect(admitted.some(r => r.status === 409)).toBe(true);
    now += 15 * 60_000;
    expect([401, 404]).toContain((await poll(playback)).status);
    expect((await post("playbacks", createBody)).status).toBe(201); // Expired memory/session buffers cleaned.
  });
  it("OFF server rejects recording/playback even with valid authorization without creating debug data", async () => {
    await app.close(); await startApp(false);
    const before = await database.query("SELECT count(*) FROM debug_spectate_recordings");
    expect((await post("recordings", createBody)).status).toBe(404);
    expect((await post("playbacks", createBody)).status).toBe(404);
    expect(await database.query("SELECT count(*) FROM debug_spectate_recordings")).toEqual(before);
  });
});
