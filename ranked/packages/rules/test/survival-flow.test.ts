import {describe,it,expect} from "vitest";
import {createSurvivalMatch,reduceSurvivalMatch,survivalCurrentRound,survivalLiveView,type SurvivalCommand} from "../src/survival-match.js";
import {DEFAULT_SURVIVAL_CONFIG} from "../src/survival.js";
import {survivalMapRange,type SurvivalVerifiedMap} from "../src/survival-maps.js";
const fixture=()=>createSurvivalMatch("flow",Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:i?null:"human",name:`P${i}`,kind:i?"BOT" as const:"HUMAN" as const,lp:150})),DEFAULT_SURVIVAL_CONFIG,"v1",1000);
function verified(phase:Parameters<typeof survivalMapRange>[1],id=40000001):SurvivalVerifiedMap{
 const range=survivalMapRange("RED_BRONZE",phase);
 return {levelId:id,stars:range.minimumStars??10,demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?range.demons[0]!:null,
  automatic:false,platformer:false,levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:100,verifiedAt:new Date(1000).toISOString()};
}
describe("Survival integrated preparation and eliminated viewing",()=>{
 it("selects and exposes the real map during the six-second INTRO without opening preparation/play early",()=>{
  let m=fixture();m.presentationControlled=true;m.presentationGate="INTRO";
  m=reduceSurvivalMatch(m,{type:"MAP_SELECTED",roundKey:"stage-1",mapGeneration:1,map:verified("ROUND_1")},1500);
  expect(m.state).toBe("MAP_SELECTION");expect(survivalLiveView(m,1500).map?.levelId).toBe(40000001);
  m=reduceSurvivalMatch(m,{type:"TICK"},6999);expect(m.state).toBe("MAP_SELECTION");
  m=reduceSurvivalMatch(m,{type:"TICK"},7000);expect(m.state).toBe("PREPARING");
  expect(survivalCurrentRound(m).preparationStartedAt).toBe(7000);
 });
 it("keeps a verified next-round map durable during results and adopts it without rerolling",()=>{
  let m=fixture();m.presentationControlled=true;m.state="ROUND_RESULT";m.presentationGate="ROUND_RESULT";
  const r=survivalCurrentRound(m);r.map=verified("ROUND_1");r.selectedMaps=[r.map];r.finishedAt=2000;
  r.players.forEach((p,i)=>{p.qualified=i<12;p.rank=i+1;});m.updatedAt=2000;
  const command={type:"NEXT_MAP_SELECTED",roundKey:r.key,mapGeneration:1,map:verified("ROUND_2",40000002)} as unknown as SurvivalCommand;
  m=reduceSurvivalMatch(m,command,2100);expect(m.state).toBe("ROUND_RESULT");
  expect((survivalLiveView(m,2100) as unknown as {nextMap:{map:SurvivalVerifiedMap}}).nextMap.map.levelId).toBe(40000002);
  const restored=JSON.parse(JSON.stringify(m));m=reduceSurvivalMatch(restored,{type:"TICK"},7000);
  expect(m.state).toBe("PREPARING");expect(survivalCurrentRound(m).map?.levelId).toBe(40000002);
  expect(survivalCurrentRound(m).players).toHaveLength(12);
 });
 it("publishes a stable authoritative elimination time and rejects official attempts from eliminated viewers",()=>{
  let m=fixture();m=reduceSurvivalMatch(m,{type:"MAP_SELECTED",roundKey:"stage-1",mapGeneration:1,map:verified("ROUND_1")},1000);
  m=reduceSurvivalMatch(m,{type:"FORFEIT",roundKey:"stage-1",mapGeneration:1,participantIds:["p0"],reason:"SURRENDER"},2000);
  const first=survivalLiveView(m,2000,"p0").participants[0] as unknown as {eliminatedAt:string};
  expect(first.eliminatedAt).toBe(new Date(2000).toISOString());
  m=reduceSurvivalMatch(m,{type:"TICK"},4000);
  expect((survivalLiveView(m,4000,"p0").participants[0] as unknown as {eliminatedAt:string}).eliminatedAt).toBe(first.eliminatedAt);
  expect(()=>reduceSurvivalMatch(m,{type:"ATTEMPT_START",roundKey:"stage-1",mapGeneration:1,levelId:40000001,participantId:"p0",attemptId:"viewer"},4001)).toThrow();
 });
});
