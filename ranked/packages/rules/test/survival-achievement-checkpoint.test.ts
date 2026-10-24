import { describe, expect, it } from "vitest";
import { observeSurvivalAchievementCheckpoint, observeSurvivalAchievementRecovery } from "../src/survival-achievement-checkpoint.js";
import { reduceSurvivalMatch, survivalCurrentRound, type SurvivalMatch, type SurvivalRoundPlayer } from "../src/survival-match.js";
import { survivalAchievementFacts } from "../src/achievement-survival.js";
function fixture(): SurvivalMatch {
  const p = (id:string, score:number):SurvivalRoundPlayer => ({ participantId:id,assetsReadyAt:0,enteredAt:0,preparationFailure:null,forfeited:false,qualified:false,rank:null,highestProgress:score,
    attempts:[{id,sequence:1,startedAt:0,endedAt:null,highestProgress:score}] });
  return {id:'m',online:true,revision:1,state:'PLAYING',createdAt:0,updatedAt:169_000,finishedAt:null,winnerId:null,
    players:[{participantId:'own',playerId:'p',kind:'HUMAN',eliminationStage:null,finalRank:null},{participantId:'other',playerId:'q',kind:'HUMAN',eliminationStage:null,finalRank:null},{participantId:'third',playerId:'r',kind:'HUMAN',eliminationStage:null,finalRank:null}],
    transitions:[],rounds:[{key:'stage-1',ordinal:1,phase:'ROUND_3',target:2,mapGeneration:1,map:{levelId:40_000_000},selectedMaps:[],startsAt:0,endsAt:180_000,finishedAt:null,cutoffScore:null,
      players:[p('own',50),p('other',80),p('third',70)]}]} as unknown as SurvivalMatch;
}
const progress = (m:SurvivalMatch,percent:number,at:number) => reduceSurvivalMatch(m,{type:'PROGRESS',roundKey:'stage-1',mapGeneration:1,levelId:40_000_000,participantId:'own',attemptId:'own',progress:percent},at);
function finalFacts(m:SurvivalMatch) {
  m=reduceSurvivalMatch(m,{type:'TICK'},180_000);m.state='FINAL_RESULT';m.finishedAt=180_001;m.players[0]!.finalRank=2;
  return survivalAchievementFacts(m,'p')!;
}
describe('server-authoritative last-ten-second observation',()=>{
 it('captures once before a boundary update and records a real recovery including cutoff ties',()=>{
  let m=fixture();expect(observeSurvivalAchievementCheckpoint(m,169_999)).toBe(m);
  m=progress(m,70,170_000);const checkpoint=survivalCurrentRound(m).achievementCheckpoint!;
  expect(checkpoint).toMatchObject({status:'CAPTURED',at:170_000,observedAt:170_000,cutoffScore:70,standings:[{participantId:'other',atRisk:false},{participantId:'third',atRisk:false},{participantId:'own',atRisk:true,rescuedAt:170_000}]});
  expect(finalFacts(m).oneShots).toContain('survival_secret_1');
 });
 it('does not label a player tied at the cutoff as at risk',()=>{
  const m=fixture();survivalCurrentRound(m).players[0]!.highestProgress=70;
  const observed=observeSurvivalAchievementCheckpoint(m,170_010);
  expect(survivalCurrentRound(observed).achievementCheckpoint!.standings.find(p=>p.participantId==='own')?.atRisk).toBe(false);
  expect(finalFacts(observed).oneShots).not.toContain('survival_secret_1');
 });
 it('records MISSED on a delayed poll/restart and cannot reconstruct from final attempts',()=>{
  for(const now of [170_251,175_000,180_000]) {
   const m=observeSurvivalAchievementCheckpoint(fixture(),now);
   expect(survivalCurrentRound(m).achievementCheckpoint).toMatchObject({status:'MISSED',standings:[]});
  }
  expect(finalFacts(progress(fixture(),90,175_000)).oneShots).not.toContain('survival_secret_1');
 });
 it('requires an unchanged pre-boundary snapshot, even inside the scheduling window',()=>{
  const m=fixture();m.updatedAt=170_001;
  expect(survivalCurrentRound(observeSurvivalAchievementCheckpoint(m,170_010)).achievementCheckpoint?.status).toBe('MISSED');
 });
 it('persists evidence across reconnect/restart and deduplicates repeated boundary/progress events',()=>{
  let m=observeSurvivalAchievementCheckpoint(fixture(),170_000);
  const checkpoint=structuredClone(survivalCurrentRound(m).achievementCheckpoint);
  expect(observeSurvivalAchievementCheckpoint(m,170_001)).toBe(m);
  m=JSON.parse(JSON.stringify(m));m=progress(m,75,179_999);m=progress(m,75,179_999);
  expect(survivalCurrentRound(m).achievementCheckpoint?.at).toBe(checkpoint?.at);
  expect(survivalCurrentRound(m).achievementCheckpoint?.standings.find(p=>p.atRisk)?.rescuedAt).toBe(179_999);
  expect(finalFacts(m).oneShots.filter(id=>id==='survival_secret_1')).toHaveLength(1);
 });
 it('rejects late updates, transient rescues that ultimately fail, and finals',()=>{
  let m=observeSurvivalAchievementCheckpoint(fixture(),170_000);
  expect(finalFacts(progress(m,99,180_000)).oneShots).not.toContain('survival_secret_1');
  m=progress(m,75,175_000);survivalCurrentRound(m).players.find(p=>p.participantId==='third')!.highestProgress=90;
  expect(finalFacts(m).oneShots).not.toContain('survival_secret_1');
  const f=fixture();survivalCurrentRound(f).phase='FINAL';expect(observeSurvivalAchievementCheckpoint(f,170_000)).toBe(f);
 });
 it('applies accepted same-timestamp updates in server order without duplicate evidence',()=>{
  let m=observeSurvivalAchievementCheckpoint(fixture(),170_000);
  m=progress(m,70,170_000);observeSurvivalAchievementRecovery(m,170_000);
  m=reduceSurvivalMatch(m,{type:'PROGRESS',roundKey:'stage-1',mapGeneration:1,levelId:40_000_000,participantId:'third',attemptId:'third',progress:71},170_000);
  expect(finalFacts(m).oneShots).not.toContain('survival_secret_1');
 });
});
