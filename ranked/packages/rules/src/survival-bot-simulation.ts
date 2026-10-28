import {survivalDebugSeed} from "./survival-debug.js";
import {survivalCurrentRound,type SurvivalMatch,type SurvivalBotFrame} from "./survival-match.js";

/** NORMAL is deliberately independent of LP settlement and map/cutoff rules. */
export const SURVIVAL_BOT_TICK_MS=500;
export type SurvivalBotCohort="STRONG"|"PEER"|"WEAK";
export interface SurvivalSimulatedBot {
  cohort:SurvivalBotCohort; sequence:number; attemptId:string; progress:number;
  best:number; ceiling:number; speed:number; segment:number; skill:number;
  phase:"RETRY"|"PLAYING"|"DONE"; nextAt:number;
}
export interface SurvivalBotState {
  version:1; context:string; simulatedAt:number; prior:number; estimate:number;
  bots:Record<string,SurvivalSimulatedBot>;
}
// Old beta.81 numeric cursors are accepted only for migration. Scores already
// committed in the match snapshot remain authoritative during that migration.
export type SurvivalBotDriverState=SurvivalBotState|Record<string,number>;
const clamp=(n:number,lo:number,hi:number)=>Math.max(lo,Math.min(hi,n));
const precise=(n:number)=>Math.round(n*1000)/1000;
const median=(values:number[])=>{const s=values.slice().sort((a,b)=>a-b);return s.length?(s[Math.floor((s.length-1)/2)]!+s[Math.floor(s.length/2)]!)/2:0;};
const draw=(seed:number,key:string)=>{
  let h=survivalDebugSeed(seed,key);h=Math.imul(h^(h>>>16),0x85ebca6b);
  h=Math.imul(h^(h>>>13),0xc2b2ae35);return ((h^(h>>>16))>>>0)/0x1_0000_0000;
};
export function survivalBotCohorts(count:number,seed:number,key:string):SurvivalBotCohort[] {
  const strong=Math.round(count*.2),weak=count-strong-Math.round(count*.5);
  const order=Array.from({length:count},(_,i)=>i).sort((a,b)=>draw(seed,`${key}:cohort:${a}`)-draw(seed,`${key}:cohort:${b}`));
  const result:SurvivalBotCohort[]=Array.from({length:count},()=>"PEER");
  order.forEach((id,i)=>{result[id]=i<strong?"STRONG":i>=count-weak?"WEAK":"PEER";});return result;
}
/** Length is GD's verified category, not a client-supplied completion time.
 * No reliable exact duration exists in the current map certificate. */
export function survivalBotMapSeconds(match:SurvivalMatch):number {
  return [15,30,55,95][survivalCurrentRound(match).map?.length??3]??95;
}
function priorSkill(match:SurvivalMatch,humanLp?:readonly number[]):number {
  const round=survivalCurrentRound(match),humans=match.players.filter(p=>p.kind==="HUMAN");
  const lp=median(humanLp?.length?[...humanLp]:humans.map(p=>p.lp));
  const demonPenalty=round.map?.demon?4+2*["EASY","MEDIUM","HARD","INSANE","EXTREME"].indexOf(round.map.demonDifficulty??"EASY"):0;
  const mapPenalty=((round.map?.stars??6)-6)*2+demonPenalty;
  const lpPrior=clamp(36+22*Math.tanh(lp/1000)-mapPenalty,26,65);
  const recent=match.rounds.slice(0,-1).slice(-2).flatMap(r=>{
    const scores=r.players.filter(p=>humans.some(h=>h.participantId===p.participantId)&&!p.forfeited).map(p=>p.highestProgress);
    return scores.length?[median(scores)]:[];
  });
  return recent.length?clamp(.35*lpPrior+.65*median(recent),10,90):lpPrior;
}
function humanObservation(match:SurvivalMatch,at:number,prior:number):number {
  const round=survivalCurrentRound(match),humans=round.players.filter(p=>!p.forfeited&&match.players.some(e=>e.kind==="HUMAN"&&e.participantId===p.participantId));
  if(!humans.length)return prior;
  // Current BEST cannot fall on death. Early zero is absence of evidence. With
  // multiple humans use the upper half's median: one deliberate zero cannot
  // lower the field, and online priors limit mass sandbagging within a round.
  const scores=humans.map(p=>p.highestProgress).sort((a,b)=>a-b);
  const observed=match.online&&scores.length>1?median(scores.slice(Math.floor(scores.length/2))):median(scores);
  const elapsed=(at-round.startsAt!)/1000;
  const confidence=clamp(elapsed/35,0,1);
  const paced=clamp(observed/Math.max(.35,Math.min(1,elapsed/survivalBotMapSeconds(match))),0,100);
  const target=prior*(1-confidence)+paced*confidence;
  return clamp(target,match.online?Math.max(10,prior-18):8,Math.min(95,prior+45));
}
function makeState(match:SurvivalMatch,seed:number,humanLp?:readonly number[]):SurvivalBotState {
  const round=survivalCurrentRound(match),key=`${round.key}:${round.mapGeneration}:${round.map!.levelId}`;
  const bots=match.players.filter(p=>p.kind==="BOT"&&round.players.some(r=>r.participantId===p.participantId&&!r.forfeited));
  const cohorts=survivalBotCohorts(bots.length,seed,key),prior=priorSkill(match,humanLp);
  // Upgrade an in-flight beta.81 snapshot without replaying past events or
  // reducing any recorded BEST. Fresh rounds always begin at startsAt.
  const migrated=bots.some(p=>round.players.find(r=>r.participantId===p.participantId)!.attempts.length);
  const state:SurvivalBotState={version:1,context:key,simulatedAt:migrated?Math.max(round.startsAt!,match.updatedAt):round.startsAt!,prior,estimate:prior,bots:{}};
  bots.forEach((p,i)=>{
    const row=round.players.find(r=>r.participantId===p.participantId)!,active=row.attempts.find(a=>a.endedAt===null);
    const progress=active?.currentProgress??active?.highestProgress??0;
    state.bots[p.participantId]={cohort:cohorts[i]!,sequence:row.attempts.length,attemptId:active?.id??"",progress,
      best:row.highestProgress,ceiling:Math.max(progress,prior),speed:100/survivalBotMapSeconds(match),segment:Math.floor(progress/10),skill:prior,
      phase:active?"PLAYING":row.attempts.some(a=>a.highestProgress===100)&&round.phase!=="TIEBREAK"?"DONE":"RETRY",
      nextAt:state.simulatedAt+350+Math.floor(draw(seed,`${key}:${p.participantId}:initial`)*1450)};
  });return state;
}
function begin(bot:SurvivalSimulatedBot,seed:number,key:string,estimate:number,seconds:number,at:number):void {
  const jitter=(draw(seed,`${key}:personality`)-.5)*10;
  const offset=bot.cohort==="STRONG"?34:bot.cohort==="WEAK"?-18:0;
  bot.skill=clamp(38+.55*(estimate-50)+offset+jitter,8,93);
  bot.sequence++;bot.attemptId=`sim-v1-${bot.sequence}`;bot.progress=0;bot.segment=0;
  const attempt=`${key}:${bot.sequence}`;
  const spread=(draw(seed,`${attempt}:a`)+draw(seed,`${attempt}:b`)+draw(seed,`${attempt}:c`)-1.5)*18;
  bot.ceiling=clamp(bot.skill+spread+Math.min(4,bot.sequence-1),5,97);
  const maximumReach=clamp(bot.skill+(draw(seed,`${key}:maximum`)-.5)*14,5,97);
  bot.ceiling=Math.min(bot.ceiling,maximumReach);
  if(bot.sequence===1)bot.ceiling*=.45;
  if(draw(seed,`${attempt}:mistake`)<.20)bot.ceiling*=.2+.6*draw(seed,`${attempt}:early`);
  // Updating a personal record is not guaranteed on every retry.
  if(bot.best>0&&draw(seed,`${attempt}:improve`)>.45+.003*bot.skill)
    bot.ceiling=Math.min(bot.ceiling,bot.best*(.45+.5*draw(seed,`${attempt}:hold`)));
  if(bot.cohort==="STRONG"&&bot.skill>70&&draw(seed,`${attempt}:clear`)<.07)bot.ceiling=100;
  bot.speed=100/seconds*(.94+.12*draw(seed,`${attempt}:speed`));
  bot.phase="PLAYING";bot.nextAt=at;
}

/** Pure, deterministic 500ms steps. The caller saves state and match together
 * under the existing match lock. Frames are coalesced before DB projection;
 * intermediate simulated ticks never perform I/O or clone the match. */
export function simulateSurvivalBots(match:SurvivalMatch,seed:number,previous:SurvivalBotDriverState,now:number,humanLp?:readonly number[]):{state:SurvivalBotState;frames:SurvivalBotFrame[]} {
  const round=survivalCurrentRound(match),key=`${round.key}:${round.mapGeneration}:${round.map!.levelId}`;
  const saved=previous as Partial<SurvivalBotState>;
  const state=saved.version===1&&saved.context===key?structuredClone(saved as SurvivalBotState):makeState(match,seed,humanLp);
  const limit=Math.min(now,round.endsAt!-1),frames:SurvivalBotFrame[]=[];
  if(state.simulatedAt+SURVIVAL_BOT_TICK_MS>limit)return {state,frames};
  const seconds=survivalBotMapSeconds(match),dm=round.phase==="TIEBREAK";
  // At most 359 steps per 180s round, even after a very large clock jump.
  while(state.simulatedAt+SURVIVAL_BOT_TICK_MS<=limit){
    const at=state.simulatedAt+SURVIVAL_BOT_TICK_MS;
    const target=humanObservation(match,at,state.prior);
    state.estimate+= (target-state.estimate)*(1-Math.exp(-.5/16));
    for(const [id,bot] of Object.entries(state.bots)){
      if(!round.players.some(p=>p.participantId===id&&!p.forfeited))continue;
      const botKey=`${key}:${id}`;
      if(bot.phase==="RETRY"&&at>=bot.nextAt){
        if(bot.sequence>=(dm?match.config.tiebreakAttempts:64)){bot.phase="DONE";continue;}
        begin(bot,seed,botKey,state.estimate,seconds,at);
        frames.push({participantId:id,attemptId:bot.attemptId,kind:"START",at,progress:0});continue;
      }
      if(bot.phase!=="PLAYING")continue;
      // Smooth segment pacing, no progress warp when difficulty changes.
      const segmentSpeed=1+.08*Math.sin((bot.progress/100)*Math.PI*4+draw(seed,`${botKey}:pace`)*Math.PI*2);
      let progress=Math.min(bot.ceiling,bot.progress+bot.speed*.5*segmentSpeed);
      const segment=Math.floor(progress/10);
      if(segment>bot.segment&&progress<100){
        const offset=bot.cohort==="STRONG"?34:bot.cohort==="WEAK"?-18:0;
        const targetSkill=clamp(38+.55*(state.estimate-50)+offset+(draw(seed,`${botKey}:personality`)-.5)*10,8,93);
        bot.skill+=clamp(targetSkill-bot.skill,-1,1);
        const hazard=clamp(.045+(.55-bot.skill/100)*.06,.015,.09);
        if(draw(seed,`${botKey}:${bot.sequence}:segment:${segment}`)<hazard)bot.ceiling=progress;
        bot.segment=segment;
      }
      progress=precise(progress);bot.progress=progress;bot.best=Math.max(bot.best,progress);
      if(progress>=precise(bot.ceiling)){
        frames.push({participantId:id,attemptId:bot.attemptId,kind:"END",at,progress});
        bot.progress=0;bot.phase=progress===100&&!dm?"DONE":"RETRY";
        bot.nextAt=at+800+Math.floor(draw(seed,`${botKey}:${bot.sequence}:retry`)*1200)+Math.max(0,50-bot.skill)*8;
      }
    }
    state.simulatedAt=at;
  }
  for(const [id,bot] of Object.entries(state.bots))if(bot.phase==="PLAYING")
    frames.push({participantId:id,attemptId:bot.attemptId,kind:"PROGRESS",at:state.simulatedAt,progress:bot.progress});
  return {state,frames:frames.sort((a,b)=>a.at-b.at)};
}
