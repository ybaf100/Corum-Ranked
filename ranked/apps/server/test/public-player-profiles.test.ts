import {describe,it,expect,vi} from 'vitest';
import {ACHIEVEMENT_DEFINITIONS} from '@corum-ranked/rules';
import {liveSurvivalBands,readPublicPlayerProfiles} from '../src/forum/public-player-profiles.js';
import {publicAchievementTitle} from '../src/achievement/public-title.js';
import type {SqlExecutor} from '../src/database/database.port.js';
describe('read-only current public profiles',()=>{
 it('omits unused match-record aggregation from batched history metadata',async()=>{
  const query=vi.fn(async()=>({rows:[{player_id:'human',account:'123',gd_username:'Player',solo_tier:'SILVER1',solo_lp:1100,survival_tier:'SILVER',survival_lp:900}]}));
  const profiles=await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['123'],false);
  expect(profiles['123']).toMatchObject({solo:{tier:'SILVER1',lp:1100},survival:{tier:'SILVER',lp:900}});
  // No record aggregation, and no ladder read below GOLD.
  expect(profiles['123'].survival).not.toHaveProperty('recordSummary');expect(query).toHaveBeenCalledTimes(1);
 });
 it('batches accounts, keeps both mode ratings distinct and preserves scoped title identity',async()=>{
  const definition=ACHIEVEMENT_DEFINITIONS.find(d=>d.type==='ONE_SHOT')!;
  const query=vi.fn(async(sql:string,_values?:readonly unknown[])=>({rows:sql.includes('WITH outcomes')?[{player_id:'human',count:10,best:1,wins:3,streak:2}]:sql.includes('WITH phases')?[]:[{player_id:'human',account:'123',gd_username:'Player',solo_tier:'GOLD',solo_lp:null,promotion_source_tier:'SILVER',promotion_target_tier:'GOLD',promotion_games_required:5,promotion_wins_required:3,promotion_results:['WIN'],survival_tier:'SILVER',survival_lp:900,title_id:definition.id,title_level:0}]}));
  const profiles=await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['123','123','-1','0','name']);
  expect(profiles['123']).toMatchObject({solo:{tier:'GOLD',lp:null,promotion:{results:['WIN']}},survival:{tier:'SILVER',lp:900,recordSummary:{completedMatches:10,wins:3,losses:7,winStreak:2}},equippedTitle:{achievementId:definition.id,scope:definition.scope}});
  expect(query.mock.calls[0]![1]).toEqual([['123']]);expect(query).toHaveBeenCalledTimes(3);
  for(const [sql]of query.mock.calls)expect(sql).not.toMatch(/\b(INSERT|UPDATE|DELETE|ALTER|TRUNCATE)\b/);
  expect(profiles['123']).not.toHaveProperty('hiddenMmr');
 });
 it('shows each tier as the lobby and leaderboard do: Solo GOLD as its ladder title, Survival tier IDs as their group',async()=>{
  const query=vi.fn(async(sql:string,_values?:readonly unknown[])=>({rows:sql.includes('WITH phases')
   ?[{phase:'current',player_id:'champ',rank:1,total:40,display_tier:'CHAMPION'},{phase:'current',player_id:'gold',rank:30,total:40,display_tier:'GOLD'}]
   :[{player_id:'champ',account:'1',gd_username:'A',solo_tier:'GOLD',solo_lp:2100,survival_tier:'SILVER_B',survival_lp:1000},
     {player_id:'gold',account:'2',gd_username:'B',solo_tier:'GOLD',solo_lp:1300,survival_tier:'RED',survival_lp:100},
     {player_id:'new',account:'3',gd_username:'C',solo_tier:'UNRANKED',solo_lp:null,survival_tier:null,survival_lp:null}]}));
  const profiles=await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['1','2','3'],false,
   [{tier:'RED',group:'RED'},{tier:'SILVER_A',group:'SILVER'},{tier:'SILVER_B',group:'SILVER'}]);
  expect(profiles['1']).toMatchObject({solo:{tier:'CHAMPION',lp:2100},survival:{tier:'SILVER',lp:1000}});
  expect(profiles['2']).toMatchObject({solo:{tier:'GOLD'},survival:{tier:'RED'}});
  // No Survival rating yet: null (the client shows UNRANKED), never a made-up tier.
  expect(profiles['3']).toMatchObject({solo:{tier:'UNRANKED',lp:null},survival:null});
  // Only the GOLD players' standings are read.
  const ladder=query.mock.calls.find(([sql])=>sql.includes('WITH phases'))!;expect(ladder[1]![0]).toEqual(['champ','gold']);
  // Without the live config (not loaded) the stored ID shows as is.
  expect((await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['1'],false))['1'].survival.tier).toBe('SILVER_B');
  expect(liveSurvivalBands(undefined)).toEqual([]);
  expect(liveSurvivalBands({getStatus:()=>({ready:false}),getSnapshot:()=>{throw new Error('unavailable');}})).toEqual([]);
  expect(liveSurvivalBands({getStatus:()=>({ready:true}),getSnapshot:()=>({config:{tierBands:[{tier:'RED',group:'RED'}]}})})).toEqual([{tier:'RED',group:'RED'}]);
 });
 it('does not query for invalid/bot identities or reveal invalid titles',async()=>{
  const query=vi.fn();expect(await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['-1','0','2147483648'])).toEqual({});expect(query).not.toHaveBeenCalled();
  expect(publicAchievementTitle(null,0)).toBeNull();expect(publicAchievementTitle('unknown',0)).toBeNull();
  const progressive=ACHIEVEMENT_DEFINITIONS.find(d=>d.type==='PROGRESSIVE')!;expect(publicAchievementTitle(progressive.id,0)).toBeNull();
 });
});
