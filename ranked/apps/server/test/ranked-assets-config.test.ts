import { describe, expect, it } from "vitest";
import { validateRankedConfigDocument } from "../src/config/ranked-config.validator.js";
import { configDocumentFixture } from "./fixtures.js";
import type { RankedClientAssetPackConfig } from "../src/config/ranked-config.document.js";

const config = (assets?: RankedClientAssetPackConfig) => ({
  ...configDocumentFixture(),
  client: {
    audio: { enabled: false, fadeInSeconds: 0, fadeOutSeconds: 0, resources: [] },
    ui: { fadeInSeconds: 0.24, fadeOutSeconds: 0.18 },
    ...(assets ? { assets } : {}),
  },
});
const valid = { enabled: true, assetPackVersion: "visuals-v1", sha256: "a".repeat(64), sizeBytes: 24957530 };

describe("server-selected external image pack", () => {
  it("accepts old configs without assets and disabled rollouts", () => {
    expect(validateRankedConfigDocument(config())).toEqual([]);
    expect(validateRankedConfigDocument(config({ enabled: false, assetPackVersion: "", sha256: "", sizeBytes: 0 }))).toEqual([]);
  });
  it("accepts an exact immutable version with its complete ZIP identity", () => {
    expect(validateRankedConfigDocument(config(valid))).toEqual([]);
  });
  it.each(["../other", ".", "..", "a/b", "한글", "x".repeat(65)])("rejects unsafe tag %s", (assetPackVersion) => {
    expect(validateRankedConfigDocument(config({ ...valid, assetPackVersion })).some(e => e.includes("assetPackVersion"))).toBe(true);
  });
  it.each([0, -1, 2.5, 67108865, Number.NaN])("rejects invalid ZIP size %s", (sizeBytes) => {
    expect(validateRankedConfigDocument(config({ ...valid, sizeBytes })).some(e => e.includes("sizeBytes"))).toBe(true);
  });
  it("rejects a missing digest", () => {
    expect(validateRankedConfigDocument(config({ ...valid, sha256: "wrong" })).some(e => e.includes("sha256"))).toBe(true);
  });
});
