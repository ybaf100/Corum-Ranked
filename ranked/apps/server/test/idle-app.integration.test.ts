import 'reflect-metadata';
import { appendFileSync } from 'node:fs';
import { readFile,readdir } from 'node:fs/promises';
import { PGlite } from '@electric-sql/pglite';
import { Test } from '@nestjs/testing';
import { expect,it,vi } from 'vitest';
import { DEFAULT_SURVIVAL_CONFIG } from '@corum-ranked/rules';
import { AppModule } from '../src/app.module.js';
import { DatabaseService } from '../src/database/database.service.js';
import { SERVER_ENVIRONMENT } from '../src/config/server-environment.js';
import { AppsScriptRankedConfigSource,RANKED_CONFIG_SOURCE, } from '../src/config/ranked-config.source.js';
import { SURVIVAL_CONFIG_SOURCE } from '../src/config/survival-config.service.js';
import { CSMP_TIER_SOURCE } from '../src/config/csmp-tier.source.js';
import { DISCORD_TRANSPORT } from '../src/relay/discord-transport.js';
import { HealthController } from '../src/health.controller.js';
import { SessionService } from '../src/session/session.service.js';
import { TokenService } from '../src/common/token.service.js';
import { REQUIRED_RANKED_CLIENT_VERSION } from '../src/session/client-version.js';
import { SurvivalQueueService } from '../src/survival/survival-queue.service.js';
import type { RankedSessionContext } from '../src/session/session.types.js';
import { PgliteDatabase } from './pglite-database.js';
import { configDocumentFixture,environmentFixture } from './fixtures.js';
import { countSql } from './sql-counter.js';

it('measures actual full-app SQL with no players, games, queues or relay work for six minutes',async()=>{
 const pg=new PGlite();const db=new PgliteDatabase(pg);
 const migrations=new URL('../../../migrations/',import.meta.url);
 for(const f of (await readdir(migrations)).filter(f=>f.endsWith('.sql')).sort())await pg.exec(await readFile(new URL(f,migrations),'utf8'));
 const difficulty={mmrOffset:0,qualifyingChance:.6,clearChance:.1,paceMultiplier:1};
 const env={...environmentFixture(),debugBot:{password:'isolated-fixture',tickMs:125,attemptDelayMs:200,difficulties:{EASY:difficulty,NORMAL:difficulty,HARD:difficulty}},
  discordRelay:{webhookUrl:'https://example.invalid',pollMs:1000,batchSize:10,requestTimeoutMs:1000,leaseMs:30000,retryBaseMs:1000,retryMaximumMs:30000,maximumAttempts:3}};
 const config=configDocumentFixture(),survival={generation:'idle-test',generatedAt:config.generatedAt,config:{...DEFAULT_SURVIVAL_CONFIG,enabled:true}};
 let refreshSequence=0;const refreshed=<T extends {generatedAt:string}>(document:T):T=>({...document,generatedAt:new Date(Date.parse(document.generatedAt)+ ++refreshSequence*1000).toISOString()});
 const counter=countSql(db,()=>false);
 // schemaStatus is kept a real SQL probe, just like production /ready.
 Object.assign(db,{schemaStatus:async()=>{await db.query('SELECT 1');return {ready:true,missing:[]};}});
 vi.useFakeTimers({toFake:['Date','setInterval','clearInterval']});
 // Mock only HTTP, not the production config source or its DB-backed metadata
 // resolver: otherwise its once-per-refresh cache query escapes the measurement.
 vi.stubGlobal('fetch',async(input:string|URL|Request)=>{
  if(String(input).startsWith(env.rankedConfigUrl))return new Response(JSON.stringify({ok:true,action:'ranked_config',data:refreshed(config)}));
  if(String(input).includes('boomlings.com'))return new Response('-1');
  return new Response('',{status:404});
 });
 const module=await Test.createTestingModule({imports:[AppModule]})
  .overrideProvider(DatabaseService).useValue(db).overrideProvider(SERVER_ENVIRONMENT).useValue(env)
  .overrideProvider(RANKED_CONFIG_SOURCE).useValue(new AppsScriptRankedConfigSource(env,db))
  .overrideProvider(SURVIVAL_CONFIG_SOURCE).useValue({fetchConfig:async()=>refreshed(survival)})
  .overrideProvider(CSMP_TIER_SOURCE).useValue({fetchCurrentTier:async()=> 'NONE'})
  .overrideProvider(DISCORD_TRANSPORT).useValue({send:vi.fn()}).compile();
 const app=module.createNestApplication();app.useLogger(false);
 try{
  await app.init();const startup=counter.metrics.queries;counter.reset();
  await vi.advanceTimersByTimeAsync(360000);
  const idle=counter.metrics.queries;if(process.env['IDLE_SQL_METRICS_FILE'])appendFileSync(process.env['IDLE_SQL_METRICS_FILE'],JSON.stringify({baseline:process.env['IDLE_BASELINE']==='true',startup,seconds:360,idle,queriesPerSecond:idle/360})+'\n');console.log('IDLE_APP_SQL',JSON.stringify({startup,seconds:360,idle,queriesPerSecond:idle/360}));
  if(process.env['IDLE_BASELINE']!=='true')expect(idle).toBe(0);
  const health=app.get(HealthController);health.health();expect(counter.metrics.queries).toBe(idle);
  await health.ready();expect(counter.metrics.queries).toBeGreaterThan(idle);
  if(process.env['IDLE_BASELINE']==='true')return;
  const installedMods=[{id:'hwanhee1.corum_ranked',version:REQUIRED_RANKED_CLIENT_VERSION,enabled:true,loaded:true,internal:false,system:false},
   {id:'geode.node-ids',version:'v1.23.3',enabled:true,loaded:true,internal:false,system:false},
   {id:'syzzi.click_between_frames',version:'v1.5.0',enabled:true,loaded:true,internal:false,system:false,settings:{'soft-toggle':false,'click-on-steps':false,'physics-bypass':false}}];
  // A new login uses the ordinary service after the six-minute idle interval.
  const loggedIn=await app.get(SessionService).create({gdAccountId:'887766',gdUsername:'IdleResume',clientVersion:REQUIRED_RANKED_CLIENT_VERSION,clientBuildChannel:'release',clientInstanceId:'00000000-0000-4000-8000-000000887766',installedMods});
  const row=(await db.query<{id:string;player_id:string}>('SELECT id,player_id FROM ranked_sessions WHERE token_hash=$1',[app.get(TokenService).hash(loggedIn.sessionToken)])).rows[0]!;
  const session:RankedSessionContext={sessionId:row.id,playerId:row.player_id,gdAccountId:'887766',gdUsername:'IdleResume',displayedTier:'UNRANKED',hiddenMmr:0,placementGames:0,clientVersion:REQUIRED_RANKED_CLIENT_VERSION,clientBuildChannel:'release'};
  const queue=app.get(SurvivalQueueService);expect((await queue.join(session,{installedMods})).status).toBe('QUEUED');
  counter.reset();await vi.advanceTimersByTimeAsync(500);expect(counter.metrics.queries).toBeGreaterThan(0);
  await queue.leave(session);await vi.advanceTimersByTimeAsync(1000);counter.reset();
  await vi.advanceTimersByTimeAsync(360000);expect(counter.metrics.queries).toBe(0);
 }finally{await app.close();vi.useRealTimers();vi.unstubAllGlobals();counter.stop();await pg.close();}
},180000);
