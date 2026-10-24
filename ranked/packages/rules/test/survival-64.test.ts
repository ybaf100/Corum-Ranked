import {describe,it,expect} from "vitest";
import {DEFAULT_SURVIVAL_CONFIG,survivalConfigForPlayerCount,validateSurvivalConfig} from "../src/survival.js";
import {createSurvivalMatch,reduceSurvivalMatch,survivalCurrentRound,survivalLiveView,survivalSettlementInputs,type SurvivalCommand,type SurvivalMatch} from "../src/survival-match.js";
import {survivalMapRange,validateSurvivalEngineConfig,type SurvivalVerifiedMap} from "../src/survival-maps.js";
import {survivalOnlineTargets,survivalOnlineBotLp} from "../src/survival-online.js";
import {survivalDebugLp,forceSurvivalDebugRank} from "../src/survival-debug.js";
import {survivalAchievementFacts,summarizeSurvivalAchievements} from "../src/achievement-survival.js";

class Field {
 now=Date.parse("2026-10-24T12:00:00Z");mapId=40_000_000;match:SurvivalMatch;
 constructor(count:16|64=64){
  this.match=createSurvivalMatch("field",Array.from({length:count},(_,i)=>({participantId:`p${i+1}`,playerId:i===0?"account":null,
   name:`Player ${i+1}`,kind:i===0?"HUMAN" as const:"BOT" as const,lp:150})),survivalConfigForPlayerCount(DEFAULT_SURVIVAL_CONFIG,count),"format",this.now);
 }
 get round(){return survivalCurrentRound(this.match);}
 get context(){return {roundKey:this.round.key,mapGeneration:this.round.mapGeneration,levelId:this.round.map!.levelId};}
 send(command:SurvivalCommand){this.match=reduceSurvivalMatch(this.match,command,this.now);}
 prepare(){
  const range=survivalMapRange(this.match.difficultyGroup,this.round.phase);
  const map:SurvivalVerifiedMap={levelId:this.mapId++,stars:range.minimumStars??10,demon:range.minimumStars===null,
   demonDifficulty:range.minimumStars===null?range.demons[0]!:null,automatic:false,platformer:false,levelVersion:1,
   geometrySha256:"a".repeat(64),geometryBytes:100,verifiedAt:new Date(this.now).toISOString()};
  this.send({type:"MAP_SELECTED",roundKey:this.round.key,mapGeneration:1,map});
  for(const p of this.round.players)this.send({type:"ASSETS_READY",...this.context,participantId:p.participantId,certificate:map});
  this.now+=5000;for(const p of this.round.players)this.send({type:"ENTERED",...this.context,participantId:p.participantId});
  this.now=this.round.startsAt!;this.send({type:"TICK"});
 }
 attempt(id:string,progress:number,attemptId="a1"){
  this.send({type:"ATTEMPT_START",...this.context,participantId:id,attemptId});
  this.send({type:"ATTEMPT_END",...this.context,participantId:id,attemptId,progress});
 }
 play(scores?:number[]){this.prepare();for(const [i,p] of this.round.players.entries())this.attempt(p.participantId,scores?.[i]??100-i);
  this.now=this.round.endsAt!;this.send({type:"TICK"});}
 next(){this.send({type:"TICK"});this.match=JSON.parse(JSON.stringify(this.match)) as SurvivalMatch;}
}

describe("shared Survival formats",()=>{
 it("plays the exact 64 -> 40 -> 26 -> 12 -> 5 -> 1 schedule across durable round restores",()=>{
  const field=new Field();
  for(const [index,size] of [64,40,26,12,5].entries()){
   expect(field.round.players).toHaveLength(size);expect(field.round.phase).toBe(index===4?"FINAL":`ROUND_${index+1}`);
   expect(field.round.target).toBe([40,26,12,5,1][index]);field.play();if(index<4)field.next();
  }
  expect(field.match.state).toBe("FINAL_RESULT");expect(field.match.winnerId).toBe("p1");expect(field.match.rounds).toHaveLength(5);
  const rewards=survivalSettlementInputs(field.match);expect(rewards).toHaveLength(64);
  expect(rewards.find(p=>p.rank===1)?.delta).toBe(50);expect(rewards.find(p=>p.rank===64)?.delta).toBe(-55);
  expect(survivalLiveView(field.match,field.now,"p1")).toMatchObject({matchPlayerCount:64,playerCount:64,roundNumber:5});
  const facts=survivalAchievementFacts(field.match,"account",true)!;
  expect(facts).toMatchObject({won:true,playedRounds:5,perfectRounds:5,survivedRounds:5});
  expect(facts.oneShots).toContain("survival_clean_champion");
  expect(summarizeSurvivalAchievements([facts]).survival_rounds_played).toBe(5);
 });
 it("preserves the original 16 -> 10 -> 6 -> 3 -> 1 format without an R4",()=>{
  const field=new Field(16);
  for(const [index,size] of [16,10,6,3].entries()){
   expect(field.round.players).toHaveLength(size);expect(field.round.phase).toBe(index===3?"FINAL":`ROUND_${index+1}`);
   field.play();if(index<3)field.next();
  }
  expect(field.match.state).toBe("FINAL_RESULT");expect(field.match.rounds).toHaveLength(4);
 });
 it("advances every tied cutoff player, including a field larger than the 40-person target",()=>{
  const field=new Field();const scores=Array.from({length:64},(_,i)=>i>=39&&i<=41?61:100-i);
  field.play(scores);expect(field.round.target).toBe(40);expect(field.round.cutoffScore).toBe(61);
  expect(field.round.players.filter(p=>p.qualified)).toHaveLength(42);field.next();
  expect(field.round.phase).toBe("ROUND_2");expect(field.round.target).toBe(26);expect(field.round.players).toHaveLength(42);
 });
 it("keeps the last-ten-second checkpoint in R4 and enters a three-decimal final tiebreak",()=>{
  const field=new Field();for(let i=0;i<3;i++){field.play();field.next();}
  expect(field.round.phase).toBe("ROUND_4");field.prepare();
  for(const [i,p] of field.round.players.entries())field.attempt(p.participantId,100-i);
  field.now=field.round.endsAt!-10000;field.send({type:"TICK"});
  expect(field.round.achievementCheckpoint).toMatchObject({status:"CAPTURED",at:field.now});
  field.now=field.round.endsAt!;field.send({type:"TICK"});field.next();
  field.play([100,100,98,97,96]);field.next();expect(field.round.phase).toBe("TIEBREAK");field.prepare();
  for(const p of [...field.round.players])for(let i=0;i<3;i++)field.attempt(p.participantId,p.participantId==="p1"?99.123:99.122,`dm${i}`);
  expect(field.match.state).toBe("FINAL_RESULT");expect(field.match.winnerId).toBe("p1");
 });
 it("retains an actual final for short 64-format fields",()=>{
  for(const count of [2,4,8,32]){
   const field=new Field();field.match=createSurvivalMatch("short",field.match.players.slice(0,count),field.match.config,"short",field.now,false,true);
   for(let stage=0;stage<6 && field.match.state!=="FINAL_RESULT";stage++){field.play();if(field.match.state==="ROUND_RESULT")field.next();}
   expect(field.match.state).toBe("FINAL_RESULT");expect(field.match.rounds.at(-1)?.phase).toBe("FINAL");
  }
 });
 it("uses the configured field for bot fills, scaled short fields and explicit debug ranks",()=>{
  const config=survivalConfigForPlayerCount(DEFAULT_SURVIVAL_CONFIG,64);
  expect(validateSurvivalConfig(config)).toEqual([]);expect(validateSurvivalEngineConfig(config)).toEqual([]);
  expect(survivalOnlineTargets(64,64)).toEqual([40,26,12,5]);expect(survivalOnlineTargets(32,64)).toEqual([20,13,6,3]);
  expect(survivalDebugLp(7,150,"RED_BRONZE",config).bots).toHaveLength(63);
  expect(survivalOnlineBotLp(7,[150,150],config)).toHaveLength(62);
  const field=new Field();field.match=forceSurvivalDebugRank(field.match,"p1",64,field.now);
  expect(survivalSettlementInputs(field.match).find(p=>p.participantId==="p1")).toMatchObject({rank:64,delta:-55});
  expect(validateSurvivalEngineConfig({...config,round1Qualifiers:32})).not.toEqual([]);
  expect(()=>survivalOnlineTargets(65,64)).toThrow();expect(()=>survivalDebugLp(1,150,"RED_BRONZE",config,64)).toThrow();
 });
});
