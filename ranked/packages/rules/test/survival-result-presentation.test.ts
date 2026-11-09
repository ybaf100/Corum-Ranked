import { describe, expect, it } from "vitest";
import { SURVIVAL_RESULT_MS, survivalResultMs, survivalSliceMs, type SurvivalMatch, type SurvivalRound } from "../src/index.js";

type Row = { id: string; rank: number; qualified: boolean; effect?: string; kind?: "HUMAN" | "BOT" };
function fixture(rows: Row[]): { match: SurvivalMatch; round: SurvivalRound } {
  const match = { players: rows.map(r => ({ participantId: r.id, kind: r.kind ?? "HUMAN", eliminationEffect: r.effect, finalRank: null })) } as unknown as SurvivalMatch;
  const round = { players: rows.map(r => ({ participantId: r.id, rank: r.rank, qualified: r.qualified, forfeited: false })) } as unknown as SurvivalRound;
  return { match, round };
}
const field = (winners: Row[], survivors: number, out: number): Row[] => [
  ...winners,
  ...Array.from({ length: survivors }, (_, i) => ({ id: `s${i}`, rank: winners.length + 1 + i, qualified: true })),
  ...Array.from({ length: out }, (_, i) => ({ id: `o${i}`, rank: winners.length + survivors + 1 + i, qualified: false })),
];

describe("round result presentation length", () => {
  it("times Slice from the dash count (50-100 ms per dash, 2 s shared)", () => {
    expect(survivalSliceMs(0)).toBe(0);
    expect(survivalSliceMs(1)).toBe(2820);
    expect(survivalSliceMs(24)).toBe(1300 + 23 * 87 + 1520);
    expect(survivalSliceMs(48)).toBe(1300 + 47 * 50 + 1520);
    expect(survivalSliceMs(8)).toBe(1300 + 7 * 100 + 1520);
  });
  it("keeps 5.5 s without a Slice executor", () => {
    const { match, round } = fixture(field([{ id: "me", rank: 1, qualified: true, effect: "BOLT" }], 39, 24));
    expect(survivalResultMs(match, round)).toBe(SURVIVAL_RESULT_MS);
  });
  it("waits for the Slice executor's icon to return home plus a hold", () => {
    const { match, round } = fixture(field([{ id: "me", rank: 1, qualified: true, effect: "SLICE" }], 39, 24));
    expect(survivalResultMs(match, round)).toBe(1950 + survivalSliceMs(24) + 1100);
    expect(survivalResultMs(match, round)).toBe(7871);
  });
  it("uses the larger half for two tied executors and ignores three or more", () => {
    const two = fixture(field([{ id: "a", rank: 1, qualified: true }, { id: "b", rank: 1, qualified: true, effect: "SLICE" }], 38, 23));
    expect(survivalResultMs(two.match, two.round)).toBe(1950 + survivalSliceMs(12) + 1100);
    const three = fixture(field([1, 2, 3].map(i => ({ id: `w${i}`, rank: 1, qualified: true, effect: "SLICE" })), 37, 24));
    expect(survivalResultMs(three.match, three.round)).toBe(SURVIVAL_RESULT_MS);
  });
  it("ignores bots; a single victim still waits for the return", () => {
    const bot = fixture(field([{ id: "bot", rank: 1, qualified: true, effect: "SLICE", kind: "BOT" }], 10, 5));
    expect(survivalResultMs(bot.match, bot.round)).toBe(SURVIVAL_RESULT_MS);
    const one = fixture(field([{ id: "me", rank: 1, qualified: true, effect: "SLICE" }], 0, 1));
    expect(survivalResultMs(one.match, one.round)).toBe(1950 + 2820 + 1100);
  });
});
