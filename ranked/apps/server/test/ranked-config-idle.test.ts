import { afterEach,expect,it,vi } from 'vitest';
import { AppsScriptRankedConfigSource } from '../src/config/ranked-config.source.js';
import { RankedConfigService } from '../src/config/ranked-config.service.js';
import { configDocumentFixture,environmentFixture } from './fixtures.js';
import type { DatabasePort,SqlExecutor } from '../src/database/database.port.js';
afterEach(()=>{vi.useRealTimers();vi.unstubAllGlobals();});
it('continues metadata TTL refresh without reading or writing PostgreSQL for the unchanged source document',async()=>{
 vi.useFakeTimers({toFake:['Date']});const initial=configDocumentFixture();let doc={...initial,maps:[{...initial.maps[0]!,title:'Stereo Madness'}]};
 const query=vi.fn(async()=>({rows:[],rowCount:0}));
 vi.stubGlobal('fetch',async()=>new Response(JSON.stringify({ok:true,action:'ranked_config',data:doc})));
 const source=new AppsScriptRankedConfigSource(environmentFixture(),{query});
 const first=await source.fetchConfig();expect(first.maps[0]!.metadata?.durationSeconds).toBe(89);
 expect(query).toHaveBeenCalledTimes(2); // one restart hydration, one initial cache write
 doc={...doc,generatedAt:new Date(Date.now()+1000).toISOString()};vi.setSystemTime(Date.now()+25*60*60*1000);
 const refreshed=await source.fetchConfig();expect(refreshed.maps[0]!.metadata?.durationSeconds).toBe(89);expect(query).toHaveBeenCalledTimes(2);
 doc={...doc,maps:[{...doc.maps[0]!,qualifyingPercent:60}]};await source.fetchConfig();expect(query).toHaveBeenCalledTimes(3);
});
it('cleans OFF once at startup and once per enabled-to-OFF transition, while metadata generations can change without SQL',async()=>{
 const initial=configDocumentFixture();let doc={...initial,operational:{...initial.operational,enabled:false}};
 const query=vi.fn(async()=>({rows:[],rowCount:0}));const db:DatabasePort={query,ping:async()=>true,transaction:async<T>(operation:(tx:SqlExecutor)=>Promise<T>)=>operation({query})};
 const service=new RankedConfigService({fetchConfig:async()=>structuredClone(doc)},environmentFixture(),db);
 await service.refresh();expect(query).toHaveBeenCalledTimes(1);
 doc={...doc,generation:'metadata-updated',operational:{...doc.operational,generation:'metadata-updated'},generatedAt:new Date().toISOString()};await service.refresh();expect(query).toHaveBeenCalledTimes(1);
 doc={...doc,operational:{...doc.operational,enabled:true}};await service.refresh();expect(query).toHaveBeenCalledTimes(1);
 doc={...doc,operational:{...doc.operational,enabled:false}};await service.refresh();expect(query).toHaveBeenCalledTimes(2);
});
