import {describe,it,expect} from "vitest";
import {DEFAULT_SURVIVAL_CONFIG,survivalConfigForPlayerCount} from "../src/survival.js";
import {createSurvivalMatch,reduceSurvivalMatch,survivalCurrentRound,survivalLiveView,survivalMapSelectionTarget,survivalSettlementInputs,type SurvivalCommand,type SurvivalMatch} from "../src/survival-match.js";
import {survivalMapRange,type SurvivalMapPhase,type SurvivalVerifiedMap} from "../src/survival-maps.js";
class Game {
 now=1000;id=40000000;match:SurvivalMatch;
 constructor(count:number,capacity:16|64,force=false,online=false){this.match=createSurvivalMatch("schedule",Array.from({length:count},(_,i)=>({participantId:`p${i}`,playerId:null,name:`P${i}`,kind:"BOT" as const,lp:150})),survivalConfigForPlayerCount(DEFAULT_SURVIVAL_CONFIG,capacity),"g",this.now,online,false,force);}
 get round(){return survivalCurrentRound(this.match);}
 get context(){return{roundKey:this.round.key,mapGeneration:this.round.mapGeneration,levelId:this.round.map?.levelId??0};}
 send(c:SurvivalCommand){this.match=reduceSurvivalMatch(this.match,c,this.now);}
 map(phase=this.round.phase):SurvivalVerifiedMap{const range=survivalMapRange(this.match.difficultyGroup,phase);return{levelId:this.id++,levelVersion:1,stars:range.minimumStars??10,demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?range.demons[0]!:null,automatic:false,platformer:false,geometrySha256:"a".repeat(64),geometryBytes:100,verifiedAt:new Date(this.now).toISOString()};}
 prepare(){if(!this.round.map)this.send({type:"MAP_SELECTED",...this.context,map:this.map()});for(const p of this.round.players)this.send({type:"ASSETS_READY",...this.context,participantId:p.participantId,certificate:this.round.map!});this.now+=5000;for(const p of this.round.players)this.send({type:"ENTERED",...this.context,participantId:p.participantId});this.now=this.round.startsAt!;this.send({type:"TICK"});}
 attempt(id:string,value:number,key="a"){this.send({type:"ATTEMPT_START",...this.context,participantId:id,attemptId:key});this.send({type:"ATTEMPT_END",...this.context,participantId:id,attemptId:key,progress:value});}
 close(){this.now=this.round.endsAt!;this.send({type:"TICK"});}
 next(){this.send({type:"TICK"});this.match=JSON.parse(JSON.stringify(this.match));}
}
const cases16:[number,SurvivalMapPhase[]][]=[[2,["FINAL"]],[3,["FINAL"]],[4,["ROUND_3","FINAL"]],[6,["ROUND_3","FINAL"]],[7,["ROUND_2","ROUND_3","FINAL"]],[10,["ROUND_2","ROUND_3","FINAL"]],[11,["ROUND_1","ROUND_2","ROUND_3","FINAL"]],[16,["ROUND_1","ROUND_2","ROUND_3","FINAL"]]];
const cases64:[number,SurvivalMapPhase[]][]=[[2,["FINAL"]],[5,["FINAL"]],[6,["ROUND_4","FINAL"]],[12,["ROUND_4","FINAL"]],[13,["ROUND_3","ROUND_4","FINAL"]],[26,["ROUND_3","ROUND_4","FINAL"]],[27,["ROUND_2","ROUND_3","ROUND_4","FINAL"]],[40,["ROUND_2","ROUND_3","ROUND_4","FINAL"]],[41,["ROUND_1","ROUND_2","ROUND_3","ROUND_4","FINAL"]],[64,["ROUND_1","ROUND_2","ROUND_3","ROUND_4","FINAL"]]];
for(const [capacity,cases] of [[16,cases16],[64,cases64]] as const)describe(`${capacity}-format played schedules`,()=>{
 for(const online of [false,true])it.each(cases)(`%i players, online=${online}`, (count,phases)=>{const g=new Game(count,capacity,false,online);for(const [i,phase] of phases.entries()){
  expect(g.round).toMatchObject({phase,ordinal:i+1});expect(survivalLiveView(g.match,g.now)).toMatchObject({roundNumber:i+1,displayRoundNumber:i+1,forceDeathMatch:false});
  if(phase!=="FINAL")expect(g.round.target).toBeLessThan(g.round.players.length);
  g.prepare();expect(g.round.endsAt!-g.round.startsAt!).toBe(180000);for(const [n,p] of [...g.round.players].entries())g.attempt(p.participantId,99-n);
  g.close();if(i<phases.length-1)g.next();
 }expect(g.match.state).toBe("FINAL_RESULT");expect(g.match.rounds.map(r=>r.phase)).toEqual(phases);expect(survivalSettlementInputs(g.match)).toHaveLength(count);});
});
it.each([[16,3,"FINAL"],[16,6,"ROUND_3"],[64,5,"FINAL"],[64,12,"ROUND_4"]] as const)("skips forward after %i-format forfeits leave %i survivors",(capacity,alive,next)=>{const g=new Game(capacity,capacity);g.prepare();g.send({type:"FORFEIT",roundKey:g.round.key,mapGeneration:g.round.mapGeneration,participantIds:g.round.players.slice(alive).map(p=>p.participantId),reason:"EXPLICIT_SURRENDER"});for(const [i,p] of g.round.players.slice(0,alive).entries())g.attempt(p.participantId,90-i);g.close();expect(survivalMapSelectionTarget(g.match)?.phase).toBe(next);const map=g.map(next as SurvivalMapPhase);g.send({type:"NEXT_MAP_SELECTED",...g.context,map});expect(survivalLiveView(g.match,g.now).nextMap?.displayRoundNumber).toBe(2);g.next();expect(g.round).toMatchObject({phase:next,ordinal:2,map});});
it.each([["ROUND_2",1],["FINAL",2]] as const)("rejects stale prefetch %s generation %i and requests the confirmed phase",(phase,generation)=>{const g=new Game(16,16);g.prepare();g.send({type:"FORFEIT",roundKey:g.round.key,mapGeneration:1,participantIds:g.round.players.slice(3).map(p=>p.participantId),reason:"SURRENDER"});for(const [i,p] of g.round.players.slice(0,3).entries())g.attempt(p.participantId,90-i);g.close();g.match.nextMap={roundKey:"stage-2",phase,mapGeneration:generation,map:g.map(phase)};expect(survivalMapSelectionTarget(g.match)?.phase).toBe("FINAL");g.next();expect(g.round).toMatchObject({phase:"FINAL",ordinal:2,map:null});expect(g.match.nextMap).toBeUndefined();});
it("finishes a single survivor without constructing a redundant phase",()=>{const g=new Game(16,16);g.send({type:"FORFEIT",roundKey:g.round.key,mapGeneration:1,participantIds:g.round.players.slice(1).map(p=>p.participantId),reason:"SURRENDER"});expect(g.match.state).toBe("FINAL_RESULT");expect(g.match.rounds).toHaveLength(1);});
it("lets an increased tied field advance forward without replaying an earlier phase",()=>{const g=new Game(11,16);g.prepare();for(const p of g.round.players)g.attempt(p.participantId,50);g.close();g.next();expect(g.round).toMatchObject({phase:"ROUND_2",ordinal:2,target:6});expect(g.round.players).toHaveLength(11);});
describe("explicit first-round Death Match",()=>{
 it.each([2,3,4,8,16,32,64])("supports %i players with six attempts, thousandth precision and durable standings",count=>{const g=new Game(count,count>16?64:16,true);expect(g.round).toMatchObject({phase:"TIEBREAK",ordinal:1,target:1});expect(g.match.rounds.some(r=>r.phase==="FINAL")).toBe(false);g.prepare();const end=g.round.endsAt;for(const [i,p] of [...g.round.players].entries())for(let n=0;n<6;n++)g.attempt(p.participantId,i===0?90.123:90.122,`a${n}`);expect(g.match.state).toBe("FINAL_RESULT");expect(g.match.winnerId).toBe("p0");expect(g.round.endsAt).toBe(end);expect(survivalSettlementInputs(g.match)).toHaveLength(count);expect(g.round.players[0]?.attempts).toHaveLength(6);});
 it("carries only the three tied winners into a second Death Match and preserves five losers",()=>{const g=new Game(8,16,true,true);g.prepare();for(const [i,p] of [...g.round.players].entries()){for(let a=0;a<6;a++)g.attempt(p.participantId,i<3?90.123:80-i,`a${a}`);if(i===0)expect(()=>g.attempt(p.participantId,95,"seventh")).toThrow();}expect(g.match.state).toBe("ROUND_RESULT");const ranks=g.match.players.slice(3).map(p=>p.finalRank);g.next();expect(g.round).toMatchObject({phase:"TIEBREAK",ordinal:2});expect(g.match.forceDeathMatch).toBe(true);expect(g.round.players.map(p=>p.participantId)).toEqual(["p0","p1","p2"]);g.prepare();for(const [i,p] of [...g.round.players].entries())for(let a=0;a<6;a++)g.attempt(p.participantId,99-i,`a${a}`);expect(g.match.state).toBe("FINAL_RESULT");expect(g.match.players.slice(3).map(p=>p.finalRank)).toEqual(ranks);expect(g.match.players.map(p=>p.finalRank)).toEqual([1,2,3,4,5,6,7,8]);});
});

it("keeps persisted three-attempt snapshots authoritative while new admissions use six",()=>{
 const g=new Game(2,16,true);expect(g.match.config.tiebreakAttempts).toBe(6);
 g.match.config={...g.match.config,tiebreakAttempts:3};g.match=JSON.parse(JSON.stringify(g.match));g.prepare();
 for(const [i,p] of [...g.round.players].entries())for(let a=0;a<3;a++)g.attempt(p.participantId,90-i,`legacy${a}`);
 expect(g.match.state).toBe("FINAL_RESULT");expect(survivalLiveView(g.match,g.now).tiebreakAttempts).toBe(3);
});
it.each([0,5])("scores best attempt %i of six and does not finish a live sixth",best=>{
 const g=new Game(2,16,true);g.prepare();const p=g.round.players[0]!.participantId,q=g.round.players[1]!.participantId;
 for(let n=0;n<6;n++)g.attempt(q,70,`b${n}`);
 for(let n=0;n<5;n++)g.attempt(p,n===best?90.123:50,`a${n}`);
 g.send({type:"ATTEMPT_START",...g.context,participantId:p,attemptId:"sixth"});
 g.send({type:"PROGRESS",...g.context,participantId:p,attemptId:"sixth",progress:best===5?90.123:40});
 expect(g.match.state).toBe("PLAYING");expect(survivalLiveView(g.match,g.now).standings.find(r=>r.participantId===p)).toMatchObject({attemptsUsed:6,attemptsFinished:5,attemptActive:true,waitingForOthers:false});
 g.send({type:"ATTEMPT_END",...g.context,participantId:p,attemptId:"sixth",progress:best===5?90.123:40});
 expect(g.match.state).toBe("FINAL_RESULT");expect(g.match.winnerId).toBe(p);expect(survivalLiveView(g.match,g.now).standings[0]?.score).toBe(90.123);
});
