import {survivalBestEndedAttempt} from './survival-best-attempt.js';
import { survivalMilliPercent,survivalRoundBadges,survivalPresentRoundBadges,type SurvivalRoundBadge,type SurvivalMatch } from '@corum-ranked/rules';
import { recordedLevelId } from './solo-history.js';
const object=(v:unknown):Record<string,any>=>{if(typeof v==='string'){try{return JSON.parse(v);}catch{return {};}}return v&&typeof v==='object'?v as Record<string,any>:{};};
/** History is a result projection, never the live aggregate (which contains
 * leases, checkpoint evidence, bot cursors and unfinished attempts). */
export function survivalHistoryProjection(value:unknown,personalResult:unknown,myParticipantId:string,summary=false,storedBadges?:readonly SurvivalRoundBadge[]){
 const match=object(value) as unknown as SurvivalMatch,result=object(personalResult);
 const completed=match.state==='FINAL_RESULT';const inProgress=!['FINAL_RESULT','CANCELLED'].includes(match.state);
 const players=Array.isArray(match.players)?match.players:[];
 const badges=survivalPresentRoundBadges(match,storedBadges??survivalRoundBadges(match)).filter(b=>b.participantId===myParticipantId);
 const deathmatches=(Array.isArray(match.rounds)?match.rounds:[]).filter(r=>r.phase==='TIEBREAK');
 const rounds=(Array.isArray(match.rounds)?match.rounds:[]).map(r=>{
  const confirmed=r.finishedAt!==null&&Number.isFinite(r.finishedAt);
  const map=r.map?{levelId:recordedLevelId(r.map.levelId),title:(r.map as any).name??(r.map as any).title??'',creator:(r.map as any).creator??null,
   difficulty:!Number.isFinite(r.map.stars)?null:r.map.demon?(r.map.demonDifficulty?`${r.map.demonDifficulty} DEMON`:'DEMON'):r.map.automatic?'AUTO':r.map.stars===2?'EASY':r.map.stars===3?'NORMAL':r.map.stars<=5?'HARD':r.map.stars<=7?'HARDER':'INSANE',stars:r.map.stars??null,length:typeof r.map.length==='number'?['TINY','SHORT','MEDIUM','LONG','XL','PLATFORMER'][r.map.length]??null:null,featureScore:r.map.featureScore??null,epic:r.map.epic??null}:null;
  const standings=confirmed&&!summary?r.players.map(p=>{
   const entrant=players.find(v=>v.participantId===p.participantId);const attempts=p.attempts.filter(a=>a.endedAt!==null);
   return {participantId:p.participantId,name:entrant?.name??'',isBot:entrant?.kind==='BOT',gdAccountId:entrant?.kind==='BOT'?null:entrant?.gdAccountId??null,displayedTier:entrant?.displayedTier??null,rank:p.rank,
    highestProgress:p.highestProgress,attemptCount:attempts.length,cleared:attempts.some(a=>a.highestProgress===100),qualified:p.qualified&&!p.forfeited,
    forfeited:p.forfeited,own:p.participantId===myParticipantId,tiedPlayers:p.rank===null?0:r.players.filter(v=>v.rank===p.rank).length,
    cutoffBoundary:p.qualified&&!p.forfeited&&r.cutoffScore!==null&&(r.phase==='TIEBREAK'?survivalMilliPercent(p.highestProgress):Math.trunc(p.highestProgress))===r.cutoffScore};
  }).sort((a,b)=>(a.rank??999)-(b.rank??999)||a.participantId.localeCompare(b.participantId)):[];
  const cutoffScore=confirmed&&r.cutoffScore!==null?r.cutoffScore/(r.phase==='TIEBREAK'?1000:1):null;
  const own=r.players.find(p=>p.participantId===myParticipantId);
  const bestAttempt=survivalBestEndedAttempt(r,myParticipantId);
  const replayIdentity=confirmed&&bestAttempt&&r.map?{matchId:match.id,roundKey:r.key,roundNumber:r.ordinal,phase:r.phase,mapGeneration:r.mapGeneration,
   participantId:myParticipantId,attemptId:bestAttempt.id,attemptOrdinal:bestAttempt.sequence,levelId:r.map.levelId,mapVersion:r.map.levelVersion,
   mapGeometrySha256:r.map.geometrySha256,officialBestProgress:bestAttempt.highestProgress,debug:match.online!==true}:null;
  return {roundBadge:badges.find(b=>b.sourceRoundKey===r.key)??null,replayIdentity,deathmatchSequence:r.phase==='TIEBREAK'?deathmatches.findIndex(dm=>dm.key===r.key)+1:null,ownRank:confirmed&&own?own.rank:null,ownQualified:confirmed&&own?own.qualified&&!own.forfeited:null,key:r.key,ordinal:r.ordinal,displayRoundNumber:r.ordinal,phase:r.phase,completed:confirmed,status:confirmed?'COMPLETED':inProgress?'IN_PROGRESS':'UNFINISHED',map,
   cutoffScore,target:r.target,qualifiedCount:confirmed?r.players.filter(p=>p.qualified&&!p.forfeited).length:null,
   finishedAt:confirmed?new Date(r.finishedAt!).toISOString():null,standings};
 });
 const own=players.find(p=>p.participantId===myParticipantId);
 const finalRank=completed?own?.finalRank??null:null;
 const settlementPending=completed&&(typeof result.actualDelta!=='number'||result.finalStandings!==true);
 const storedStandings=Array.isArray(result.standings)?result.standings:[];
 const finalStandings=completed?players.map(p=>{const stored=storedStandings.find(s=>s.participantId===p.participantId);return {participantId:p.participantId,name:p.name,kind:p.kind,rank:p.finalRank,delta:result.finalStandings===true&&typeof stored?.delta==='number'?stored.delta:null};}):[];
 const safeResult=completed?{rank:finalRank,finalStandings:true,standings:finalStandings,requestedDelta:typeof result.requestedDelta==='number'?result.requestedDelta:null,actualDelta:typeof result.actualDelta==='number'?result.actualDelta:null,
  profileAfter:result.profileAfter?{lp:result.profileAfter.lp,tier:result.profileAfter.tier}:null,won:finalRank===1}:null;
 // Compatibility snapshot is sanitized too; no unfinished standings or hidden
 // achievement/bot evidence is returned even to an authenticated participant.
 const snapshot={id:match.id,matchPlayerCount:match.config.matchPlayerCount,forceDeathMatch:match.forceDeathMatch??false,state:match.state,createdAt:match.createdAt,players:(summary?[]:players).map(p=>({participantId:p.participantId,name:p.name,kind:p.kind,finalRank:completed?p.finalRank:null})),rounds:rounds.map(r=>({key:r.key,ordinal:r.ordinal,displayRoundNumber:r.ordinal,phase:r.phase,map:r.map,finishedAt:r.finishedAt,cutoffScore:r.cutoffScore,players:r.standings.map(p=>({participantId:p.participantId,rank:p.rank,highestProgress:p.highestProgress,qualified:p.qualified,attempts:Array.from({length:p.attemptCount},()=>({}))}))}))};
 return {detailsLoaded:!summary,state:match.state,inProgress,completed,settlementPending,finalRank,participantCount:players.length,debug:match.online!==true,
  matchPlayerCount:match.config.matchPlayerCount,reachedFinal:match.rounds?.some(r=>(r.phase==='FINAL'||r.phase==='TIEBREAK')&&r.players.some(p=>p.participantId===myParticipantId))??false,
  roundBadges:badges,
  currentRoundKey:rounds.at(-1)?.key??null,rounds,myParticipantId,result:safeResult,snapshot};
}
