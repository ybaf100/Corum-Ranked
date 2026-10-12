import { describe, expect, it } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG } from "../src/survival.js";
import { createSurvivalMatch, reduceSurvivalMatch, survivalCurrentRound, survivalLiveView,
  survivalMilliPercent, survivalSettlementInputs, type SurvivalCommand, type SurvivalMatch } from "../src/survival-match.js";
import { survivalDifficultyForAverage, survivalMapAllowed, survivalMapRange, validateSurvivalEngineConfig,
  survivalDemonFromNativeCode, type SurvivalDifficultyGroup, type SurvivalMapPhase, type SurvivalVerifiedMap } from "../src/survival-maps.js";

class Rig {
  now=Date.parse("2026-10-11T10:00:00Z");
  mapId=40_000_000;
  match:SurvivalMatch;
  constructor(lp=150){this.match=createSurvivalMatch("match",Array.from({length:16},(_,i)=>({
    participantId:`p${String(i+1).padStart(2,"0")}`,playerId:null,name:`Player ${i+1}`,kind:"BOT" as const,lp})),DEFAULT_SURVIVAL_CONFIG,"fixture-v1",this.now);}
  send(command:SurvivalCommand){this.match=reduceSurvivalMatch(this.match,command,this.now);return this.match;}
  get round(){return survivalCurrentRound(this.match);}
  get context(){return {roundKey:this.round.key,mapGeneration:this.round.mapGeneration,levelId:this.round.map!.levelId};}
  select(){const range=survivalMapRange(this.match.difficultyGroup,this.round.phase);
    const map:SurvivalVerifiedMap={levelId:this.mapId++,stars:range.minimumStars??10,demon:range.minimumStars===null,
      demonDifficulty:range.minimumStars===null?range.demons[0]!:null,automatic:false,platformer:false,levelVersion:1,
      geometrySha256:"a".repeat(64),geometryBytes:100,verifiedAt:new Date(this.now).toISOString()};
    this.send({type:"MAP_SELECTED",roundKey:this.round.key,mapGeneration:this.round.mapGeneration,map});return map;}
  assets(){for(const p of [...this.round.players])this.send({type:"ASSETS_READY",...this.context,participantId:p.participantId,certificate:this.round.map!});}
  prepare(){this.select();this.assets();for(const p of [...this.round.players])this.send({type:"ENTERED",...this.context,participantId:p.participantId});
    this.now=this.round.startsAt!;this.send({type:"TICK"});}
  attempt(participantId:string,progress:number,attemptId="a1",end=true){
    this.send({type:"ATTEMPT_START",...this.context,participantId,attemptId});
    this.send({type:end?"ATTEMPT_END":"PROGRESS",...this.context,participantId,attemptId,progress});}
  play(scores?:number[]){this.prepare();for(const [i,p] of [...this.round.players].entries())this.attempt(p.participantId,scores?.[i]??100-i);
    this.now=this.round.endsAt!;this.send({type:"TICK"});}
  next(){this.send({type:"TICK"});}
  final(scores=[100,100,97]){for(let i=0;i<3;i++){this.play();this.next();}this.play(scores);if(this.match.state==="ROUND_RESULT")this.next();}
}
describe("Survival round engine",()=>{
  it("projects captured human tiers and subdivided tier groups separately from virtual matchmaking LP",()=>{
    const r=new Rig();r.match.config={...r.match.config,tierBands:r.match.config.tierBands.map(b=>b.tier==="SILVER"?{...b,tier:"SILVER_I"}:b)};
    r.match.players[0]!.displayedTier="SILVER_I";
    expect(survivalLiveView(r.match,r.now).participants[0]?.displayedTier).toBe("SILVER");
    expect(survivalLiveView(r.match,r.now).participants[1]?.displayedTier).toBe("RED");
    r.play();expect(survivalLiveView(r.match,r.now).completedRound?.players[0]?.displayedTier).toBe("SILVER");
  });
  it("keeps controlled roster/result barriers separate from play timers and rejects stale or non-owner acknowledgements",()=>{
    const r=new Rig();r.match.players[0]!.kind="HUMAN";r.match.players[0]!.playerId="human";
    r.match.presentationControlled=true;r.match.presentationGate="INTRO";
    const acknowledge=()=>({type:"PRESENTATION_COMPLETE" as const,roundKey:r.round.key,mapGeneration:r.round.mapGeneration,participantId:"p01"});
    expect(()=>r.select()).toThrow("presentation is pending");
    expect(()=>r.send({...acknowledge(),participantId:"p02"})).toThrow("owner");
    expect(()=>r.send({...acknowledge(),mapGeneration:2})).toThrow("CONTEXT_CHANGED");
    r.now+=5000;r.next();expect(r.round.startsAt).toBeNull();
    r.send(acknowledge());r.play();expect(r.match.presentationGate).toBe("ROUND_RESULT");
    expect(r.round.endsAt!-r.round.startsAt!).toBe(180000);
    const completed=survivalLiveView(r.match,r.now).completedRound;
    r.now+=4999;r.next();expect(r.match.rounds).toHaveLength(1);
    expect(()=>r.send(acknowledge())).toThrow("not complete");
    r.now++;r.next();expect(r.match.rounds).toHaveLength(1);
    const old=acknowledge();r.send(old);r.next();r.next();expect(r.match.rounds).toHaveLength(2);
    expect(r.round.players).toHaveLength(10);expect(r.round.startsAt).toBeNull();
    expect(survivalLiveView(r.match,r.now).completedRound).toEqual(completed);
    expect(()=>r.send(old)).toThrow("CONTEXT_CHANGED");
  });
  it("records simultaneous trusted forfeits atomically, with one survivor winning and zero survivors cancelling",()=>{
    for(const left of [0,1]) {
      const r=new Rig();const ids=r.round.players.slice(left).map(p=>p.participantId);
      r.send({type:"FORFEIT",roundKey:r.round.key,mapGeneration:r.round.mapGeneration,participantIds:ids,reason:"SESSION_TIMEOUT"});
      expect(r.match.state).toBe(left?"FINAL_RESULT":"CANCELLED");
      expect(r.match.winnerId).toBe(left?"p01":null);
      expect(r.match.players.filter(p=>p.forfeitReason==="SESSION_TIMEOUT")).toHaveLength(16-left);
      if(left)expect(survivalSettlementInputs(r.match)).toHaveLength(16);else expect(survivalSettlementInputs(r.match)).toEqual([]);
    }
  });
  it("releases preparation barriers when a trusted adapter removes only the unready participants",()=>{
    const r=new Rig();r.select();
    for(const p of r.round.players.slice(0,2))r.send({type:"ASSETS_READY",...r.context,participantId:p.participantId,certificate:r.round.map!});
    r.send({type:"FORFEIT",roundKey:r.round.key,mapGeneration:r.round.mapGeneration,participantIds:r.round.players.slice(2).map(p=>p.participantId),reason:"ASSET_FAILURE"});
    expect(r.match.state).toBe("ENTERING");
    r.send({type:"ENTERED",...r.context,participantId:"p01"});r.send({type:"ENTERED",...r.context,participantId:"p02"});
    expect(r.match.state).toBe("COUNTDOWN");expect(r.round.startsAt).toBe(r.now+3000);
    expect(r.round.players[0]!.forfeited).toBe(false);
  });
  it("runs 16→10→6→3→1 with independent frozen LP/config and settlement inputs",()=>{
    const r=new Rig();expect(r.match.averageLp).toBe(150);
    const counts:number[]=[];
    for(let i=0;i<4;i++){counts.push(r.round.players.length);r.play();if(i<3)r.next();}
    expect(counts).toEqual([16,10,6,3]);expect(r.match.state).toBe("FINAL_RESULT");expect(r.match.winnerId).toBe("p01");
    expect(r.match.players.map(p=>p.finalRank)).toEqual(Array.from({length:16},(_,i)=>i+1));
    expect(survivalSettlementInputs(r.match).map(p=>p.delta)).toEqual(DEFAULT_SURVIVAL_CONFIG.placementLp);
    expect(r.match.rounds.every(round=>round.endsAt! - round.startsAt! === 180_000)).toBe(true);
  });
  it("retains raw normal progress, ×1 highest score, and ignores reset/death regression",()=>{
    const r=new Rig();r.prepare();r.attempt("p01",94.37051,"a1");r.attempt("p01",2,"a2");
    const p=r.round.players[0]!;expect(p.highestProgress).toBe(94.37051);
    expect(survivalLiveView(r.match,r.now,"p01").standings[0]?.score).toBe(94.37051);
  });
  it("retains a completed round's eliminated identities after advancing to the next map",()=>{
    const r=new Rig();expect(survivalLiveView(r.match,r.now).completedRound).toBeNull();
    r.play();const completed=survivalLiveView(r.match,r.now).completedRound!;
    expect(completed.players.filter(p=>!p.qualified).map(p=>p.participantId))
      .toEqual(["p11","p12","p13","p14","p15","p16"]);
    expect(completed.players.find(p=>p.participantId==="p16")).toMatchObject({rank:16,score:85});
    r.next();expect(survivalLiveView(r.match,r.now).completedRound).toEqual(completed);
    expect(r.round.players.some(p=>p.participantId==="p16")).toBe(false);
    r.select();expect(survivalLiveView(r.match,r.now).completedRound).toEqual(completed);
    expect(r.round.startsAt).toBeNull();
  });
  it("does not finish at 179.999s and commits cutoff at 180s before a late packet",()=>{
    const r=new Rig();r.prepare();r.attempt("p16",5,"late",false);const context=r.context;
    r.now=r.round.endsAt!-1;r.send({type:"TICK"});expect(r.match.state).toBe("PLAYING");
    r.now++;r.send({type:"PROGRESS",...context,participantId:"p16",attemptId:"late",progress:100});
    expect(r.match.state).toBe("ROUND_RESULT");expect(r.round.players[15]!.highestProgress).toBe(5);
    expect(r.round.finishedAt).toBe(r.round.endsAt);
  });
  it("accepts an attempt exactly when countdown completes, without counting preparation",()=>{
    const r=new Rig();r.select();r.assets();for(const p of [...r.round.players])r.send({type:"ENTERED",...r.context,participantId:p.participantId});
    const invitedAt=r.now;r.now+=2999;expect(()=>r.attempt("p01",2)).toThrow();
    r.now=invitedAt+3000;r.attempt("p01",2);expect(r.match.state).toBe("PLAYING");expect(r.round.endsAt).toBe(invitedAt+183000);
  });
  it("advances every cutoff tie, recomputes nominal targets and never revives eliminated players",()=>{
    const r=new Rig();r.play([100,99,98,97,96,95,94,93,92,91,91,80,79,78,77,76]);expect(r.round.players.filter(p=>p.qualified)).toHaveLength(11);
    r.next();expect(r.round.players).toHaveLength(11);r.play([100,99,98,97,96,95,95,90,89,88,87]);r.next();expect(r.round.players).toHaveLength(7);
    r.play([100,99,98,98,90,89,88]);r.next();expect(r.round.players).toHaveLength(4);
    r.play();expect(r.match.state).toBe("FINAL_RESULT");
    expect(r.match.rounds.slice(1).every(round=>round.players.every(p=>p.participantId!=="p16"))).toBe(true);
  });
  it("assigns competition ranks to tied final losers and gives both the lowest occupied LP",()=>{
    const r=new Rig();r.final([100,97,97]);expect(r.match.players.slice(0,4).map(p=>p.finalRank)).toEqual([1,2,2,4]);
    expect(survivalSettlementInputs(r.match).slice(1,3).map(p=>[p.rank,p.delta])).toEqual([[2,30],[2,30]]);
  });
  it("keeps all-zero fields advancing and resolves a sixteen-way final in Death Match",()=>{
    const r=new Rig();for(let i=0;i<4;i++){r.play(Array(16).fill(0));r.next();}expect(r.round.phase).toBe("TIEBREAK");expect(r.round.players).toHaveLength(16);
    r.prepare();for(const [i,p] of [...r.round.players].entries())r.attempt(p.participantId,100-i,"a1",false);
    r.now=r.round.endsAt!;r.send({type:"TICK"});expect(r.match.winnerId).toBe("p01");
  });
  it("rejects eliminated identities, wrong map/context and unverified map selection",()=>{
    const r=new Rig();r.play();r.next();r.prepare();
    expect(()=>r.attempt("p16",100)).toThrow("not active");
    expect(()=>r.send({type:"ENTERED",...r.context,levelId:1,participantId:"p01"})).toThrow();
    const m=new Rig();const range={levelId:39999999,stars:4,demon:false,demonDifficulty:null,automatic:false,platformer:false,
      levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:100,verifiedAt:new Date(m.now).toISOString()};
    expect(()=>m.send({type:"MAP_SELECTED",roundKey:m.round.key,mapGeneration:1,map:range})).toThrow();
  });
  it.each([8,9,10,11,12,13,14,15,17])("rejects %i initial players in M3",count=>{
    const r=new Rig();expect(()=>createSurvivalMatch("other",Array.from({length:count},(_,i)=>({...r.match.players[i%16]!,participantId:String(i)})),DEFAULT_SURVIVAL_CONFIG,"fixture",r.now)).toThrow();
  });
});
describe("Survival preparation synchronization",()=>{
  it("rerolls at 15s without blame when no assets finish, with a 10s warning",()=>{
    const r=new Rig();r.select();const view=survivalLiveView(r.match,r.now);
    expect(Date.parse(view.preparationWarningAt!)-r.now).toBe(10000);r.now+=14999;r.send({type:"TICK"});expect(r.match.state).toBe("PREPARING");
    r.now++;r.send({type:"TICK"});expect(r.match.state).toBe("MAP_SELECTION");expect(r.round.mapGeneration).toBe(2);
    expect(r.match.players.every(p=>p.forfeitReason===null)).toBe(true);expect(r.round.endsAt).toBeNull();
  });
  it("forfeits only asset stragglers at 15s and continues the ready field",()=>{
    const r=new Rig();r.select();for(const p of r.round.players.slice(0,8))r.send({type:"ASSETS_READY",...r.context,participantId:p.participantId,certificate:r.round.map!});
    r.now+=15000;r.send({type:"TICK"});expect(r.match.state).toBe("ENTERING");expect(r.round.players.filter(p=>p.forfeited)).toHaveLength(8);
    expect(r.match.players.slice(0,8).every(p=>p.forfeitReason===null)).toBe(true);
    for(const p of r.round.players.filter(p=>!p.forfeited))r.send({type:"ENTERED",...r.context,participantId:p.participantId});
    r.now=r.round.startsAt!;r.send({type:"TICK"});for(const p of r.round.players.filter(p=>!p.forfeited))r.attempt(p.participantId,50);
    r.now=r.round.endsAt!;r.send({type:"TICK"});r.next();expect(r.round.players).toHaveLength(8);expect(r.round.phase).toBe("ROUND_2");
  });
  it("anchors the no-entry 5s to the entry invitation after all assets, not map selection",()=>{
    const r=new Rig();r.select();r.now+=14000;r.assets();const old=r.context;
    r.now+=4999;r.send({type:"TICK"});expect(r.match.state).toBe("ENTERING");r.now++;r.send({type:"TICK"});
    expect(r.match.state).toBe("MAP_SELECTION");expect(r.round.startsAt).toBeNull();r.select();
    expect(()=>r.send({type:"ENTERED",...old,participantId:"p01"})).toThrow("CONTEXT_CHANGED");
  });
  it("starts 10s culpability at first actual entry and never blames entered players",()=>{
    const r=new Rig();r.select();r.assets();r.now+=4000;r.send({type:"ENTERED",...r.context,participantId:"p01"});
    r.now+=9999;r.send({type:"TICK"});expect(r.match.state).toBe("ENTERING");
    r.now++;r.send({type:"TICK"});expect(r.match.state).toBe("FINAL_RESULT");expect(r.match.winnerId).toBe("p01");
    expect(r.match.players[0]!.forfeitReason).toBeNull();expect(r.match.players.slice(1).every(p=>p.forfeitReason==="NATIVE_ENTRY_TIMEOUT")).toBe(true);
  });
  it("rerolls all explicit failures without responsibility and checks client metadata",()=>{
    const r=new Rig();r.select();expect(()=>r.send({type:"ASSETS_READY",...r.context,participantId:"p01",certificate:{...r.round.map!,stars:9}})).toThrow("certificate mismatch");
    for(const p of [...r.round.players])r.send({type:"PREPARATION_FAILED",...r.context,participantId:p.participantId,reason:"MAP"});
    expect(r.match.state).toBe("MAP_SELECTION");expect(r.match.players.every(p=>p.forfeitReason===null)).toBe(true);
  });
});
describe("Survival Death Match",()=>{
  it.each([2,3])("admits only %i tied finalists and resolves 0.001%% differences",tied=>{
    const r=new Rig();r.final(tied===3?[100,100,100]:[100,100,97]);expect(r.round.phase).toBe("TIEBREAK");expect(r.round.players).toHaveLength(tied);
    r.prepare();for(const [i,p] of [...r.round.players].entries())for(let a=1;a<=3;a++)r.attempt(p.participantId,94.371-i*0.001,`a${a}`);
    expect(r.match.state).toBe("FINAL_RESULT");expect(r.match.winnerId).toBe("p01");
    expect(r.round.finishedAt).toBeLessThan(r.round.endsAt!);
  });
  it("rounds only Death Match decimal halves and uses exact milli-percent comparisons",()=>{
    expect(survivalMilliPercent(94.3705)).toBe(94371);expect(survivalMilliPercent(1.0055)).toBe(1006);
    const r=new Rig();r.final();r.prepare();r.attempt("p01",94.3705,"a1",false);r.attempt("p02",94.3704,"a1",false);
    r.now=r.round.endsAt!;r.send({type:"TICK"});expect(r.match.winnerId).toBe("p01");
  });
  it("waits for everyone, preserves unfinished received records and pads unused slots with zero",()=>{
    const r=new Rig();r.final();r.prepare();for(let i=1;i<=3;i++)r.attempt("p01",90,`a${i}`);
    const view=survivalLiveView(r.match,r.now);expect(view.standings.find(p=>p.participantId==="p01")?.waitingForOthers).toBe(true);
    expect(r.match.state).toBe("PLAYING");r.attempt("p02",91.123,"active",false);r.now=r.round.endsAt!;r.send({type:"TICK"});
    expect(r.match.winnerId).toBe("p02");expect(r.round.players[1]!.attempts).toHaveLength(1);
  });
  it("repeats 100% ties on new maps and narrows three finalists to the next tied pair",()=>{
    const r=new Rig();r.final([100,100,100]);r.prepare();for(const p of [...r.round.players])r.attempt(p.participantId,p.participantId==="p03"?99:100,"a1",false);
    r.now=r.round.endsAt!;r.send({type:"TICK"});r.next();expect(r.round.players.map(p=>p.participantId)).toEqual(["p01","p02"]);
    for(let replay=0;replay<3;replay++){r.prepare();for(const p of [...r.round.players])r.attempt(p.participantId,100,"a1",false);
      r.now=r.round.endsAt!;r.send({type:"TICK"});r.next();}
    r.prepare();r.attempt("p01",94.371,"a1",false);r.attempt("p02",94.370,"a1",false);r.now=r.round.endsAt!;r.send({type:"TICK"});
    expect(r.match.winnerId).toBe("p01");expect(new Set(r.match.rounds.map(round=>round.map!.levelId)).size).toBe(r.match.rounds.length);
    expect(r.match.players[2]!.finalRank).toBe(3);
  });
  it("limits each Death Match player to three attempts and terminal results are immutable",()=>{
    const r=new Rig();r.final();r.prepare();for(let a=1;a<=3;a++)r.attempt("p01",95,`a${a}`);
    expect(()=>r.attempt("p01",100,"a4")).toThrow();for(let a=1;a<=3;a++)r.attempt("p02",94,`a${a}`);
    const frozen=structuredClone(r.match);r.now+=180000;r.send({type:"TICK"});expect(r.match).toEqual(frozen);
  });
});
describe("Survival map ranges and groups",()=>{
  it.each([[899,"RED_BRONZE"],[900,"SILVER"],[1199,"SILVER"],[1200,"GOLD"]] as const)("uses average LP %i → %s",(lp,group)=>expect(survivalDifficultyForAverage(lp,DEFAULT_SURVIVAL_CONFIG.tierBands)).toBe(group));
  it("uses stable group metadata after boundaries change and tiers subdivide",()=>{
    const bands=[{tier:"RED_I",group:"RED" as const,minimumLp:0},{tier:"BRONZE_II",group:"BRONZE" as const,minimumLp:600},
      {tier:"SILVER_I",group:"SILVER" as const,minimumLp:1000},{tier:"SILVER_II",group:"SILVER" as const,minimumLp:1100},{tier:"GOLD_I",group:"GOLD" as const,minimumLp:1300}];
    expect(survivalDifficultyForAverage(999,bands)).toBe("RED_BRONZE");expect(survivalDifficultyForAverage(1200,bands)).toBe("SILVER");
  });
  const phases:SurvivalMapPhase[]=["ROUND_1","ROUND_2","ROUND_3","FINAL","TIEBREAK"];
  const ranges:Record<SurvivalDifficultyGroup,readonly (readonly [number|null,number|null,readonly string[]])[]>={
    RED_BRONZE:[[4,6,[]],[5,7,[]],[6,8,[]],[7,9,["EASY"]],[null,null,["MEDIUM"]]],
    SILVER:[[5,7,[]],[6,8,[]],[7,9,[]],[null,null,["EASY","MEDIUM"]],[null,null,["HARD"]]],
    GOLD:[[6,8,[]],[7,9,[]],[8,9,["EASY"]],[null,null,["EASY","MEDIUM","HARD"]],[null,null,["INSANE"]]],
  };
  for(const group of Object.keys(ranges) as SurvivalDifficultyGroup[])for(const [i,phase] of phases.entries())it(`${group} ${phase} exact inclusive predicate`,()=>{
    const [low,high,demons]=ranges[group][i]!;
    for(let stars=1;stars<=10;stars++)expect(survivalMapAllowed({levelId:40000000,stars,demon:false,demonDifficulty:null,automatic:false,platformer:false},group,phase)).toBe(low!==null && high!==null && stars>=low && stars<=high);
    for(const demonDifficulty of ["EASY","MEDIUM","HARD","INSANE","EXTREME"] as const)
      expect(survivalMapAllowed({levelId:40000000,stars:10,demon:true,demonDifficulty,automatic:false,platformer:false},group,phase)).toBe(demons.includes(demonDifficulty));
  });
  it("uses confirmed native Demon codes, excludes bad metadata and freezes config",()=>{
    expect([0,3,4,5,6,1].map(survivalDemonFromNativeCode)).toEqual(["HARD","EASY","MEDIUM","INSANE","EXTREME",null]);
    const map={levelId:40000000,stars:4,demon:false,demonDifficulty:null,automatic:false,platformer:false};
    for(const invalid of [{levelId:39999999},{stars:0},{automatic:true},{platformer:true}])expect(survivalMapAllowed({...map,...invalid},"RED_BRONZE","ROUND_1")).toBe(false);
    expect(validateSurvivalEngineConfig({...DEFAULT_SURVIVAL_CONFIG,scoreMultiplier:2})).not.toEqual([]);
    const config=structuredClone(DEFAULT_SURVIVAL_CONFIG);const r=new Rig();const match=createSurvivalMatch("snapshot",r.match.players,config,"old",r.now);
    (config as {roundSeconds:number}).roundSeconds=1;expect(match.config.roundSeconds).toBe(180);
  });
});
