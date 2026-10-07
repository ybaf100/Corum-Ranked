import { expect } from "vitest";
import type { MatchService } from "../src/match/match.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";

// Existing match-flow regressions now pass through the same native entry API
// as real clients; a resource READY response alone must never start gameplay.
export const enterPreparedMatch = async (
  matches: MatchService,
  matchId: string,
  tokenA: string,
  contextA: RankedSessionContext,
  tokenB: string,
  contextB: RankedSessionContext,
  advanceSeconds: (seconds: number) => void,
): Promise<Record<string, any>> => {
  const waiting = await matches.state(matchId, tokenA, contextA);
  expect(["ROUND_ENTERING", "DEATHMATCH_ENTERING"]).toContain(waiting.state);
  expect(waiting.entry).toMatchObject({ entered: { A: false, B: false }, startsAt: null });
  const entry = waiting.entry!;
  const body = { roundNumber: entry.roundNumber, deathmatchSequence: entry.deathmatchSequence,
    playableLevelId: entry.playableLevelId };
  expect(await matches.startAttempt(matchId, tokenA, contextA, {
    levelId: entry.playableLevelId, clientEventId: `before-entry:${entry.roundNumber}:${entry.deathmatchSequence}`,
  })).toMatchObject({ accepted: false, reason: `ATTEMPT_START_NOT_ACCEPTED_IN_${waiting.state}` });
  const first = await matches.entered(matchId, tokenA, contextA, body);
  if (first.matchType !== "DEBUG_BOT") {
    expect(first.entry!.startsAt).toBeNull();
    await matches.entered(matchId, tokenB, contextB, body);
  }
  const countdown = await matches.state(matchId, tokenA, contextA);
  expect(countdown.entry).toMatchObject({ entered: { A: true, B: true } });
  expect(countdown.entry!.startsAt).not.toBeNull();
  expect((await matches.state(matchId, tokenB, contextB)).entry!.startsAt).toBe(countdown.entry!.startsAt);
  expect(countdown.state).toBe(waiting.state);
  advanceSeconds(3);
  const playing = await matches.state(matchId, tokenA, contextA);
  expect(playing.state).toBe(waiting.state === "ROUND_ENTERING" ? "ROUND_PLAYING" : "DEATHMATCH_PLAYING");
  return playing;
};
