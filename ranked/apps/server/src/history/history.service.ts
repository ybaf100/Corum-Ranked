import { Inject, Injectable } from "@nestjs/common";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { MatchService } from "../match/match.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalService } from "../survival/survival.service.js";

export type HistoryScope = "ALL" | "SOLO_RANKED" | "SURVIVAL";
export interface HistorySummary { matchId: string; finishedAt: string | null }
export interface HistoryEntry extends HistorySummary { mode: Exclude<HistoryScope, "ALL"> }

function withMode<T extends HistorySummary>(matches: readonly T[], mode: HistoryEntry["mode"]) {
  return matches.map((match) => ({ ...match, mode }));
}

@Injectable()
export class HistoryService {
  public constructor(
    private readonly solo: MatchService,
    private readonly survival: SurvivalService,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
  ) {}

  public async history(session: RankedSessionContext, scope: HistoryScope) {
    // Keep each mode's visibility policy and the existing Solo details intact.
    // Survival M1 returns no matches; its LP audit is not a match history.
    const [solo, survival] = await Promise.all([
      scope === "SURVIVAL" ? null : this.solo.history(session),
      scope === "SOLO_RANKED" ? null : this.survival.history(session.playerId),
    ]);
    const matches: HistoryEntry[] = [
      ...withMode(solo?.matches ?? [], "SOLO_RANKED"),
      ...withMode(survival?.matches ?? [], "SURVIVAL"),
    ];
    matches.sort((a, b) => (b.finishedAt ?? "").localeCompare(a.finishedAt ?? "") ||
      a.matchId.localeCompare(b.matchId) || a.mode.localeCompare(b.mode));
    return { scope, matches: matches.slice(0, 30), serverNow: this.clock.now().toISOString() };
  }
}
