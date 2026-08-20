import { describe, expect, it } from "vitest";
import { formatDiscordRelayMessage } from "../src/relay/discord-message.js";
import { RANKED_RELAY_EVENT_TYPES } from "../src/relay/outbox.service.js";

const basePayload = {
  matchId: "00000000-0000-4000-8000-000000000001",
  players: { A: "Alpha", B: "Beta" },
  roundNumber: 2,
  banner: "MATCH_POINT",
  mapTitle: "Map `with` formatting",
  qualifyingPercent: 62.5,
  side: "A",
  targetSide: "B",
  windowSeconds: 10,
  scores: { A: 201, B: 98 },
  clears: { A: 2, B: 1 },
  result: "A",
  roundWins: { A: 1, B: 0 },
  winnerSide: "A",
  mmrDelta: { A: 18, B: -18 },
  ratingAfter: { A: 1518, B: 1482 },
  roundResults: ["DRAW", "A"],
  sequence: 1,
  scoreA: 250,
  scoreB: 220,
  repeatRequired: false,
};

describe("Discord Ranked relay formatting", () => {
  it("formats every specified event without mentions or oversized content", () => {
    for (const eventType of RANKED_RELAY_EVENT_TYPES) {
      const message = formatDiscordRelayMessage({ eventType, payload: basePayload });
      expect(message.length).toBeGreaterThan(10);
      expect(message.length).toBeLessThanOrEqual(1_900);
      expect(message).not.toContain("`with`");
    }
  });

  it("renders LAST ATTEMPT as a start window rather than one fixed attempt", () => {
    const message = formatDiscordRelayMessage({
      eventType: "LAST_ATTEMPT",
      payload: basePayload,
    });
    expect(message).toContain("10s attempt-start window");
    expect(message).toContain("Beta");
  });
});

