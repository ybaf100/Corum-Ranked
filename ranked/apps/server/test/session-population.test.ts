import 'reflect-metadata';
import {PGlite} from '@electric-sql/pglite';
import {it,expect,vi} from 'vitest';
import {SessionService} from '../src/session/session.service.js';
import {PgliteDatabase} from './pglite-database.js';
const id=(n:number)=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
function service(db:any,clock:any){return new SessionService(db,{} as any,clock,{} as any,{getSnapshot:()=>({client:{audio:{resources:[]}}})} as any,{} as any);}
it('counts unique live human players, excludes bots/invalid IDs/dead leases, and caches/coalesces only on demand',async()=>{
 const pg=new PGlite(),db=new PgliteDatabase(pg);let now=Date.parse('2026-10-25T00:00:00Z');
 try{
  await pg.exec(`CREATE TABLE ranked_players(id uuid PRIMARY KEY,gd_account_id bigint,is_bot boolean);
   CREATE TABLE ranked_sessions(player_id uuid,revoked_at timestamptz,expires_at timestamptz,last_heartbeat_at timestamptz);`);
  for(let n=1;n<=5;n++)await db.query('INSERT INTO ranked_players VALUES($1,$2,$3)',[id(n),n===5?0:n,n===4]);
  const add=async(n:number,heartbeat=now,expires=now+3600000,revoked:string|null=null)=>db.query('INSERT INTO ranked_sessions VALUES($1,$2,$3,$4)',[id(n),revoked,new Date(expires).toISOString(),new Date(heartbeat).toISOString()]);
  await add(1);await add(1);await add(2,now,now);await add(3,now-90000);await add(4);await add(5);
  const s=service(db,{now:()=>new Date(now)}),spy=vi.spyOn(db,'query');
  expect(await Promise.all([s.population(),s.population()])).toEqual([{offline:2,online:1,sampledAt:new Date(now).toISOString()},{offline:2,online:1,sampledAt:new Date(now).toISOString()}]);
  expect(spy).toHaveBeenCalledTimes(1);await add(2);const calls=spy.mock.calls.length;
  expect(await s.population()).toMatchObject({offline:2,online:1});expect(spy).toHaveBeenCalledTimes(calls);
  now+=5001;expect(await s.population()).toMatchObject({offline:1,online:2});
  const reads=spy.mock.calls.length;now+=300000;await Promise.resolve();expect(spy).toHaveBeenCalledTimes(reads);
  expect(await s.population()).toMatchObject({offline:3,online:0});
 }finally{await pg.close();}
},30000);
it('adds counts only when a valid lobby heartbeat requests them; optional failures never break admission',async()=>{
 let admitted=true;const db={transaction:async(f:any)=>f({query:async()=>({rows:[],rowCount:admitted?1:0})})};
 const s=service(db,{now:()=>new Date('2026-10-25T00:00:00Z')}),read=vi.spyOn(s,'population').mockResolvedValue({offline:4,online:1,sampledAt:'now'});
 const session={playerId:id(1),sessionId:id(2)} as any;
 expect(await s.heartbeat(session)).not.toHaveProperty('population');expect(read).not.toHaveBeenCalled();
 expect(await s.heartbeat(session,true)).toHaveProperty('population.online',1);expect(read).toHaveBeenCalledTimes(1);
 read.mockRejectedValueOnce(new Error('optional display unavailable'));expect(await s.heartbeat(session,true)).toHaveProperty('sessionLeaseSeconds',90);
 admitted=false;await expect(s.heartbeat(session,true)).rejects.toThrow('expired');expect(read).toHaveBeenCalledTimes(2);
});
