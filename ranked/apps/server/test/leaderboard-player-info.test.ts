import 'reflect-metadata';
import {describe,it,expect,vi} from 'vitest';
import {LeaderboardPlayerInfoController} from '../src/forum/leaderboard-player-info.controller.js';
import type {DatabasePort} from '../src/database/database.port.js';
import type {ForumRecordsService} from '../src/forum/forum-records.service.js';
describe('Leaderboard custom player information',()=>{
 function rig(known=true){
  const query=vi.fn(async()=>({rows:known?[{gd_username:'Player'}]:[]}));
  const read=vi.fn(async(account:string)=>({gdAccountId:account,records:[{provider:'GDKF',rank:4}]}));
  return {query,read,controller:new LeaderboardPlayerInfoController({query} as unknown as DatabasePort,{read} as unknown as ForumRecordsService)};
 }
 it('reads only cached information for the exact registered public account',async()=>{
  const r=rig();expect(await r.controller.read('123')).toMatchObject({gdAccountId:'123',gdUsername:'Player',records:[{rank:4}]});
  expect(r.query).toHaveBeenNthCalledWith(1,'SELECT gd_username FROM ranked_players WHERE gd_account_id=$1',['123']);expect(r.query).toHaveBeenCalledTimes(2);expect(r.read).toHaveBeenCalledExactlyOnceWith('123');
 });
 it('rejects malformed IDs and bots without a GD account before any query',async()=>{
  const r=rig();for(const id of ['0','-1','01','bot-01','123/other','2147483648','1.5'])await expect(r.controller.read(id)).rejects.toMatchObject({status:400});
  expect(r.query).not.toHaveBeenCalled();expect(r.read).not.toHaveBeenCalled();
 });
 it('does not create a profile or refresh an unknown account',async()=>{
  const r=rig(false);await expect(r.controller.read('999')).rejects.toMatchObject({status:404});expect(r.read).not.toHaveBeenCalled();
 });
});
