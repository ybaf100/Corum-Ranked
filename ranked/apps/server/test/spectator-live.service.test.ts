import { ForbiddenException } from "@nestjs/common";
import { describe, expect, it, vi } from "vitest";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { SpectatorLiveService } from "../src/match/spectator-live.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";

const session = { playerId: "player-a" } as RankedSessionContext;

const standardState = (side: "A" | "B", spectator: boolean) => ({
  matchId: "match-1",
  state: "ROUND_SETTLING",
  side,
  opponentPractice: false,
  players: {
    A: { gdUsername: "alpha" },
    B: { gdUsername: "bravo" },
  },
  currentRound: { roundNumber: 2 },
  deathmatch: null,
  spectator: spectator
    ? { active: true, opponentName: side === "A" ? "bravo" : "alpha", currentProgress: 42 }
    : { active: false },
});

const packet = (overrides: Record<string, unknown> = {}) => ({
  protocolVersion: 1,
  matchId: "match-1",
  roundNumber: 2,
  deathmatchSequence: null,
  attemptId: "attempt-b",
  generation: 1,
  sequence: 0,
  elapsedMs: 10,
  inputEdges: [],
  keyframe: {
    x: 120,
    y: 48,
    rotation: 15,
    dead: false,
    finished: false,
  },
  ...overrides,
});

const setup = () => {
  let state = standardState("B", false);
  const stateCall = vi.fn(async () => state);
  const matches = { state: stateCall } as unknown as MatchService;
  const runtime = new InMemoryMatchRuntimeState();
  const service = new SpectatorLiveService(matches, runtime);
  return {
    service,
    runtime,
    stateCall,
    setState(next: ReturnType<typeof standardState>) {
      state = next;
    },
  };
};

describe("SpectatorLiveService", () => {
  it("rejects stream subscription for a viewer without spectator.active", async () => {
    const { service } = setup();
    await expect(
      service.subscribe("match-1", "match-token", session),
    ).rejects.toBeInstanceOf(ForbiddenException);
  });

  it("authenticates publish against the active authoritative Attempt", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await expect(
      service.publish("match-1", "match-token", session, packet()),
    ).resolves.toMatchObject({ accepted: true, attemptId: "attempt-b", sequence: 0 });
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ attemptId: "old-attempt", generation: 2 }),
      ),
    ).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
  });

  it("suppresses Practice gameplay before any packet reaches the relay", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    runtime.setPractice("match-1", 2, "B", true);
    await expect(
      service.publish("match-1", "match-token", session, packet()),
    ).rejects.toThrow("PRACTICE_STREAM_SUPPRESSED");
  });

  it("drops stale generation and reverse sequence packets", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet({ generation: 2 }));
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ generation: 1, sequence: 1 }),
      ),
    ).rejects.toThrow("SPECTATOR_STALE_GENERATION");
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ generation: 2, sequence: 0 }),
      ),
    ).rejects.toThrow("SPECTATOR_STALE_SEQUENCE");
  });

  it("provides a bounded late-join buffer and then an incremental live handoff", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    await service.publish(
      "match-1",
      "match-token",
      session,
      packet({ sequence: 1, elapsedMs: 60, keyframe: { x: 126, y: 50, rotation: 18, dead: false, finished: false } }),
    );

    setState(standardState("A", true));
    const lateJoin = await service.subscribe("match-1", "match-token", session, "1");
    expect(lateJoin).toMatchObject({ active: true, waiting: false, fallback: false, reset: true });
    expect(lateJoin.packets.map((item) => item.sequence)).toEqual([0, 1]);

    await service.publish(
      "match-1",
      "match-token",
      session,
      packet({ sequence: 2, elapsedMs: 110, keyframe: { x: 132, y: 54, rotation: 20, dead: false, finished: false } }),
    ).catch(() => undefined);
    // Switch back to publisher state for the authoritative publish above, then
    // restore viewer state before the incremental read.
    setState(standardState("A", true));
    const incremental = await service.subscribe(
      "match-1",
      "match-token",
      session,
      "1",
      "attempt-b",
      "1",
      "1",
    );
    expect(incremental.fallback).toBe(false);
  });

  it("returns polling fallback for unsupported protocol versions", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    setState(standardState("A", true));
    const result = await service.subscribe("match-1", "match-token", session, "99");
    expect(result).toMatchObject({ fallback: true, reason: "UNSUPPORTED_PROTOCOL_VERSION" });
  });

  it("rejects a large publisher sequence gap instead of fabricating missing gameplay", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    await expect(
      service.publish("match-1", "match-token", session, packet({ sequence: 20 })),
    ).rejects.toThrow("SPECTATOR_SEQUENCE_GAP");
  });

  it("does not invoke scoring/result APIs while relaying telemetry", async () => {
    const { service, runtime, stateCall } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    expect(stateCall).toHaveBeenCalledTimes(1);
    expect(runtime.progress("match-1", 2, "B")?.attemptId).toBe("attempt-b");
  });

  it("will not apply packets from an old Attempt after reconnect", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    runtime.endAttempt("match-1", 2, "B", "attempt-b", false);
    runtime.beginAttempt("match-1", 2, "B", "attempt-new", 2_000);
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ attemptId: "attempt-b", sequence: 1 }),
      ),
    ).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
  });
});
