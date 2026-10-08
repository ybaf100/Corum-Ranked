import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

const matchServiceSource = readFileSync(
  fileURLToPath(new URL("../src/match/match.service.ts", import.meta.url)),
  "utf8",
);

describe("Debug Bot Achievement result eligibility", () => {
  it("keeps DEBUG_BOT on the same Achievement result-event path as Ranked PvP", () => {
    expect(matchServiceSource).toContain(
      'current.match_type === "RANKED_PVP" || current.match_type === "DEBUG_BOT"',
    );
  });
});
