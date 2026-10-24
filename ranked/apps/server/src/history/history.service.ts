import type { HistoryOptions } from "./history-options.js";
import { Inject, Injectable, NotFoundException } from "@nestjs/common";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { MatchService } from "../match/match.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalService } from "../survival/survival.service.js";

export type HistoryScope = "ALL" | "SOLO_RANKED" | "SURVIVAL";
export interface HistorySummary { matchId: string; finishedAt: string | null;createdAt?:string|null;inProgress?:boolean }
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

  public async history(session: RankedSessionContext, scope: HistoryScope, options:HistoryOptions={}) {
    // Keep each mode's visibility policy and the existing Solo details intact.
    // Apply filters in each SQL source before its limit, then merge deterministically.
    const [solo, survival] = await Promise.all([
      scope === "SURVIVAL" ? null : this.solo.history(session,options),
      scope === "SOLO_RANKED" ? null : this.survival.history(session.playerId,options),
    ]);
    const matches: HistoryEntry[] = [
      ...withMode(solo?.matches ?? [], "SOLO_RANKED"),
      ...withMode(survival?.matches ?? [], "SURVIVAL"),
    ];
    matches.sort((a,b)=>Number(Boolean(b.inProgress))-Number(Boolean(a.inProgress)) ||
      ((a.finishedAt??a.createdAt??'').localeCompare(b.finishedAt??b.createdAt??'')*(options.order==='OLDEST'?1:-1)) ||
      a.matchId.localeCompare(b.matchId)||a.mode.localeCompare(b.mode));
    return { scope, matches: matches.slice(0, 30), serverNow: this.clock.now().toISOString() };
  }
  public async detail(session:RankedSessionContext,mode:Exclude<HistoryScope,'ALL'>,matchId:string){
    const result=mode==='SURVIVAL'?await this.survival.history(session.playerId,{matchId}):await this.solo.history(session,{matchId});
    const match=result.matches.find(m=>m.matchId===matchId);
    if(!match)throw new NotFoundException('Match history is unavailable');
    return {match:{...match,mode},serverNow:this.clock.now().toISOString()};
  }

}
