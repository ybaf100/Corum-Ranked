import "reflect-metadata";
import { plainToInstance } from "class-transformer";
import { validate } from "class-validator";
import { describe, expect, it } from "vitest";
import { AttemptEndDto } from "../src/match/match.dto.js";

const attemptEnd = (progressPercent: unknown): AttemptEndDto =>
  plainToInstance(AttemptEndDto, {
    levelId: "12345",
    attemptId: "A-1",
    clientEventId: "end-test-1",
    progressPercent,
    cleared: false,
  });

describe("AttemptEndDto progress normalization", () => {
  it.each([
    [0.019547325102880658, 0.02],
    [24.505414197332836, 24.505],
    [100, 100],
  ])("normalizes %s to the three-decimal wire contract", async (raw, normalized) => {
    const dto = attemptEnd(raw);
    expect(dto.progressPercent).toBe(normalized);
    expect(await validate(dto)).toHaveLength(0);
  });

  it.each([Number.NaN, "not-a-number", -0.001, 100.001])(
    "still rejects invalid or out-of-range input %s",
    async (raw) => {
      expect(await validate(attemptEnd(raw))).not.toHaveLength(0);
    },
  );
});
