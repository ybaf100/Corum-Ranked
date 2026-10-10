import { describe, expect, it, vi } from 'vitest';
import { DebugBotService } from '../src/debug-bot/debug-bot.service.js';
import type { ServerEnvironment } from '../src/config/server-environment.js';

describe('Debug scenario durable checkpoints', () => {
  it('resumes after a committed End loses its ACK without double-clearing the Bot prefix', async () => {
    const clears = { A: 0, B: 0 };
    const state = () => ({ state: 'ROUND_PLAYING', currentRound: {
      roundNumber: 1, map: { playableLevelId: '12345' }, clears: { ...clears },
    } });
    const starts = new Map<string, string>();
    const ends = new Set<string>();
    let loseAck = true;
    const startAttempt = vi.fn(async (_id, _token, _context, body) => {
      const attemptId = starts.get(body.clientEventId) ?? `attempt-${starts.size}`;
      starts.set(body.clientEventId, attemptId);
      return { accepted: true, attemptId };
    });
    const endAttempt = vi.fn(async (_id, _token, context, body) => {
      if (!ends.has(body.clientEventId)) { ends.add(body.clientEventId); clears[context.playerId as 'A' | 'B'] += 1; }
      if (loseAck) { loseAck = false; throw new Error('End committed but acknowledgement lost'); }
      return { accepted: true };
    });
    const matches = { startAttempt, endAttempt, state: vi.fn(async () => state()) };
    const service = new DebugBotService({ debugBot: null } as unknown as ServerEnvironment,
      {} as any, { now: () => new Date() }, { next: () => .5 } as any, {} as any,
      matches as any, {} as any, {} as any);
    const creation = { matchId: 'm', playerMatchToken: 'a', botMatchToken: 'b',
      playerContext: { playerId: 'A' }, botContext: { playerId: 'B' }, settings: { scenario: 'TRIGGER_ROUND_DRAW' } };
    const driver = (service as any).newDriver(creation);
    await expect((service as any).seedScenarioRound(driver, state(), state().currentRound)).rejects.toThrow('acknowledgement');
    expect(clears).toEqual({ A: 0, B: 1 }); expect(driver.seededRounds.has(1)).toBe(false);
    // A recovered driver has no in-memory checkpoint. Durable counts still win.
    const recovered = (service as any).newDriver(creation);
    await (service as any).seedScenarioRound(recovered, state(), state().currentRound);
    expect(clears).toEqual({ A: 3, B: 3 }); expect(startAttempt).toHaveBeenCalledTimes(6);
    expect(ends.size).toBe(6); expect(recovered.seededRounds.has(1)).toBe(true);
  });

  it('does not mark a rejected native-entry/start boundary as seeded', async () => {
    const state = { state: 'ROUND_PLAYING', currentRound: { roundNumber: 1,
      map: { playableLevelId: '12345' }, clears: { A: 0, B: 0 } } };
    const matches = { startAttempt: vi.fn(async () => ({ accepted: false, reason: 'NATIVE_ENTRY_NOT_GRANTED', attemptId: null })),
      endAttempt: vi.fn(), state: vi.fn(async () => state) };
    const service = new DebugBotService({ debugBot: null } as unknown as ServerEnvironment,
      {} as any, { now: () => new Date() }, { next: () => .5 } as any, {} as any,
      matches as any, {} as any, {} as any);
    const driver = (service as any).newDriver({ matchId: 'm', playerMatchToken: 'a', botMatchToken: 'b',
      playerContext: { playerId: 'A' }, botContext: { playerId: 'B' }, settings: { scenario: 'TRIGGER_ROUND_DRAW' } });
    await (service as any).seedScenarioRound(driver, state, state.currentRound);
    expect(driver.seededRounds.has(1)).toBe(false); expect(matches.endAttempt).not.toHaveBeenCalled();
  });
});
