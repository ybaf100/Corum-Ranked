import { RankedDomainError } from "./errors.js";

export interface RandomSource {
  next(): number;
}

export class SeededRandom implements RandomSource {
  private state: number;

  public constructor(seed: number) {
    if (!Number.isInteger(seed)) {
      throw new RankedDomainError("INVALID_CONFIG", "RNG seed must be an integer", { seed });
    }
    this.state = (seed >>> 0) || 0x6d2b79f5;
  }

  public next(): number {
    let value = this.state;
    value ^= value << 13;
    value ^= value >>> 17;
    value ^= value << 5;
    this.state = value >>> 0;
    return this.state / 0x1_0000_0000;
  }
}

export const shuffle = <T>(values: readonly T[], random: RandomSource): T[] => {
  const result = [...values];
  for (let index = result.length - 1; index > 0; index -= 1) {
    const swapIndex = Math.floor(random.next() * (index + 1));
    const current = result[index];
    const swap = result[swapIndex];
    if (current === undefined || swap === undefined) {
      throw new RankedDomainError("INVALID_CONFIG", "Random source returned an invalid value");
    }
    result[index] = swap;
    result[swapIndex] = current;
  }
  return result;
};

export const sample = <T>(values: readonly T[], count: number, random: RandomSource): T[] => {
  if (!Number.isInteger(count) || count < 0 || count > values.length) {
    throw new RankedDomainError("INVALID_CONFIG", "Invalid sample size", {
      count,
      available: values.length,
    });
  }
  return shuffle(values, random).slice(0, count);
};
