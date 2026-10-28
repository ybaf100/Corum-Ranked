import {describe,it,expect} from "vitest";
import {DEFAULT_SURVIVAL_CONFIG,createSurvivalMatch,survivalCurrentRound,reduceSurvivalMatch,survivalLiveView,
  applySurvivalBotFrames,simulateSurvivalBots,survivalBotCohorts,type SurvivalBotDriverState,type SurvivalBotFrame} from "../src/index.js";

function fixture(dm=false,online=false){
  const m=createSurvivalMatch("match",Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:i?null:"human",name:`P${i}`,kind:i?"BOT" as const:"HUMAN" as const,lp:150})),DEFAULT_SURVIVAL_CONFIG,"test",0,online);
  const r=survivalCurrentRound(m);if(dm)r.phase="TIEBREAK";
  r.map={levelId:40000001,stars:dm?10:5,demon:dm,demonDifficulty:dm?"MEDIUM":null,length:3,automatic:false,platformer:false,levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:10,verifiedAt:new Date(0).toISOString()};
  r.startsAt=10000;r.endsAt=190000;r.entryRequestedAt=1000;
  for(const p of r.players){p.assetsReadyAt=1000;p.enteredAt=6000;}
  m.state="PLAYING";m.updatedAt=10000;return m;
}
function run(seed:number,best:number,dm=false){
  const m=fixture(dm);survivalCurrentRound(m).players[0]!.highestProgress=best;
  const sim=simulateSurvivalBots(m,seed,{},189999);
  return {sim,match:applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,189999)};
}
describe("Survival adaptive NORMAL attempts",()=>{
  it("assigns approximately 20/50/30 cohorts for every supported bot count",()=>{
    for(let n=1;n<=15;n++){
      const cohorts=survivalBotCohorts(n,42,"round");
      expect(cohorts.filter(v=>v==="STRONG")).toHaveLength(Math.round(n*.2));
      expect(cohorts.filter(v=>v==="WEAK")).toHaveLength(n-Math.round(n*.2)-Math.round(n*.5));
      expect(cohorts).toHaveLength(n);
    }
  });
  it("starts every bot at zero, staggers entry and produces different bounded live speeds",()=>{
    let m=fixture(),state:SurvivalBotDriverState={};const speeds=new Set<number>();
    for(let at=10000;at<=20000;at+=500){
      const before=survivalCurrentRound(m),sim=simulateSurvivalBots(m,42,state,at);state=sim.state;
      m=applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,at);
      for(const p of survivalCurrentRound(m).players.slice(1)){
        const active=p.attempts.find(a=>a.endedAt===null),old=before.players.find(v=>v.participantId===p.participantId)!.attempts.find(a=>a.id===active?.id);
        if(active&&old){const d=active.highestProgress-old.highestProgress;speeds.add(d);expect(d).toBeGreaterThanOrEqual(0);expect(d).toBeLessThan(1);}
        expect(p.highestProgress).toBeLessThan(11);
      }
    }
    expect(speeds.size).toBeGreaterThan(10);
  });
  it("keeps BEST monotone, resets NOW on death, and never raises BEST before NOW passes it",()=>{
    let m=fixture(),state:SurvivalBotDriverState={};let deaths=0,held=0,improved=0;
    for(let at=10500;at<190000;at+=500){
      const old=survivalCurrentRound(m),sim=simulateSurvivalBots(m,37,state,at);state=sim.state;
      m=applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,at);
      const live=survivalLiveView(m,at);
      for(const p of survivalCurrentRound(m).players.slice(1)){
        const previous=old.players.find(v=>v.participantId===p.participantId)!;
        expect(p.highestProgress).toBeGreaterThanOrEqual(previous.highestProgress);
        const now=p.attempts.find(a=>a.endedAt===null)?.currentProgress??0;
        if(now<previous.highestProgress&&!sim.frames.some(f=>f.participantId===p.participantId&&f.kind==="END")){expect(p.highestProgress).toBe(previous.highestProgress);held++;}
        if(p.highestProgress>previous.highestProgress)improved++;
        if(sim.frames.some(f=>f.participantId===p.participantId&&f.kind==="END")){
          expect(live.standings.find(v=>v.participantId===p.participantId)!.currentProgress).toBe(0);deaths++;
        }
      }
    }
    expect(deaths).toBeGreaterThan(20);expect(held).toBeGreaterThan(100);expect(improved).toBeGreaterThan(100);
  });
  it("one human death cannot lower the estimate or move an active bot",()=>{
    const m=fixture(),r=survivalCurrentRound(m);r.players[0]!.highestProgress=50;
    const a=simulateSurvivalBots(m,41,{},80000),active=Object.entries(a.state.bots).filter(([,b])=>b.phase==="PLAYING");
    r.players[0]!.attempts=[{id:"human",sequence:1,startedAt:10000,endedAt:80000,highestProgress:50,currentProgress:0}];
    const b=simulateSurvivalBots(m,41,a.state,80500);
    expect(Math.abs(b.state.estimate-a.state.estimate)).toBeLessThan(.5);
    for(const [id,old] of active){const next=b.state.bots[id]!;if(next.phase==="PLAYING"){expect(next.progress-old.progress).toBeLessThan(1);expect(next.ceiling).toBe(old.ceiling);}}
  });
  it("recovers byte-identical attempts from JSON state and bounded large catch-up ticks",()=>{
    let m=fixture(),state:SurvivalBotDriverState={};
    for(let at=10500;at<=70000;at+=500){const sim=simulateSurvivalBots(m,51,state,at);state=sim.state;m=applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,at);}
    const a=simulateSurvivalBots(m,51,state,189999),b=simulateSurvivalBots(JSON.parse(JSON.stringify(m)),51,JSON.parse(JSON.stringify(state)),10**12);
    expect(a).toEqual(b);
    const twice=simulateSurvivalBots(m,51,a.state,189999);expect(twice.state).toEqual(a.state);
    const stale=simulateSurvivalBots(m,51,a.state,80000);expect(stale.state).toEqual(a.state);expect(stale.frames).toHaveLength(0);
    expect(JSON.stringify(a.state).length).toBeLessThan(9000);
    expect(Object.values(a.state.bots).every(v=>v.sequence<=64)).toBe(true);
  });
  it("has the same committed result for many small ticks and one catch-up with identical human observations",()=>{
    let m=fixture(),state:SurvivalBotDriverState={};
    for(let at=10500;at<190000;at+=500){const sim=simulateSurvivalBots(m,51,state,at);state=sim.state;m=applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,at);}
    expect(survivalCurrentRound(m).players).toEqual(survivalCurrentRound(run(51,0).match).players);
  });
  it("preserves an in-flight legacy cursor's BEST and resumes its attempt without replay",()=>{
    const m=fixture(),p=survivalCurrentRound(m).players[1]!;m.updatedAt=80000;p.highestProgress=80;
    p.attempts=[{id:"bot-normal",sequence:1,startedAt:10000,endedAt:null,highestProgress:80,currentProgress:80}];
    const sim=simulateSurvivalBots(m,42,{"stage-1:1:p1":5},80500);
    const after=applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,80500);
    expect(survivalCurrentRound(after).players[1]!.highestProgress).toBeGreaterThanOrEqual(80);
    expect(survivalCurrentRound(after).players[1]!.attempts[0]!.id).toBe("bot-normal");
  });
  it("uses at most six timed Death Match attempts with precise scores",()=>{
    const {match,sim}=run(19,40,true);
    expect(sim.frames.filter(f=>f.kind==="START").some(f=>f.at>30000)).toBe(true);
    for(const p of survivalCurrentRound(match).players.slice(1)){
      expect(p.attempts.length).toBeLessThanOrEqual(6);expect(p.attempts.length).toBeGreaterThan(3);expect(p.attempts.filter(a=>a.endedAt!==null).every(a=>a.endedAt!>a.startedAt)).toBe(true);expect(p.attempts.filter(a=>a.endedAt===null).length).toBeLessThanOrEqual(1);
      expect(p.attempts.some(a=>Math.trunc(a.highestProgress)!==a.highestProgress)).toBe(true);
    }
    const later=simulateSurvivalBots(match,19,sim.state,10**12);expect(later.frames).toHaveLength(0);
  });
  it("does not alter real human attempt history, LP, cutoff ties or settlement",()=>{
    const m=fixture(false,true),r=survivalCurrentRound(m);r.players[0]!.highestProgress=50;
    const human=structuredClone(r.players[0]),entrants=structuredClone(m.players),sim=simulateSurvivalBots(m,12,{},189999);
    const after=applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},sim.frames,189999);
    expect(survivalCurrentRound(after).players[0]).toEqual(human);expect(after.players).toEqual(entrants);
    const result=reduceSurvivalMatch(after,{type:"TICK"},190000),live=survivalLiveView(result,190000);
    for(const p of survivalCurrentRound(result).players)if(p.highestProgress>=live.cutoffScore!)expect(p.qualified).toBe(true);
    expect(result.config.placementLp).toEqual(DEFAULT_SURVIVAL_CONFIG.placementLp);
  });
  it("uses robust online human observations: adding one zero cannot weaken an established field",()=>{
    const m=fixture(false,true),r=survivalCurrentRound(m);m.players[1]!.kind="HUMAN";m.players[1]!.playerId="other";
    r.players[0]!.highestProgress=80;r.players[1]!.highestProgress=80;
    const a=simulateSurvivalBots(m,12,{},150000);r.players[1]!.highestProgress=0;
    const b=simulateSurvivalBots(m,12,{},150000);expect(a.state.estimate).toBe(b.state.estimate);
  });
  it("uses the actual Debug LP prior independently of virtual map-matching LP",()=>{
    const m=fixture();const low=simulateSurvivalBots(m,42,{},10000,[150]);
    const high=simulateSurvivalBots(m,42,{},10000,[20000]);
    expect(high.state.prior).toBeGreaterThan(low.state.prior);
    expect(m.players[0]!.lp).toBe(150);
    expect(simulateSurvivalBots(fixture(true),42,{},10000).state.prior).toBeLessThan(low.state.prior);
  });
  it("rejects human simulation frames and stale context, preserving the original match",()=>{
    const m=fixture(),frames:SurvivalBotFrame[]=[{participantId:"p0",kind:"START",attemptId:"forged",progress:0,at:10000}];
    expect(()=>applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:1,levelId:40000001},frames,10000)).toThrow("Invalid bot");
    expect(applySurvivalBotFrames(m,{roundKey:"stage-1",mapGeneration:2,levelId:40000001},frames,10000)).toBe(m);
    expect(survivalCurrentRound(m).players[0]!.attempts).toHaveLength(0);
  });
  it("bounds a single-round estimate while using recent rounds and map difficulty",()=>{
    const m=fixture(),first=survivalCurrentRound(m);first.players[0]!.highestProgress=80;
    const high=simulateSurvivalBots(m,31,{},189999).state;
    expect(high.estimate-high.prior).toBeLessThanOrEqual(45);
    const next=structuredClone(first);next.key="stage-2";next.mapGeneration++;next.players[0]!.highestProgress=0;m.rounds.push(next);
    expect(simulateSurvivalBots(m,31,{},10000).state.prior).toBeGreaterThan(high.prior);
  });
});
