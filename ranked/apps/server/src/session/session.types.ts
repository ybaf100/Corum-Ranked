import type { DisplayTier } from "@corum-ranked/rules";
import type { Request } from "express";

export interface RankedSessionContext {
  readonly sessionId: string;
  readonly playerId: string;
  readonly gdAccountId: string;
  readonly gdUsername: string;
  readonly displayedTier: DisplayTier;
  readonly hiddenMmr: number;
  readonly placementGames: number;
}

export interface RankedRequest extends Request {
  rankedSession?: RankedSessionContext;
}
