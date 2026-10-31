import { gzipSync } from "node:zlib";
import { describe, expect, it } from "vitest";
import { appendSpectatorPacket, readSpectatorStream } from "../src/match/spectator-relay.js";
import { parseSpectatorKeyframe, parseSpectatorPacket, type LiveStream } from "../src/match/spectator-live.protocol.js";
import { SPECTATOR_HISTORY_MAX_BYTES } from "../src/match/spectator-live.policy.js";

const packet = (sequence: number) => parseSpectatorPacket({
  protocolVersion: 2, matchId: "m", roundNumber: 1, deathmatchSequence: null, attemptId: "a", generation: 1,
  sequence, elapsedMs: sequence * 50, levelId: 123, inputEdges: [],
  keyframe: { x: sequence, y: 40, rotation: 0, mode: "CUBE", mini: false, upsideDown: false, flipped: false, dual: false, dead: false, finished: false,
    audio: { trackHash: "a".repeat(64), positionMs: sequence * 50 } },
}, "m");

describe("P2-C spectator relay memory budget", () => {
  it("bounds Solo history bytes and reports the trimmed window as partial, not a full restore", () => {
    const stream: LiveStream = { matchId: "m", slot: 1, side: "A", attemptId: "a", generation: 1, lastSequence: -1, lastReceivedAtMs: 0, packets: [] };
    for (let i = 0; i < 4096; ++i) appendSpectatorPacket(stream, packet(i), i * 50, 4096, 256 * 1024);
    const bytes = stream.history!.reduce((n, p) => n + Buffer.byteLength(JSON.stringify(p)), 0);
    expect(bytes).toBeLessThanOrEqual(256 * 1024);
    expect(stream.historyBytes).toBe(bytes);
    expect(stream.history![0]!.sequence).toBeGreaterThan(0);
    const joined = readSpectatorStream("k", stream, 1, null, "B", undefined, undefined, undefined, true);
    expect(joined.restoration?.available ?? false).toBe(false);
    expect(SPECTATOR_HISTORY_MAX_BYTES).toBe(4 * 1024 * 1024);
  });
  it("keeps a complete history available while under budget", () => {
    const stream: LiveStream = { matchId: "m", slot: 1, side: "A", attemptId: "a", generation: 1, lastSequence: -1, lastReceivedAtMs: 0, packets: [] };
    for (let i = 0; i < 200; ++i) appendSpectatorPacket(stream, packet(i), i * 50);
    expect(stream.history![0]!.sequence).toBe(0);
    const joined = readSpectatorStream("k", stream, 1, null, "B", undefined, undefined, undefined, true);
    expect(joined.restoration?.available).toBe(true);
  });

  it("P2-A baseline: measures per-viewer JSON bytes/s at the 50ms cadence and the gzip headroom", () => {
    const stream: LiveStream = { matchId: "m", slot: 1, side: "A", attemptId: "a", generation: 1, lastSequence: -1, lastReceivedAtMs: 0, packets: [] };
    for (let i = 0; i < 400; ++i) appendSpectatorPacket(stream, packet(i), i * 50);
    const page = JSON.stringify(readSpectatorStream("k", stream, 1, null, "B", "a", "1", "380", false));
    const perPacket = page.length / 19;
    const bytesPerSecond = perPacket * 20;
    const gzipRatio = gzipSync(page).length / page.length;
    console.info(`[P2-A baseline] packet≈${perPacket.toFixed(0)}B json≈${(bytesPerSecond / 1024).toFixed(1)}KiB/s/viewer gzip=${(gzipRatio * 100).toFixed(0)}%`);
    expect(perPacket).toBeGreaterThan(0);
    expect(bytesPerSecond).toBeLessThan(64 * 1024);
  });

  it("accepts the native PLAYER_TELEPORTED evidence callback (Entropie trigger teleport) and still rejects unknown callbacks", () => {
    const keyframe = (callback: string) => ({ x: 25892.316, y: 5437.5, rotation: 0, mode: "SHIP", mini: false, upsideDown: false, flipped: false, dual: false, dead: false, finished: false,
      discontinuity: { type: "TELEPORT", fromX: 25892.316, fromY: 4044, toX: 25892.316, toY: 5437.5,
        nativeEvents: [{ type: "TELEPORT", fromX: 25892.316, fromY: 4044, toX: 25892.316, toY: 5437.5, beforeMode: "SHIP", afterMode: "SHIP", beforeGravity: false, afterGravity: false,
          trace: { event: 1, tick: 8669, step: 33880, elapsedMs: 71295.8, callback } }] } });
    expect(parseSpectatorKeyframe(keyframe("PLAYER_TELEPORTED")).discontinuity?.type).toBe("TELEPORT");
    expect(() => parseSpectatorKeyframe(keyframe("SET_POSITION"))).toThrow();
  });
});
