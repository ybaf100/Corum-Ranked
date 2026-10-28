// Compare both actual provider implementations against the same public-API
// fixture. These are synthetic search distributions, not production telemetry.
import {execFileSync} from 'node:child_process';
import {writeFileSync,unlinkSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {gzipSync} from 'node:zlib';
import ts from 'typescript';
import {SeededRandom} from '@corum-ranked/rules';
const ranked=resolve(dirname(fileURLToPath(import.meta.url)),'..'),repo=resolve(ranked,'..');
const baseline=process.env.CORUM_MAP_BASELINE_SHA??'1370eadba55f93672ecbfa350cb390a8e8b3548f';
const old=execFileSync('git',['show',`${baseline}:ranked/apps/server/src/survival/survival-map.provider.ts`],{cwd:repo,encoding:'utf8'});
const path=resolve(ranked,'apps/server/dist/survival/survival-map-baseline.js');
writeFileSync(path,ts.transpileModule(old,{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ES2022,experimentalDecorators:true}}).outputText);
const geometry=gzipSync('kS38,1;1,1,2,30,3,10;').toString('base64url');
const record=id=>`1:${id}:18:4:17::43:0:25::15:2:8:10:9:30`;
function fixture(metrics){return async(url,init)=>{
 const f=new URLSearchParams(String(init?.body));
 if(String(url).endsWith('getGJLevels21.php')){
  ++metrics.searches;const page=Number(f.get('page')),type=f.get('type');
  metrics.sources[type]=(metrics.sources[type]??0)+1;
  // Dense recent page zero; deeper liked/awarded pages expose older ID ranges.
  const base=page===0||type==='4'?120000000:type==='2'?80000000:50000000;
  const ids=Array.from({length:10},(_,i)=>base+page*10+i);
  return new Response(`${ids.map(record).join('|')}###10000:${page*10}:10#hash`);
 }
 ++metrics.downloads;const id=Number(f.get('levelID'));return new Response(`${record(id)}:5:1:4:${geometry}#hash`);
};}
try{
 const {GdSurvivalMapProvider:Before}=await import(pathToFileURL(path));
 const {GdSurvivalMapProvider:After}=await import('../apps/server/dist/survival/survival-map.provider.js');
 const result={baseline,fixture:'dense recent page zero, deep liked/awarded older ID strata',samples:600,before:null,after:null};
 for(const [name,Provider]of [['before',Before],['after',After]]){
  const metrics={searches:0,downloads:0,sources:{},bands:{'40m-70m':0,'70m-100m':0,'100m+':0},selectedSources:{},selectedPages:{zero:0,deep:0}};
  for(let seed=1;seed<=result.samples;seed++){
   const map=await new Provider(new SeededRandom(seed),fixture(metrics)).select({group:'RED_BRONZE',phase:'ROUND_1',minimumLevelId:40000000,excludedLevelIds:[]});
   ++metrics.bands[map.levelId<70000000?'40m-70m':map.levelId<100000000?'70m-100m':'100m+'];
   if(map.sourceSearchType)metrics.selectedSources[map.sourceSearchType]=(metrics.selectedSources[map.sourceSearchType]??0)+1;
   if(map.sourcePage!==undefined)++metrics.selectedPages[map.sourcePage===0?'zero':'deep'];
  }result[name]=metrics;
 }
 const output=JSON.stringify(result,null,2);if(process.argv[2])writeFileSync(process.argv[2],output+'\n');console.log(output);
 if(result.after.bands['100m+']>=result.before.bands['100m+'])throw Error('Fixture recent bias did not improve');
}finally{unlinkSync(path);}
