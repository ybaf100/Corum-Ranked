import { expect,it } from 'vitest';
import { Pool } from 'pg';
import { DatabaseService } from '../src/database/database.service.js';
import { environmentFixture } from './fixtures.js';
const url=process.env['SURVIVAL_TEST_DATABASE_URL'];
// Never terminate a remote backend. This test needs a disposable local service.
const local=url&&['localhost','127.0.0.1','::1'].includes(new URL(url).hostname);
it.runIf(Boolean(local))('retires idle pool connections, reconnects after a dropped backend, and preserves a committed transaction',async()=>{
 const database=new DatabaseService({...environmentFixture(),databaseUrl:url!});
 const admin=new Pool({connectionString:url!,max:1});
 const schema=`idle_pool_${process.pid}_${Date.now()}`;
 try{
  await database.query(`CREATE SCHEMA "${schema}"`);
  await database.query(`CREATE TABLE "${schema}".receipt(id integer PRIMARY KEY,value integer NOT NULL)`);
  await database.transaction(async tx=>{await tx.query(`INSERT INTO "${schema}".receipt VALUES(1,100)`);});
  const pid=(await database.query<{pid:number}>('SELECT pg_backend_pid() AS pid')).rows[0]!.pid;
  const pool=(database as unknown as {pool:Pool}).pool;
  const disconnected=new Promise<void>((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('idle error was not handled')),5000);pool.once('error',()=>{clearTimeout(timer);resolve();});});
  await admin.query('SELECT pg_terminate_backend($1)',[pid]);await disconnected;
  expect(await database.ping()).toBe(true);expect((await database.query(`SELECT value FROM "${schema}".receipt`)).rows).toEqual([{value:100}]);
  const retired=new Promise<void>((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('pool did not retire its idle connection')),35000);pool.once('remove',()=>{clearTimeout(timer);resolve();});});
  await retired;expect(pool.totalCount).toBe(0);
  expect((await database.query(`SELECT value FROM "${schema}".receipt`)).rows).toEqual([{value:100}]);
 }finally{await admin.query(`DROP SCHEMA IF EXISTS "${schema}" CASCADE`);await database.onApplicationShutdown();await admin.end();}
},45000);
