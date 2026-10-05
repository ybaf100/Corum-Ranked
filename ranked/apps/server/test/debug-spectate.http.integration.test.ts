import "reflect-metadata";
import { readFile, writeFile, mkdtemp, rm } from "node:fs/promises";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
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
    const response = await fetch(base + `/api/ranked/debug/spectate/playbacks/${playback.playbackId}/spectator-live?protocolVersion=2${query}`,
      { headers: { ...headers, "x-debug-spectate-token": playback.token } });
    return { status: response.status, body: await response.json() as any };
  };
  const frame = (sequence: number, elapsedMs: number, dead = false, finished = false) => ({ sequence, elapsedMs,
    keyframe: { x: 100 + sequence * 10, y: 40 + sequence, rotation: sequence * 3, mode: "CUBE", mini: false, upsideDown: false, flipped: false, dual: false, dead, finished } });
  const startRecord = async () => {
    const response = await post("recordings", createBody);
    expect(response.status).toBe(201); return response.body;
  };
  const completedRecord = async (dead = true) => {
    const recording = await startRecord();
    expect((await post(`recordings/${recording.recordingId}/frames`, { formatVersion: 2,
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
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: [frame(1, 0)] }, recording.token)).status).toBe(409);
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: [frame(0, 0), frame(1, 0)] }, recording.token)).status).toBe(409);
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: [frame(0, 0)] }, recording.token)).body.frameCount).toBe(1);
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: [frame(0, 0)] }, recording.token)).body.frameCount).toBe(1);
    expect((await post(`${path}/finalize`, { completed: true }, recording.token)).status).toBe(409);
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: [frame(1, 50, true)] }, recording.token)).status).toBe(201);
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: [frame(2, 100)] }, recording.token)).status).toBe(409);
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
  it("first HTTP reply emits t=0, only server clock advances packets, reuses production stream/terminal/sequence semantics", async () => {
    const before = await rankedSnapshot(); const playback = (await post("playbacks", createBody)).body;
    const first = await poll(playback); expect(first.status).toBe(200);
    expect(first.body.waiting).toBe(false);
    const initial = first.body;
    expect(initial.reset).toBe(true); expect(initial.packets).toHaveLength(1);
    expect(initial.stream.matchId).toBe(playback.contextId);
    expect(initial.packets[0]).toMatchObject({ protocolVersion: 2, generation: 1, sequence: 0, elapsedMs: 0, roundNumber: 1, deathmatchSequence: null });
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
  it("persists all v2 modes/mini/gravity through real HTTP and replays the same JSONB via production relay", async () => {
    const before = await rankedSnapshot(); now += 1000;
    const recording = await startRecord();
    const modes = ["CUBE", "SHIP", "BALL", "UFO", "WAVE", "ROBOT", "SPIDER", "SWING"];
    const frames = modes.map((mode, sequence) => ({ ...frame(sequence, sequence * 50),
      keyframe: { ...frame(sequence, sequence * 50).keyframe, mode, mini: sequence % 2 === 1,
        upsideDown: sequence >= 4, flipped: sequence >= 4, finished: sequence === 7 } }));
    expect((await post(`recordings/${recording.recordingId}/frames`, { formatVersion: 2, frames }, recording.token)).status).toBe(201);
    expect((await post(`recordings/${recording.recordingId}/finalize`, { completed: true }, recording.token)).status).toBe(201);
    const stored = (await database.query<{ keyframe: unknown }>("SELECT keyframe FROM debug_spectate_frames WHERE recording_id = $1 ORDER BY sequence", [recording.recordingId])).rows;
    expect(stored.map(row => row.keyframe)).toEqual(frames.map(f => f.keyframe));
    const playback = (await post("playbacks", createBody)).body;
    expect(playback.recordingId).toBe(recording.recordingId); expect(playback.formatVersion).toBe(2);
    expect(playback.frames).toBeUndefined(); const first = (await poll(playback)).body;
    expect(first.packets[0].keyframe).toEqual(frames[0]!.keyframe);
    now += 350; const rest = (await poll(playback, `&attemptId=${first.stream.attemptId}&generation=1&afterSequence=0`)).body;
    expect(rest.packets.map((p: any) => p.keyframe)).toEqual(frames.slice(1).map(f => f.keyframe));
    expect(rest.playbackComplete).toBe(true); expect(await rankedSnapshot()).toEqual(before);
    expect((await post(`playbacks/${playback.playbackId}/close`, {}, playback.token)).status).toBe(201);
  });
  it.each([true, false])("normalizes persisted leading time, preserves relative timing/terminal/camera over HTTP (dead=%s)", async dead => {
    now += 1000; const before = await rankedSnapshot(); const recording = await startRecord();
    const leading = 1239;
    const frames = [frame(0, leading), frame(1, leading + 51), frame(2, leading + 101, dead, !dead)].map((f, i) => ({
      ...f, keyframe: { ...f.keyframe, cameraX: 700 + i * 10, cameraY: -20 + i * 5, cameraSnap: false, visible: true },
    }));
    frames[1]!.keyframe.x = frames[0]!.keyframe.x + 729.7268;
    frames[2]!.keyframe.x = frames[1]!.keyframe.x + 10;
    const snap = { type: "TELEPORT", fromX: frames[0]!.keyframe.x, fromY: frames[0]!.keyframe.y,
      toX: frames[1]!.keyframe.x, toY: frames[1]!.keyframe.y };
    const recorded = frames.map((f, i) => i === 1 ? { ...f, keyframe: { ...f.keyframe, discontinuity: snap, cameraSnap: true } } : f);
    const path = `recordings/${recording.recordingId}`;
    expect((await post(`${path}/frames`, { formatVersion: 2, frames: recorded }, recording.token)).status).toBe(201);
    expect((await post(`${path}/finalize`, { completed: true }, recording.token)).status).toBe(201);
    const persisted = (await database.query<{ elapsed_ms: number; keyframe: unknown }>(
      "SELECT elapsed_ms, keyframe FROM debug_spectate_frames WHERE recording_id = $1 ORDER BY sequence", [recording.recordingId])).rows;
    expect(persisted.map(f => f.elapsed_ms)).toEqual([1239, 1290, 1340]);
    expect(persisted.map(f => f.keyframe)).toEqual(recorded.map(f => f.keyframe));
    const playback = (await post("playbacks", createBody)).body;
    expect(playback).toMatchObject({ playbackLeadingMs: leading, playbackDurationMs: 101, durationMs: 1340 });
    const first = (await poll(playback)).body;
    expect(first.packets).toHaveLength(1); expect(first.packets[0]).toMatchObject({ elapsedMs: 0, keyframe: recorded[0]!.keyframe });
    const query = `&attemptId=${first.stream.attemptId}&generation=1&afterSequence=0`;
    // Repeated render polls never advance server playback time.
    for (let render = 0; render < 144; ++render) expect((await poll(playback, query)).body.packets).toEqual([]);
    now += 50; expect((await poll(playback, query)).body.packets).toEqual([]);
    now += 1; const middle = (await poll(playback, query)).body;
    expect(middle.packets).toHaveLength(1); expect(middle.packets[0]).toMatchObject({ elapsedMs: 51, keyframe: recorded[1]!.keyframe });
    expect(middle.playbackComplete).toBe(false);
    now += 49; expect((await poll(playback, query + "&unused=ignored")).body.playbackComplete).toBe(false);
    now += 1; const terminal = (await poll(playback, `&attemptId=${first.stream.attemptId}&generation=1&afterSequence=1`)).body;
    expect(terminal.playbackComplete).toBe(true);
    expect(terminal.packets[0]).toMatchObject({ elapsedMs: 101, keyframe: recorded[2]!.keyframe });
    const wirePath = join(dir, `http-wire-${dead ? "dead" : "finished"}.json`);
    await writeFile(wirePath, JSON.stringify([first, middle, terminal]));
    await promisify(execFile)(process.execPath, ["--test", fileURLToPath(new URL(
      "../../../../corum-ranked-mod/tests/SpectatorRemoteNativeTest.mjs", import.meta.url))], {
      env: { ...process.env, CORUM_SPECTATOR_WIRE_FIXTURE: wirePath }, maxBuffer: 4 * 1024 * 1024,
    }).catch(error => { throw new Error(String(error.stdout) + String(error.stderr)); });
    now += 5000; expect((await poll(playback)).body.packets.at(-1).keyframe).toEqual(recorded[2]!.keyframe);
    expect(await rankedSnapshot()).toEqual(before); expect(rankedState).not.toHaveBeenCalled();
    await post(`playbacks/${playback.playbackId}/close`, {}, playback.token);
  }, 60_000);
  it("drops only invalid camera fields and rejects malformed native event evidence over actual HTTP", async () => {
    now += 1000; const recording = await startRecord(); const path = `recordings/${recording.recordingId}/frames`;
    const pose = frame(0, 0);
    for (const discontinuity of [true, { type: "ALLOW_LARGE_JUMP" }, { type: "TELEPORT", fromX: 0, fromY: 0, toX: 1e20, toY: 0 }]) {
      expect((await post(path, { formatVersion: 2, frames: [{ ...pose, keyframe: { ...pose.keyframe, discontinuity } }] }, recording.token)).status).toBe(400);
    }
    expect((await post(path, { formatVersion: 2, frames: [{ ...pose, keyframe: { ...pose.keyframe, cameraX: 1e20, cameraY: 0 } }] }, recording.token)).status).toBe(201);
    expect((await post(path, { formatVersion: 2, frames: [{ ...frame(1, 50, true),
      keyframe: { ...frame(1, 50, true).keyframe, cameraX: null, cameraY: 40 } }] }, recording.token)).status).toBe(201);
    await post(`recordings/${recording.recordingId}/finalize`, { completed: true }, recording.token);
    const persisted = (await database.query<{ keyframe: any }>("SELECT keyframe FROM debug_spectate_frames WHERE recording_id = $1", [recording.recordingId])).rows;
    expect(persisted).toHaveLength(2);
    for (const f of persisted) { expect(f.keyframe.cameraX).toBeUndefined(); expect(f.keyframe.cameraY).toBeUndefined(); expect(f.keyframe.x).toBeGreaterThan(0); }
  });
  it("excludes old completed v1 recordings even when newer, and rejects corrupt expanded state", async () => {
    now += 1000; const good = await completedRecord();
    const old = ids.next();
    await database.query(`INSERT INTO debug_spectate_recordings
      (id, level_id, format_version, sample_interval_ms, completed, created_at, duration_ms, frame_count, token_hash, expires_at)
      VALUES ($1, $2, 1, 50, true, $3, 50, 2, 'old-format-fixture', $3)`, [old, DEBUG_SPECTATE_LEVEL_ID, new Date(now + 10000)]);
    expect((await post("playbacks", createBody)).body.recordingId).toBe(good.recordingId);
    expect((await post("recordings", { ...createBody, formatVersion: 1 })).status).toBe(400);
    const recording = await startRecord();
    const invalid = frame(0, 0); invalid.keyframe.mode = "UNKNOWN";
    expect((await post(`recordings/${recording.recordingId}/frames`, { formatVersion: 2, frames: [invalid] }, recording.token)).status).toBe(400);
    expect((await database.query("SELECT * FROM debug_spectate_frames WHERE recording_id = $1", [recording.recordingId])).rows).toEqual([]);
  });
  it("OFF server rejects recording/playback even with valid authorization without creating debug data", async () => {
    await app.close(); await startApp(false);
    const before = await database.query("SELECT count(*) FROM debug_spectate_recordings");
    expect((await post("recordings", createBody)).status).toBe(404);
    expect((await post("playbacks", createBody)).status).toBe(404);
    expect(await database.query("SELECT count(*) FROM debug_spectate_recordings")).toEqual(before);
  });
});
