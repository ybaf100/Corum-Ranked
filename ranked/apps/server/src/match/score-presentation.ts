import type { PlayerSide, PromotionSeriesState, RankedRatingProfile, TierBand } from "@corum-ranked/rules";
import type { SqlExecutor } from "../database/database.port.js";
import { readStandings, unrankedStanding, type StandingOverride } from "../leaderboard/standing.js";

interface ScoreSubject {
  readonly playerId: string;
  readonly before: RankedRatingProfile;
  readonly after: RankedRatingProfile;
  readonly beforeWins?:number;
  readonly beforeLosses?:number;
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
  for (const [side,subject] of Object.entries(subjects)) {
    const stats=subject.beforeWins!==undefined && subject.beforeLosses!==undefined?
      {wins:subject.beforeWins,losses:subject.beforeLosses}:{};
    const afterStats=subject.beforeWins!==undefined && subject.beforeLosses!==undefined?
      {wins:subject.beforeWins+(winner===side?1:0),losses:subject.beforeLosses+(winner!==side?1:0)}:{};
    overrides.push({ phase: "before", playerId: subject.playerId, profile: subject.before,...stats },
      { phase: "after", playerId: subject.playerId, profile: subject.after,...afterStats });
  }
  // A single statement projects both participants' before/after profiles over
  // the same ladder snapshot. A concurrent match cannot change only one phase.
  const standings = await readStandings(database, Object.values(subjects).map((p) => p.playerId),
    ["before", "after"], overrides, undefined, true);
  const player = (side: PlayerSide) => {
    const subject = subjects[side];
    const {goldTimeline,...before} = standings.get("before")?.get(subject.playerId) ?? unrankedStanding();
    return {
      before,
      after: standings.get("after")?.get(subject.playerId) ?? unrankedStanding(),
      promotion: resultPromotionSeries(subject.before, subject.after, side, winner),
      goldTimeline:goldTimeline ?? null,
    };
  };
  return { version: 1,
    tierBands: [...bands].sort((a, b) => a.minInclusive - b.minInclusive)
      .map(({ tier, minInclusive }) => ({ tier, minInclusive })),
    players: { A: player("A"), B: player("B") } };
}
