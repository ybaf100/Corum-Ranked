import { RANKED_TIERS, SOLO_DIVISION_TIERS, type BaseRankedTier, type DisplayTier, type RankedTier, type TierBand } from "./types.js";

/** Survival keeps five independent display tiers; only Solo has numbered divisions. */
export function baseRankedTier(tier: string): BaseRankedTier | null {
  const value = tier.trim().toUpperCase();
  if (value === "MASTER" || value === "CHAMPION") return "GOLD";
  const base = value.replace(/[123]$/, "");
  return (RANKED_TIERS as readonly string[]).includes(base) ? base as BaseRankedTier : null;
}

export function soloTierOrder(tier: string): number {
  if (tier === "MASTER") return 13;
  if (tier === "CHAMPION") return 14;
  const index = (SOLO_DIVISION_TIERS as readonly string[]).indexOf(tier);
  if (index >= 0) return index;
  const base = (RANKED_TIERS as readonly string[]).indexOf(tier);
  return base < 0 ? -1 : base * 3;
}

/** Existing five-row sheets remain readable while operators deploy the new script.
 * Explicit division rows are never recalculated or overwritten. */
export function expandSoloTierBands(bands: readonly TierBand[]): readonly TierBand[] {
  if (bands.length !== 5 || !RANKED_TIERS.every(tier => bands.some(b => b.tier === tier))) return bands;
  return RANKED_TIERS.flatMap(tier => {
    const band = bands.find(b => b.tier === tier)!;
    if (tier === "GOLD" || band.maxExclusive === null) return [band];
    const cuts = [band.minInclusive, Math.ceil(band.minInclusive + (band.maxExclusive - band.minInclusive) / 3),
      Math.ceil(band.minInclusive + 2 * (band.maxExclusive - band.minInclusive) / 3), band.maxExclusive];
    return [3, 2, 1].map((division, index) => ({ ...band, tier: `${tier}${division}` as RankedTier,
      minInclusive: cuts[index]!, maxExclusive: cuts[index + 1]!,
      promotionGames: division === 1 ? band.promotionGames : 0,
      promotionWinsRequired: division === 1 ? band.promotionWinsRequired : 0 }));
  });
}

export function soloBaseTierBands(bands: readonly TierBand[]): readonly TierBand[] {
  return RANKED_TIERS.flatMap(tier => {
    const group = bands.filter(b => baseRankedTier(b.tier) === tier).sort((a,b) => a.minInclusive - b.minInclusive);
    const first = group[0], last = group.at(-1);
    return first && last ? [{ ...first, tier, maxExclusive: last.maxExclusive,
      promotionGames: last.promotionGames, promotionWinsRequired: last.promotionWinsRequired }] : [];
  });
}

/** Exact slots: three Champions, then the upper half, with the middle player in
 * the upper half for odd populations. Ties are ordered by the existing ladder. */
export function goldTierSlots(total: number) {
  const champion = Math.min(3, Math.max(0, total));
  const masterEnd = total > 6 ? Math.ceil(total / 2) : champion;
  return { champion, master: masterEnd - champion, gold: Math.max(0, total - masterEnd), masterEnd };
}

export function goldTierForPosition(position: number, total: number): DisplayTier {
  const slots = goldTierSlots(total);
  return position <= slots.champion ? "CHAMPION" : position <= slots.masterEnd ? "MASTER" : "GOLD";
}

export function rankedTierLabel(tier: string): string {
  return tier.replace(/([123])$/, (_, digit: string) => ` ${digit === "3" ? "III" : digit === "2" ? "II" : "I"}`);
}
