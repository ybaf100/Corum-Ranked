import { describe, expect, it } from "vitest";
import { createRound, startRoundAttempt, endRoundAttempt, advanceRoundClock, DOCUMENT_RULES_V0_4, type RankedMapSnapshot } from "@corum-ranked/rules";
import { standardSpectatorEligible } from "../src/match/spectator-live.policy.js";
const map: RankedMapSnapshot = { levelId:"1",canonicalLevelId:"1",alternateLevelId:null,playableLevelId:"1",title:"Map",creator:"Creator",difficulty:"5",pool:3,qualifyingPercent:60 };
const fresh=()=>createRound(1,map,DOCUMENT_RULES_V0_4,1000);
describe("canonical standard spectator authority",()=>{
 it("denies an active viewer even without progress telemetry, then grants only after End commits",()=>{
  let s=fresh();const a=startRoundAttempt(s,"A",1001,"a");s=a.state;
  const b=startRoundAttempt(s,"B",1001,"b");s=advanceRoundClock(b.state,b.state.finalWindowEndAtMs+1);
  expect(s.phase).toBe("ROUND_SETTLING");expect(standardSpectatorEligible(s,"A")).toBe(false);
  const ended=endRoundAttempt(s,"A",a.attemptId!,s.lastEvaluatedAtMs+1,20,false,"end-a");s=ended.state;
  expect(ended.accepted).toBe(true);expect(standardSpectatorEligible(s,"A")).toBe(true);
  expect(standardSpectatorEligible(s,"B")).toBe(false);
  const duplicate=endRoundAttempt(s,"A",a.attemptId!,s.lastEvaluatedAtMs+1,20,false,"end-a");
  expect(duplicate.duplicate).toBe(true);expect(standardSpectatorEligible(duplicate.state,"A")).toBe(true);
  s=endRoundAttempt(s,"B",b.attemptId!,s.lastEvaluatedAtMs+1,30,false,"end-b").state;
  expect(standardSpectatorEligible(s,"A")).toBe(false);
 });
 it("grants the three-clear trigger side while the opponent retains an Attempt budget",()=>{
  let s=fresh();
  for(let i=0;i<3;i++){
   const started=startRoundAttempt(s,"A",1100+i*1000,"start-"+i);expect(started.accepted).toBe(true);
   s=endRoundAttempt(started.state,"A",started.attemptId!,1200+i*1000,100,true,"end-"+i).state;
   expect(standardSpectatorEligible(s,"A")).toBe(i===2);
  }
  expect(standardSpectatorEligible(s,"B")).toBe(false);
  s.clears.A=2;expect(standardSpectatorEligible(s,"A")).toBe(false);
 });
 it("never grants from a finished label or closed/mismatched round state",()=>{
  const s=fresh();expect(standardSpectatorEligible(s,"A")).toBe(false);
  for(const phase of ["ROUND_RESULT","ROUND_PLAYING","FINAL_ATTEMPT_WINDOW"] as const){s.phase=phase;expect(standardSpectatorEligible(s,"A")).toBe(false);}
 });
});
