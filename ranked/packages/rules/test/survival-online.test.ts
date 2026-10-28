import { describe,it,expect } from "vitest";
import { createSurvivalMatch,reduceSurvivalMatch,survivalCurrentRound,survivalSettlementInputs,survivalOnlineTargets,
  survivalOnlineRewardRank,survivalOnlineBotLp,survivalMarkAbsent,DEFAULT_SURVIVAL_CONFIG,survivalMapRange,
  survivalDifficultyForAverage,type SurvivalMatch,type SurvivalCommand } from "../src/index.js";
const config={...DEFAULT_SURVIVAL_CONFIG,enabled:true};
const entrants=(n:number)=>Array.from({length:n},(_,i)=>({participantId:`p${i}`,playerId:`a${i}`,kind:"HUMAN" as const,name:`Player ${i}`,lp:150}));
const make=(n=16)=>createSurvivalMatch("test",entrants(n),config,"g",0,true);
const context=(m:SurvivalMatch)=>({roundKey:survivalCurrentRound(m).key,mapGeneration:survivalCurrentRound(m).mapGeneration,levelId:survivalCurrentRound(m).map?.levelId??0});
function command(m:SurvivalMatch,c:SurvivalCommand,time:number){return reduceSurvivalMatch(m,c,time);}
function map(m:SurvivalMatch,time:number){const r=survivalCurrentRound(m),range=survivalMapRange(m.difficultyGroup,r.phase);
 return command(m,{type:"MAP_SELECTED",...context(m),map:{levelId:40000000+r.ordinal+r.mapGeneration*100,levelVersion:1,stars:range.minimumStars??10,demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?(range.demons[0]??null):null,
 automatic:false,platformer:false,geometryBytes:1,geometrySha256:"a".repeat(64),verifiedAt:new Date(time).toISOString()}},time);}
function ready(m:SurvivalMatch,time:number){m=map(m,time);const r=survivalCurrentRound(m);
 for(const p of r.players)m=command(m,{type:"ASSETS_READY",...context(m),participantId:p.participantId,certificate:r.map!},time+1);
 for(const p of r.players)m=command(m,{type:"ENTERED",...context(m),participantId:p.participantId},time+5001);
 return command(m,{type:"TICK"},survivalCurrentRound(m).startsAt!);}
function record(m:SurvivalMatch,id:string,value:number,time:number){m=command(m,{type:"ATTEMPT_START",...context(m),participantId:id,attemptId:id+"-attempt"},time);
 return command(m,{type:"PROGRESS",...context(m),participantId:id,attemptId:id+"-attempt",progress:value},time);}

describe("Survival M5 confirmed online policies",()=>{
 it.each([8,9,10,11,12,13,14,15,16])("captures proportional fixed targets for %i humans",n=>{
   const m=make(n);expect(survivalCurrentRound(m).target).toBe(Math.round(n*10/16));
   expect(survivalOnlineTargets(n)).toEqual([Math.round(n*10/16),Math.round(n*6/16),Math.round(n*3/16)]);
 });
 it.each([2,3,4,5,6,7])("uses the agreed short-match phases for %i humans",n=>{
   let m=make(n);expect(survivalCurrentRound(m).phase).toBe(n<=3?"FINAL":"ROUND_3");
   m=ready(m,1);expect(survivalCurrentRound(m).endsAt!-survivalCurrentRound(m).startsAt!).toBe(180000);
   survivalCurrentRound(m).players.forEach((p,i)=>{m=record(m,p.participantId,100-i,survivalCurrentRound(m).startsAt!+1);});
   m=command(m,{type:"TICK"},survivalCurrentRound(m).endsAt!);
   if(n>3){expect(survivalCurrentRound(m).players.filter(p=>p.qualified)).toHaveLength(3);m=command(m,{type:"TICK"},m.updatedAt+1);expect(survivalCurrentRound(m).phase).toBe("FINAL");}
   else expect(m.state).toBe("FINAL_RESULT");
 });
 it("keeps legacy M3/M4 creation restricted to 16",()=>{expect(()=>createSurvivalMatch("old",entrants(8),config,"g",0)).toThrow();});
 it("maps 8-person rewards including ties to the agreed 16-position table",()=>{
   expect(Array.from({length:8},(_,i)=>config.placementLp[survivalOnlineRewardRank(i+1,8)-1])).toEqual([50,30,20,10,2,-20,-40,-55]);
   let m=ready(make(8),1);survivalCurrentRound(m).players.forEach((p,i)=>{m=record(m,p.participantId,i===7?0:50,survivalCurrentRound(m).startsAt!+1);});
   m=command(m,{type:"TICK"},survivalCurrentRound(m).endsAt!);
   const input=survivalSettlementInputs(m).find(p=>p.participantId==="p7")!;expect(input).toMatchObject({rank:8,occupiedLastRank:8,delta:-55});
 });
 it("pays each explicit surrender the immutable round floor immediately",()=>{
   let m=make();m=command(m,{type:"FORFEIT",...context(m),participantIds:["p0"],reason:"ONLINE_SURRENDER"},1);
   expect(survivalSettlementInputs(m)[0]).toMatchObject({rank:16,tiedPlayers:1,occupiedLastRank:16,delta:-55});
   m=command(m,{type:"FORFEIT",...context(m),participantIds:["p1"],reason:"ONLINE_SURRENDER"},2);
   expect(survivalSettlementInputs(m).map(p=>[p.rank,p.occupiedLastRank,p.delta])).toEqual([[16,16,-55],[16,16,-55]]);
 });
 it("settles normal eliminations as soon as the round closes and preserves later stages",()=>{
   let m=ready(make(),1);const start=survivalCurrentRound(m).startsAt!;
   for(let i=0;i<16;i++)m=record(m,`p${i}`,100-i,start+1);
   expect(survivalSettlementInputs(m)).toEqual([]);m=command(m,{type:"TICK"},survivalCurrentRound(m).endsAt!);
   expect(survivalSettlementInputs(m).map(p=>p.rank)).toEqual([11,12,13,14,15,16]);
   const first=survivalSettlementInputs(m);m=command(m,{type:"TICK"},m.updatedAt+1);expect(survivalSettlementInputs(m)).toEqual(first);
 });
 it("counts every absent entrant as zero without faking native evidence, permits mid-round recovery",()=>{
   let m=map(make(8),1);m=survivalMarkAbsent(m,["p0"],15001);
   for(let i=1;i<8;i++)m=command(m,{type:"ASSETS_READY",...context(m),participantId:`p${i}`,certificate:survivalCurrentRound(m).map!},2);
   for(let i=1;i<8;i++)m=command(m,{type:"ENTERED",...context(m),participantId:`p${i}`},survivalCurrentRound(m).entryRequestedAt!+5000);
   m=command(m,{type:"TICK"},survivalCurrentRound(m).startsAt!);const end=survivalCurrentRound(m).endsAt!;
   expect(survivalCurrentRound(m).players[0]).toMatchObject({highestProgress:0,assetsReadyAt:null,enteredAt:null,absent:true});
   const at=m.updatedAt+1;m=command(m,{type:"ASSETS_READY",...context(m),participantId:"p0",certificate:survivalCurrentRound(m).map!},at);
   m=command(m,{type:"ENTERED",...context(m),participantId:"p0"},at+1);m=record(m,"p0",94.321,at+2);
   expect(survivalCurrentRound(m).players[0]!.highestProgress).toBe(94.321);expect(survivalCurrentRound(m).endsAt).toBe(end);
 });
 it("continues all-absent zero records instead of cancelling or giving a premature winner",()=>{
   let m=map(make(8),1);m=survivalMarkAbsent(m,m.players.map(p=>p.participantId),15001);
   m=command(m,{type:"TICK"},15001);expect(m.state).toBe("COUNTDOWN");
   m=command(m,{type:"TICK"},survivalCurrentRound(m).endsAt!);expect(m.state).toBe("ROUND_RESULT");expect(m.winnerId).toBeNull();
   expect(survivalCurrentRound(m).players.every(p=>p.qualified)).toBe(true);
 });
 it("preserves mixed-match average tier without mutating human LP, even near numeric boundaries",()=>{
   for(const human of [[150],[899,900],[900,1200,1500],[2147483647],[0,0],Array(16).fill(1200)]){
     const before=[...human],bots=survivalOnlineBotLp(42,human,config);
     expect(bots).toHaveLength(16-human.length);expect(bots.every(p=>Number.isSafeInteger(p)&&p>=0&&p<=2147483647)).toBe(true);
     const actual=human.reduce((a,b)=>a+b,0)/human.length,filled=[...human,...bots].reduce((a,b)=>a+b,0)/16;
     expect(survivalDifficultyForAverage(filled,config.tierBands)).toBe(survivalDifficultyForAverage(actual,config.tierBands));expect(human).toEqual(before);
   }
 });
});

describe("online preparation deferral",()=>{
 it.each([8,16,64])("keeps slow downloaders eligible in a %i-player match without extending the shared clock",n=>{
   const cfg={...config,matchPlayerCount:n===64?64 as const:16 as const,round1Qualifiers:n===64?40:10,round2Qualifiers:n===64?26:6,round3Qualifiers:n===64?12:3,...(n===64?{round4Qualifiers:5}:{})};
   let m=map(createSurvivalMatch("slow",entrants(n),cfg,"g",0,true),1);
   const certificate=survivalCurrentRound(m).map!;
   for(let i=1;i<n;i++)m=command(m,{type:"ASSETS_READY",...context(m),participantId:`p${i}`,certificate},2);
   m=command(m,{type:"TICK"},15001);
   expect(m.state).toBe("ENTERING");expect(survivalCurrentRound(m).players[0]).toMatchObject({entryDeferred:true,forfeited:false,assetsReadyAt:null,enteredAt:null,highestProgress:0});
   for(let i=1;i<n;i++)m=command(m,{type:"ENTERED",...context(m),participantId:`p${i}`},20001);
   const start=survivalCurrentRound(m).startsAt!,end=survivalCurrentRound(m).endsAt!;
   m=command(m,{type:"TICK"},start);
   expect(()=>command(m,{type:"ATTEMPT_START",...context(m),participantId:"p0",attemptId:"early"},start+1)).toThrow();
   m=command(m,{type:"ASSETS_READY",...context(m),participantId:"p0",certificate},start+2);
   expect(()=>command(m,{type:"ATTEMPT_START",...context(m),participantId:"p0",attemptId:"early2"},start+3)).toThrow();
   m=command(m,{type:"ENTERED",...context(m),participantId:"p0"},start+4);
   m=record(m,"p0",74.1234,start+5);
   expect(survivalCurrentRound(m).players[0]).toMatchObject({entryDeferred:false,forfeited:false,highestProgress:74.1234});
   expect(survivalCurrentRound(m).endsAt).toBe(end);expect(end-start).toBe(180000);expect(survivalSettlementInputs(m)).toEqual([]);
 });
 it("caps all-downloading grace and map replacement attempts without charging supply failures",()=>{
   let m=map(make(8),1);m=command(m,{type:"TICK"},15001);expect(m.state).toBe("PREPARING");
   m=command(m,{type:"TICK"},60001);expect(m.state).toBe("MAP_SELECTION");
   for(let i=0;i<3;i++){m=map(m,m.updatedAt+1);m=command(m,{type:"MAP_INVALIDATED",...context(m),reason:"COMMON_AUDIO_UNAVAILABLE"},m.updatedAt+1);}
   expect(m.state).toBe("CANCELLED");expect(m.cancellationReason).toBe("MAP_SUPPLY_UNAVAILABLE");expect(survivalSettlementInputs(m)).toEqual([]);
 });
 it("accepts value-identical certificate arrays after JSON transport and rejects changed dependencies",()=>{
   let m=map(make(8),1);survivalCurrentRound(m).map={...survivalCurrentRound(m).map!,songIds:[123],sfxIds:[456]};
   const certificate=structuredClone(survivalCurrentRound(m).map!);
   expect(()=>command(m,{type:"ASSETS_READY",...context(m),participantId:"p0",certificate:{...certificate,songIds:[999]}},2)).toThrow();
   m=command(m,{type:"ASSETS_READY",...context(m),participantId:"p0",certificate},2);expect(survivalCurrentRound(m).players[0]!.assetsReadyAt).toBe(2);
 });
});
