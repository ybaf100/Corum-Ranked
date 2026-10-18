// Build rules first. Reproducible NORMAL distribution + virtual long-run probe:
// node --expose-gc tools/simulate-survival-bots.mjs [report.json]
import {writeFileSync} from "node:fs";
import {performance} from "node:perf_hooks";
import assert from "node:assert/strict";
import {createSurvivalMatch,DEFAULT_SURVIVAL_CONFIG,survivalCurrentRound,simulateSurvivalBots,
  applySurvivalBotFrames,reduceSurvivalMatch} from "../packages/rules/dist/index.js";
function fixture(seed){
  const match=createSurvivalMatch(`sim-${seed}`,Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:i?null:"human",name:`P${i}`,kind:i?"BOT":"HUMAN",lp:150})),DEFAULT_SURVIVAL_CONFIG,"NORMAL",0);
  const r=survivalCurrentRound(match);r.map={levelId:40000001,stars:5,demon:false,demonDifficulty:null,length:3,automatic:false,platformer:false,levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:10,verifiedAt:new Date(0).toISOString()};
  r.startsAt=10000;r.endsAt=190000;r.entryRequestedAt=1000;
  for(const p of r.players){p.assetsReadyAt=1000;p.enteredAt=6000;}
  match.state="PLAYING";match.updatedAt=10000;return match;
}
const quantile=(values,q)=>values.slice().sort((a,b)=>a-b)[Math.floor((values.length-1)*q)];
const mean=values=>values.reduce((a,b)=>a+b,0)/values.length;
const round=n=>Math.round(n*1000)/1000;
const describe=values=>({mean:round(mean(values)),p10:round(quantile(values,.1)),median:round(quantile(values,.5)),p90:round(quantile(values,.9))});
global.gc?.();const heapStart=process.memoryUsage().heapUsed,started=performance.now();
const groups=[],samplesPerGroup=350;
for(const humanBest of [20,50,80]){
  const scores=[],ranks=[],attempts=[];let eliminated=0,clears=0,maxStateBytes=0;
  for(let seed=1;seed<=samplesPerGroup;seed++){
    const m=fixture(seed),r=survivalCurrentRound(m);let state={},frames=[];
    // Feed only observations already confirmed at the previous checkpoint.
    for(let elapsed=30000;elapsed<=180000;elapsed+=30000){
      r.players[0].highestProgress=humanBest*Math.min(1,(elapsed-30000)/95000);
      const sim=simulateSurvivalBots(m,seed,state,Math.min(10000+elapsed,189999));state=sim.state;frames.push(...sim.frames);
    }
    r.players[0].highestProgress=humanBest;
    const after=applySurvivalBotFrames(m,{roundKey:r.key,mapGeneration:1,levelId:r.map.levelId},frames,189999);
    const result=reduceSurvivalMatch(after,{type:"TICK"},190000),players=survivalCurrentRound(result).players;
    const human=players[0];ranks.push(human.rank);if(!human.qualified)eliminated++;
    for(const p of players.slice(1)){scores.push(p.highestProgress);attempts.push(p.attempts.length);if(p.highestProgress===100)clears++;}
    maxStateBytes=Math.max(maxStateBytes,JSON.stringify(state).length);
  }
  groups.push({humanBest,matches:samplesPerGroup,botBest:describe(scores),humanRank:describe(ranks),eliminationRate:round(eliminated/samplesPerGroup),
    botClearRate:round(clears/scores.length),attempts:describe(attempts),maxStateBytes});
}
assert(groups[0].botBest.mean<groups[1].botBest.mean&&groups[1].botBest.mean<groups[2].botBest.mean);
assert(groups[0].botBest.mean<45&&groups[2].botBest.mean-groups[0].botBest.mean>5);
assert(groups.every(g=>g.botClearRate<.04));
assert(groups[0].eliminationRate>.1&&groups[0].eliminationRate<.9);
assert(groups[2].botClearRate>0);
// Continuous 15-bot run with real authoritative frame application every 500ms.
let m=fixture(42),state={},tickMs=[],example=[];let prior=0;
for(let at=10000;at<190000;at+=500){
  const r=survivalCurrentRound(m);r.players[0].highestProgress=20*Math.min(1,(at-10000)/95000);
  const start=performance.now(),sim=simulateSurvivalBots(m,42,state,at);state=sim.state;
  m=applySurvivalBotFrames(m,{roundKey:r.key,mapGeneration:1,levelId:r.map.levelId},sim.frames,at);tickMs.push(performance.now()-start);
  const p=survivalCurrentRound(m).players[1],active=p.attempts.find(a=>a.endedAt===null),now=active?.currentProgress??0;
  assert(p.highestProgress>=prior);prior=p.highestProgress;
  if(at<=45000&&(at%3000===1000||sim.frames.some(f=>f.participantId==="p1"&&f.kind==="END")))example.push({elapsedSeconds:(at-10000)/1000,now:round(now),best:round(p.highestProgress),attempt:active?.sequence??null});
}
// 200 additional 180s rounds (10h virtual time), retaining no prior match state.
for(let i=0;i<200;i++){const fresh=fixture(i);simulateSurvivalBots(fresh,i,{},10**12);}
global.gc?.();const report={policy:"NORMAL",simulationTickMs:500,map:{stars:5,lengthCategory:3,estimatedSeconds:95},cohorts:{strong:3,peer:8,weak:4},
  matches:groups.reduce((a,g)=>a+g.matches,0),groups,example,performance:{continuousTicks:tickMs.length,tickMs:describe(tickMs),p95TickMs:round(quantile(tickMs,.95)),
    maxTickMs:round(Math.max(...tickMs)),wallSeconds:round((performance.now()-started)/1000),virtualSoakHours:10,heapDeltaBytes:process.memoryUsage().heapUsed-heapStart,
    driverStateBytes:JSON.stringify(state).length,scope:"Node pure simulation + reducer; DB timings measured separately in integration tests"}};
const output=JSON.stringify(report,null,2)+"\n";
if(process.argv[2])writeFileSync(process.argv[2],output);process.stdout.write(output);
