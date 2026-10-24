import {describe,it,expect} from "vitest";
import {survivalAchievementFacts,summarizeSurvivalAchievements,type SurvivalAchievementFacts} from "../src/achievement-survival.js";
import type {SurvivalMatch,SurvivalRound,SurvivalRoundPlayer} from "../src/survival-match.js";
const player=(id:string,rank:number,score:number,qualified=true):SurvivalRoundPlayer=>({participantId:id,rank,highestProgress:score,qualified,forfeited:false,assetsReadyAt:1,preparationFailure:null,enteredAt:2,
 attempts:[{id:id+score,sequence:1,startedAt:3,endedAt:8,highestProgress:score}]});
const round=(key:string,phase:SurvivalRound["phase"],ordinal:number,players:SurvivalRoundPlayer[],target:number,cutoffScore:number):SurvivalRound=>({key,phase,ordinal,players,target,cutoffScore,mapGeneration:1,map:{levelId:40000000} as SurvivalRound["map"],selectedMaps:[],preparationStartedAt:0,entryRequestedAt:1,firstEnteredAt:2,startsAt:3,endsAt:9,finishedAt:9});
function fixture():SurvivalMatch{return {id:"m",online:true,state:"FINAL_RESULT",finishedAt:10,winnerId:"own",players:[{participantId:"own",playerId:"p",kind:"HUMAN",finalRank:1},{participantId:"other",playerId:"q",kind:"HUMAN",finalRank:2}],rounds:[
 round("r1","ROUND_1",1,[player("own",1,100),player("other",2,79),player("third",3,70,false)],2,79),
 round("final","FINAL",2,[player("own",1,100),player("other",2,99,false)],1,100)]} as SurvivalMatch;}
describe("authoritative Survival achievement facts",()=>{
 it("derives normal counts and perfect/clean/cutoff rules from stored round results",()=>{const m=fixture(),f=survivalAchievementFacts(m,"p")!;
  expect(f).toMatchObject({won:true,podium:true,finalist:true,survivedRounds:2,soleRoundWins:2});
  expect(f.oneShots).toEqual(expect.arrayContaining(["survival_round_perfect","survival_clean_champion","survival_final_perfect_win"]));
  expect(f.oneShots).not.toContain("survival_cutoff_escape");
  expect(survivalAchievementFacts(m,"q")!.oneShots).toContain("survival_cutoff_escape");
 });
 it("requires an exact real cutoff rank, including tied boundary ranks",()=>{const m=fixture();m.rounds[0]!.players=[player("own",2,70),player("other",1,90),player("third",2,70),player("fourth",4,65,false)];m.rounds[0]!.target=3;m.rounds[0]!.cutoffScore=70;
  const f=survivalAchievementFacts(m,"p")!;expect(f.oneShots).toContain("survival_cutoff_escape");expect(f.oneShots).not.toContain("survival_secret_1");});
 it("requires exactly third place at qualification for Underdog, including tied third",()=>{for(const [rank,yes] of [[1,false],[2,false],[3,true],[4,false]] as const){const m=fixture();m.rounds[0]!.players[0]!.rank=rank;
  expect(survivalAchievementFacts(m,"p")!.oneShots.includes("survival_underdog")).toBe(yes);}});
 it("counts joint first for Round Ruler while Clean Champion still requires every sole first",()=>{const m=fixture();m.rounds[0]!.players[1]!.rank=1;
  const f=survivalAchievementFacts(m,"p")!;expect(f.roundWins).toBe(2);expect(f.soleRoundWins).toBe(1);expect(f.oneShots).not.toContain('survival_clean_champion');});
 it("distinguishes FINAL entry from ties in an ongoing TIEBREAK at millipercent precision",()=>{
  const m=fixture();m.rounds[1]!.players=[player('own',1,80),player('other',1,80)];
  m.rounds.push(round('dm','TIEBREAK',3,[player('own',1,99.1234),player('other',1,99.12349)],1,99123));
  expect(survivalAchievementFacts(m,'p')!.oneShots).toContain('survival_deathmatch_entry');
  expect(survivalAchievementFacts(m,'p')!.oneShots).not.toContain('survival_secret_2');
  m.rounds.push(round('dm2','TIEBREAK',4,[player('own',1,100),player('other',2,98,false)],1,100000));
  expect(survivalAchievementFacts(m,'p')!.oneShots).toContain('survival_secret_2');
  m.rounds[2]!.players[1]!.attempts[0]!.highestProgress=99.124; m.rounds[2]!.players[1]!.rank=2;
  expect(survivalAchievementFacts(m,'p')!.oneShots).not.toContain('survival_secret_2');
 });
 it('counts one played/perfect round despite many clears and rejects invalid or unentered attempts',()=>{
  const m=fixture();m.rounds[0]!.players[0]!.attempts.push({...m.rounds[0]!.players[0]!.attempts[0]!,id:'duplicate-clear'});
  expect(survivalAchievementFacts(m,'p')).toMatchObject({playedRounds:2,perfectRounds:2});
  m.rounds[0]!.players[0]!.enteredAt=null;m.rounds[1]!.players[0]!.attempts[0]!.startedAt=0;
  expect(survivalAchievementFacts(m,'p')).toMatchObject({playedRounds:0,perfectRounds:0});
 });
 it("excludes debug, cancelled, unfinished, wrong player and bot results",()=>{for(const patch of [{online:false},{state:"CANCELLED"},{state:"PLAYING"},{completionKind:"DEBUG_FORCE"},{finishedAt:null}])expect(survivalAchievementFacts({...fixture(),...patch} as SurvivalMatch,"p")).toBeNull();
  const m=fixture();m.players[0]!.kind="BOT";expect(survivalAchievementFacts(m,"p")).toBeNull();expect(survivalAchievementFacts(fixture(),"missing")).toBeNull();});
 it("accepts the same human Debug result only through an explicitly trusted server source",()=>{
  const m=fixture();m.online=false;m.completionKind="DEBUG_FORCE";
  expect(survivalAchievementFacts(m,"p")).toBeNull();
  expect(survivalAchievementFacts(m,"p",true)).toMatchObject({won:true,podium:true});
  m.state="CANCELLED";expect(survivalAchievementFacts(m,"p",true)).toBeNull();m.state="FINAL_RESULT";
  m.players[0]!.kind="BOT";expect(survivalAchievementFacts(m,"p",true)).toBeNull();
 });
 it("does not infer a perfect clear or clean champion without valid attempts and round evidence",()=>{const m=fixture();for(const r of m.rounds)r.players[0]!.attempts=[];
  expect(survivalAchievementFacts(m,"p")!.oneShots).not.toContain("survival_final_perfect_win");m.rounds=[];expect(survivalAchievementFacts(m,"p")!.oneShots).not.toContain("survival_clean_champion");});
 it("recomputes best/current streaks in completion order and deduplicates final retries",()=>{const f=survivalAchievementFacts(fixture(),"p")!;const make=(id:string,time:number,won:boolean,podium:boolean):SurvivalAchievementFacts=>({...f,matchId:id,completedAt:time,won,podium});
  const a=make("a",1,true,true),b=make("b",2,true,true),c=make("c",3,false,false),d=make("d",4,true,true);
  expect(summarizeSurvivalAchievements([d,b,c,a,a])).toMatchObject({survival_matches:4,survival_wins:3,survival_podiums:3,current_survival_win_streak:1,survival_win_streak:2,survival_podium_streak:2});});
});
