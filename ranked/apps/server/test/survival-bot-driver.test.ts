import {describe,it,expect} from "vitest";
import {createSurvivalMatch,reduceSurvivalMatch,survivalCurrentRound,DEFAULT_SURVIVAL_CONFIG,type SurvivalMatch} from "@corum-ranked/rules";
import {driveSurvivalBots} from "../src/survival/survival-bot-driver.js";

function playing(dm=false):SurvivalMatch {
  let m=createSurvivalMatch("simulation",Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:i?null:"human",name:`P${i}`,kind:i?"BOT" as const:"HUMAN" as const,lp:150})),DEFAULT_SURVIVAL_CONFIG,"test",0);
  const r=survivalCurrentRound(m);if(dm)r.phase="TIEBREAK";
  r.map={levelId:40000001,stars:dm?10:5,demon:dm,demonDifficulty:dm?"MEDIUM":null,length:3,automatic:false,platformer:false,levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:10,verifiedAt:new Date(0).toISOString()};
  r.startsAt=10000;r.endsAt=190000;r.entryRequestedAt=1000;
  for(const p of r.players){p.assetsReadyAt=1000;p.enteredAt=6000;}
  m.state="PLAYING";m.updatedAt=10000;return m;
}
describe("authoritative bot driver regression",()=>{
  it("starts NOW and BEST at zero instead of granting an initial random record",()=>{
    const {match}=driveSurvivalBots(playing(),42,{},10000);
    expect(survivalCurrentRound(match).players.slice(1).every(p=>p.highestProgress===0)).toBe(true);
  });
  it("Death Match consumes time for attempts instead of instantly ending all three",()=>{
    const {match}=driveSurvivalBots(playing(true),42,{},10500);
    for(const p of survivalCurrentRound(match).players.slice(1)){
      expect(p.attempts.filter(a=>a.endedAt!==null)).toHaveLength(0);
      expect(p.highestProgress).toBeLessThan(2);
    }
  });
  it("adapts subsequent attempts to confirmed human performance",()=>{
    const simulate=(best:number)=>{
      let m=playing();const r=survivalCurrentRound(m),context={roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,participantId:"p0",attemptId:"human"};
      m=reduceSurvivalMatch(m,{type:"ATTEMPT_START",...context},10000);
      m=reduceSurvivalMatch(m,{type:"PROGRESS",...context,progress:best},10000);
      return survivalCurrentRound(driveSurvivalBots(m,42,{},189500).match).players.slice(1).reduce((a,p)=>a+p.highestProgress,0)/15;
    };
    expect(simulate(80)-simulate(20)).toBeGreaterThan(8);
  });
});
