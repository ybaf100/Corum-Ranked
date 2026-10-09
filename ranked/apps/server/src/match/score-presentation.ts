import type { PlayerSide, PromotionSeriesState, RankedRatingProfile, TierBand } from "@corum-ranked/rules";
import type { SqlExecutor } from "../database/database.port.js";
import { readStandings, unrankedStanding, type StandingOverride } from "../leaderboard/standing.js";

interface ScoreSubject {
  readonly playerId: string;
  readonly before: RankedRatingProfile;
  readonly after: RankedRatingProfile;
}

/** Ended series disappear from profileAfter; retain the final mark for the UI. */
export function resultPromotionSeries(
  before: RankedRatingProfile, after: RankedRatingProfile, side: PlayerSide, winner: PlayerSide,
): PromotionSeriesState | null {
  if (after.promotion) return { ...after.promotion, results: [...after.promotion.results] };
  if (!before.promotion) return null;
  return { ...before.promotion, results: [...before.promotion.results, winner === side ? "WIN" : "LOSS"] };
}

export async function buildScorePresentation(
  database: SqlExecutor, bands: readonly TierBand[],
  subjects: Readonly<Record<PlayerSide, ScoreSubject>>, winner: PlayerSide,
) {
  const overrides: StandingOverride[] = [];
  for (const subject of Object.values(subjects)) {
    overrides.push({ phase: "before", playerId: subject.playerId, profile: subject.before },
      { phase: "after", playerId: subject.playerId, profile: subject.after });
  }
  // A single statement projects both participants' before/after profiles over
  // the same ladder snapshot. A concurrent match cannot change only one phase.
  const standings = await readStandings(database, Object.values(subjects).map((p) => p.playerId),
    ["before", "after"], overrides);
  const player = (side: PlayerSide) => {
    const subject = subjects[side];
    return {
      before: standings.get("before")?.get(subject.playerId) ?? unrankedStanding(),
      after: standings.get("after")?.get(subject.playerId) ?? unrankedStanding(),
      promotion: resultPromotionSeries(subject.before, subject.after, side, winner),
    };
  };
  return { version: 1,
    tierBands: [...bands].sort((a, b) => a.minInclusive - b.minInclusive)
      .map(({ tier, minInclusive }) => ({ tier, minInclusive })),
    players: { A: player("A"), B: player("B") } };
}
