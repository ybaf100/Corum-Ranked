import { describe, expect, it } from "vitest";
import { resolveCsmpTier } from "../src/config/csmp-tier.source.js";

const stages = [
  {
    order: 1,
    name: "Red",
    required: 2,
    maps: [
      { levelId: "1", alternateLevelId: "101", title: "Red 1" },
      { levelId: "2", title: "Red 2" },
    ],
  },
  {
    order: 2,
    name: "Aqua",
    required: 1,
    maps: [{ levelId: "3", title: "Aqua 1" }],
  },
  {
    order: 3,
    name: "Bronze",
    required: 1,
    maps: [{ levelId: "4", title: "Bronze 1" }],
  },
];

describe("authoritative CSMP seed lookup", () => {
  it("requires 100% and advances only through consecutive completed tiers", () => {
    expect(
      resolveCsmpTier(stages, [
        { levelId: "101", percent: 100, status: "unverified" },
        { levelId: "2", percent: 99, status: "verified" },
        { levelId: "3", percent: 100, status: "verified" },
      ]),
    ).toBe("NONE");
    expect(
      resolveCsmpTier(stages, [
        { levelId: "101", percent: 100, status: "unverified" },
        { levelId: "2", percent: 100, status: "verified" },
        { levelId: "3", percent: 100, status: "verified" },
      ]),
    ).toBe("AQUA");
  });

  it("does not count rejected records", () => {
    expect(
      resolveCsmpTier(stages, [
        { levelId: "1", percent: 100, status: "rejected" },
        { levelId: "2", percent: 100, status: "verified" },
      ]),
    ).toBe("NONE");
  });
});
