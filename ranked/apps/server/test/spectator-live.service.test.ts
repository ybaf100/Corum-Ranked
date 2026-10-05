import { BadRequestException, ForbiddenException } from "@nestjs/common";
import { afterEach, describe, expect, it, vi } from "vitest";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { SpectatorLiveService } from "../src/match/spectator-live.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";

const session = { playerId: "player-a" } as RankedSessionContext;

const standardState = (
  side: "A" | "B",
  spectator: boolean,
  state = "ROUND_SETTLING",
  roundNumber = 2,
) => ({
  matchId: "match-1",
  state,
  side,
  opponentPractice: false,
  players: {
    A: { gdUsername: "alpha" },
    B: { gdUsername: "bravo" },
  },
  currentRound: { roundNumber },
  deathmatch: null,
  spectator: spectator
    ? { active: true, opponentName: side === "A" ? "bravo" : "alpha", currentProgress: 42 }
    : { active: false },
});

const packet = (overrides: Record<string, unknown> = {}) => ({
  protocolVersion: 2,
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
    mode: "CUBE", mini: false, upsideDown: false, flipped: false, dual: false,
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
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("rejects stream subscription for a viewer without spectator.active", async () => {
    const { service, stateCall } = setup();
    await expect(
      service.subscribe("match-1", "match-token", session),
    ).rejects.toBeInstanceOf(ForbiddenException);
    expect(stateCall).toHaveBeenCalledWith("match-1", "match-token", session);
  });

  it("propagates MatchService authentication failures for invalid match tokens", async () => {
    const { service, stateCall } = setup();
    stateCall.mockRejectedValueOnce(new ForbiddenException("invalid match token"));
    await expect(
      service.subscribe("match-1", "wrong-token", session),
    ).rejects.toThrow("invalid match token");
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

  it("rejects route/body match and authoritative round mismatches", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await expect(
      service.publish("match-1", "match-token", session, packet({ matchId: "match-2" })),
    ).rejects.toBeInstanceOf(BadRequestException);
    await expect(
      service.publish("match-1", "match-token", session, packet({ roundNumber: 3 })),
    ).rejects.toThrow("SPECTATOR_CONTEXT_MISMATCH");
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
      packet({ sequence: 1, elapsedMs: 60, keyframe: { ...packet().keyframe, x: 126, y: 50, rotation: 18, dead: false, finished: false } }),
    );

    setState(standardState("A", true));
    const lateJoin = await service.subscribe("match-1", "match-token", session, "2");
    expect(lateJoin).toMatchObject({ active: true, waiting: false, fallback: false, reset: true });
    expect(lateJoin.packets.map((item) => item.sequence)).toEqual([0, 1]);
    expect(lateJoin.packets[0]).not.toHaveProperty("receivedAtMs");

    setState(standardState("B", false));
    await service.publish(
      "match-1",
      "match-token",
      session,
      packet({ sequence: 2, elapsedMs: 110, keyframe: { ...packet().keyframe, x: 132, y: 54, rotation: 20, dead: false, finished: false } }),
    );
    setState(standardState("A", true));
    const incremental = await service.subscribe(
      "match-1",
      "match-token",
      session,
      "2",
      "attempt-b",
      "1",
      "1",
    );
    expect(incremental).toMatchObject({ fallback: false, reset: false });
    expect(incremental.packets.map((item) => item.sequence)).toEqual([2]);
  });

  it("bounds late join by both 72 packets and the three-second window", async () => {
    const { service, runtime, setState } = setup();
    const now = vi.spyOn(Date, "now").mockReturnValue(10_000);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    for (let sequence = 0; sequence < 80; sequence += 1) {
      await service.publish("match-1", "match-token", session, packet({ sequence }));
    }
    setState(standardState("A", true));
    const capped = await service.subscribe("match-1", "match-token", session, "2");
    expect(capped.packets).toHaveLength(72);
    expect(capped.packets[0]?.sequence).toBe(8);
    expect(capped.packets.at(-1)?.sequence).toBe(79);

    setState(standardState("B", false));
    service.clearAttempt("match-1", "attempt-b");
    runtime.endAttempt("match-1", 2, "B", "attempt-b", false);
    runtime.beginAttempt("match-1", 2, "B", "attempt-new", 11_000);
    now.mockReturnValue(20_000);
    await service.publish("match-1", "match-token", session, packet({ attemptId: "attempt-new" }));
    for (const [sequence, timestamp] of [[1, 21_000], [2, 22_000], [3, 23_001]] as const) {
      now.mockReturnValue(timestamp);
      await service.publish("match-1", "match-token", session, packet({
        attemptId: "attempt-new",
        sequence,
      }));
    }
    setState(standardState("A", true));
    const windowed = await service.subscribe("match-1", "match-token", session, "2");
    expect(windowed.packets.map((item) => item.sequence)).toEqual([1, 2, 3]);
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

  it("falls back when the subscriber cursor has fallen behind the retained buffer", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    for (let sequence = 0; sequence < 80; sequence += 1) {
      await service.publish("match-1", "match-token", session, packet({ sequence }));
    }
    setState(standardState("A", true));
    const result = await service.subscribe(
      "match-1",
      "match-token",
      session,
      "2",
      "attempt-b",
      "1",
      "0",
    );
    expect(result).toMatchObject({ fallback: true, reason: "SEQUENCE_GAP" });
  });

  it("discards a timed-out stream and requires a fresh sequence-zero generation", async () => {
    const { service, runtime, setState } = setup();
    const now = vi.spyOn(Date, "now").mockReturnValue(10_000);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    now.mockReturnValue(11_501);
    setState(standardState("A", true));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ fallback: true, reason: "STREAM_TIMEOUT" });

    setState(standardState("B", false));
    await expect(
      service.publish("match-1", "match-token", session, packet({ sequence: 1 })),
    ).rejects.toThrow("SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO");
    await expect(
      service.publish("match-1", "match-token", session, packet({ generation: 2 })),
    ).resolves.toMatchObject({ generation: 2, sequence: 0 });
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

  it("removes buffers on Attempt end, round change, and match end", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    service.clearAttempt("match-1", "attempt-b");
    setState(standardState("A", true));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ waiting: true, reason: "STREAM_NOT_READY" });

    setState(standardState("B", false));
    await service.publish("match-1", "match-token", session, packet());
    runtime.beginAttempt("match-1", 3, "B", "attempt-new", 2_000);
    setState(standardState("B", false, "ROUND_PLAYING", 3));
    await service.publish("match-1", "match-token", session, packet({
      roundNumber: 3,
      attemptId: "attempt-new",
    }));
    setState(standardState("A", true));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ waiting: true, reason: "STREAM_NOT_READY" });

    setState(standardState("B", false, "ROUND_PLAYING", 3));
    await service.publish("match-1", "match-token", session, packet({
      roundNumber: 3,
      attemptId: "attempt-new",
    }));
    service.decorateState({
      ...standardState("A", false, "MATCH_RESULT", 3),
      currentRound: null,
    });
    setState(standardState("A", true, "ROUND_PLAYING", 3));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ waiting: true, reason: "STREAM_NOT_READY" });
  });
  it("keeps the exact server-ended Attempt for terminal HTTP races, then expires it", async () => {
    const { service, runtime, setState } = setup();
    let now = 10000; vi.spyOn(Date, "now").mockImplementation(() => now);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", now);
    await service.publish("match-1", "match-token", session, packet());
    runtime.endAttempt("match-1", 2, "B", "attempt-b", false);
    await expect(service.publish("match-1", "match-token", session,
      packet({ sequence: 1, elapsedMs: 60, keyframe: { ...packet().keyframe, dead: true } }))).resolves.toMatchObject({ accepted: true });
    setState(standardState("A", true));
    expect((await service.subscribe("match-1", "match-token", session, "2")).packets.at(-1)?.keyframe.dead).toBe(true);
    now += 1501;
    expect((await service.subscribe("match-1", "match-token", session, "2")).waiting).toBe(true);
    setState(standardState("B", false));
    await expect(service.publish("match-1", "match-token", session,
      packet({ sequence: 2, keyframe: { ...packet().keyframe, dead: true } }))).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
  });
  it("rejects wrong ended identity/terminal outcome and reviving the same Attempt, but permits a new Attempt", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", Date.now());
    await service.publish("match-1", "match-token", session, packet());
    runtime.endAttempt("match-1", 2, "B", "attempt-b", true);
    await expect(service.publish("match-1", "match-token", session,
      packet({ sequence: 1, keyframe: { ...packet().keyframe, dead: true } }))).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
    await service.publish("match-1", "match-token", session,
      packet({ sequence: 1, keyframe: { ...packet().keyframe, finished: true } }));
    await expect(service.publish("match-1", "match-token", session, packet({ sequence: 2 }))).rejects.toThrow();
    await expect(service.publish("match-1", "match-token", session,
      packet({ generation: 2, keyframe: { ...packet().keyframe, finished: true } }))).rejects.toThrow("SPECTATOR_TERMINAL_STREAM");
    runtime.beginAttempt("match-1", 2, "B", "new-attempt", Date.now());
    await expect(service.publish("match-1", "match-token", session,
      packet({ attemptId: "new-attempt", generation: 2 }))).resolves.toMatchObject({ accepted: true });
  });

});
