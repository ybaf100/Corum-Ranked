import { RankedDomainError } from "./errors.js";
import { SeededRandom,shuffle } from "./random.js";
import { survivalDifficultyForAverage } from "./survival-maps.js";
import type { SurvivalConfig } from "./survival.js";

export function survivalOnlineTargets(count:number,capacity=16):readonly number[] {
  if(!Number.isInteger(count)||count<2||count>capacity || ![16,64].includes(capacity))throw new RankedDomainError("INVALID_CONFIG","Online Survival requires 2–64 entrants within the configured field");
  if(capacity===64)return count<8?[5,5,5,5]:[40,26,12,5].map(n=>Math.max(2,Math.round(count*n/64)));
  return count<8?[3,3,3]:[Math.round(count*10/16),Math.round(count*6/16),Math.round(count*3/16)];
}
export function survivalOnlineRewardRank(rank:number,count:number):number {
  if(!Number.isInteger(rank)||rank<1||rank>count||count<2||count>64)
    throw new RankedDomainError("INVALID_CONFIG","Invalid online Survival reward position");
  return 1+Math.round((rank-1)*15/(count-1));
}
/** Preserve the actual humans' difficulty average/group; bots have no accounts. */
export function survivalOnlineBotLp(seed:number,humanLp:readonly number[],config:SurvivalConfig,botCount=config.matchPlayerCount-humanLp.length):readonly number[] {
  if(!humanLp.length||humanLp.length>config.matchPlayerCount||humanLp.some(n=>!Number.isInteger(n)||n<0||n>2147483647))
    throw new RankedDomainError("INVALID_CONFIG","Invalid human LP snapshots");
  if(!Number.isInteger(botCount)||botCount<0||botCount>config.matchPlayerCount-1||humanLp.length+botCount<2||humanLp.length+botCount>config.matchPlayerCount)throw new RankedDomainError("INVALID_CONFIG","Invalid bot count");
  const count=botCount,total=humanLp.length+count;if(!count)return [];
  const sum=humanLp.reduce((a,b)=>a+b,0),budget=Math.floor(sum*total/humanLp.length)-sum;
  const base=Math.floor(budget/count),remainder=budget%count;
  const bots=Array.from({length:count},(_,i)=>base+(i<remainder?1:0));
  const random=new SeededRandom(seed),order=shuffle(Array.from({length:count},(_,i)=>i),random);
  for(let i=0;i+1<count;i+=2){
    const a=order[i]!,b=order[i+1]!,limit=Math.min(bots[b]!,2147483647-bots[a]!,Math.floor(base*.4));
    const transfer=Math.floor(random.next()*(limit+1));bots[a]!+=transfer;bots[b]!-=transfer;
  }
  // The floor changes mean by less than 1/total LP and cannot cross an integer
  // tier boundary downward unless the original mean was already below it.
  if(survivalDifficultyForAverage(sum/humanLp.length,config.tierBands)!==
    survivalDifficultyForAverage((sum+bots.reduce((a,b)=>a+b,0))/total,config.tierBands))
    throw new RankedDomainError("INVALID_CONFIG","Bot LP changed the configured difficulty group");
  return bots;
}
