import {describe,it,expect,vi} from 'vitest';
import {ACHIEVEMENT_DEFINITIONS} from '@corum-ranked/rules';
import {readPublicPlayerProfiles} from '../src/forum/public-player-profiles.js';
import {publicAchievementTitle} from '../src/achievement/public-title.js';
import type {SqlExecutor} from '../src/database/database.port.js';
describe('read-only current public profiles',()=>{
 it('omits unused match-record aggregation from batched history metadata',async()=>{
  const query=vi.fn(async()=>({rows:[{player_id:'human',account:'123',gd_username:'Player',solo_tier:'GOLD',solo_lp:1200,survival_tier:'SILVER',survival_lp:900}]}));
  const profiles=await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['123'],false);
  expect(profiles['123']).toMatchObject({solo:{tier:'GOLD',lp:1200},survival:{tier:'SILVER',lp:900}});
  expect(profiles['123'].survival).not.toHaveProperty('recordSummary');expect(query).toHaveBeenCalledTimes(1);
 });
 it('batches accounts, keeps both mode ratings distinct and preserves scoped title identity',async()=>{
  const definition=ACHIEVEMENT_DEFINITIONS.find(d=>d.type==='ONE_SHOT')!;
  const query=vi.fn(async(sql:string,_values?:readonly unknown[])=>({rows:sql.includes('WITH outcomes')?[{player_id:'human',count:10,best:1,wins:3,streak:2}]:[{player_id:'human',account:'123',gd_username:'Player',solo_tier:'GOLD',solo_lp:null,promotion_source_tier:'SILVER',promotion_target_tier:'GOLD',promotion_games_required:5,promotion_wins_required:3,promotion_results:['WIN'],survival_tier:'SILVER',survival_lp:900,title_id:definition.id,title_level:0}]}));
  const profiles=await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['123','123','-1','0','name']);
  expect(profiles['123']).toMatchObject({solo:{tier:'GOLD',lp:null,promotion:{results:['WIN']}},survival:{tier:'SILVER',lp:900,recordSummary:{completedMatches:10,wins:3,losses:7,winStreak:2}},equippedTitle:{achievementId:definition.id,scope:definition.scope}});
  expect(query.mock.calls[0]![1]).toEqual([['123']]);expect(query).toHaveBeenCalledTimes(2);
  for(const [sql]of query.mock.calls)expect(sql).not.toMatch(/\b(INSERT|UPDATE|DELETE|ALTER|TRUNCATE)\b/);
  expect(profiles['123']).not.toHaveProperty('hiddenMmr');
 });
 it('does not query for invalid/bot identities or reveal invalid titles',async()=>{
  const query=vi.fn();expect(await readPublicPlayerProfiles({query} as unknown as SqlExecutor,['-1','0','2147483648'])).toEqual({});expect(query).not.toHaveBeenCalled();
  expect(publicAchievementTitle(null,0)).toBeNull();expect(publicAchievementTitle('unknown',0)).toBeNull();
  const progressive=ACHIEVEMENT_DEFINITIONS.find(d=>d.type==='PROGRESSIVE')!;expect(publicAchievementTitle(progressive.id,0)).toBeNull();
 });
});
