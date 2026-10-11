import { describe, expect, it } from "vitest";
import type { RoundAttempt, RoundState } from "@corum-ranked/rules";
import { recentRoundDeaths } from "../src/match/death-feed.js";
const death = (id: string, endedAtMs: number, extra: Partial<RoundAttempt> = {}): RoundAttempt => ({
  id,side:"A",sequence:1,startEventId:"start-"+id,serverAcceptedStartAtMs:0,endEventId:id,endedAtMs,
  progressPercent:37.5,cleared:false,awardedScore:0,valid:true,invalidReason:null,nativeDeath:true,...extra,
});
describe("bounded recent accepted native deaths",()=>{
  it("excludes Clears, quit/practice, invalidated attempts, open runs, stale deaths and future timestamps",()=>{
    const state={attempts:{A:[death("old",4999),death("quit",9500,{nativeDeath:false}),death("clear",9500,{cleared:true}),
      death("invalid",9500,{valid:false}),death("active",9500,{endedAtMs:null}),death("future",10001)],
      B:[death("real",9500,{side:"B"})]}} as RoundState;
    expect(recentRoundDeaths(state,10000)).toEqual([{id:"real",side:"B",progressPercent:37.5,endedAtMs:9500}]);
  });
  it("returns only the latest three in chronological display order",()=>{
    const state={attempts:{A:[death("1",6000),death("3",8000)],B:[death("2",7000,{side:"B"}),death("4",9000,{side:"B"})]}} as RoundState;
    expect(recentRoundDeaths(state,10000).map(event=>event.id)).toEqual(["2","3","4"]);
    expect(recentRoundDeaths(state,14000)).toEqual([]);
  });
});
