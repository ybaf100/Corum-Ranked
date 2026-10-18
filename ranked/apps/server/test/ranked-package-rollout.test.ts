import 'reflect-metadata';
import {describe,it,expect,vi} from 'vitest';
import {RankedModUpdateService} from '../src/config/ranked-mod-update.service.js';
import {REQUIRED_RANKED_CLIENT_VERSION as target,PREVIOUS_RANKED_CLIENT_VERSION as previous} from '../src/session/client-version.js';
import {rankedModDownloadUrl} from '../src/config/ranked-mod-updates.js';
import {environmentFixture} from './fixtures.js';
const base='https://downloads.example.org';
describe('public immutable packages gate new admission',()=>{
 it('keeps the previous clients admitted until both packages exist, then promotes once without reversing',async()=>{
  let debug=false;
  const remote=vi.fn<typeof fetch>(async(url,init)=>{
   const channel=String(url).includes('/debug-')?'debug':'release';
   if(init?.method==='HEAD')return new Response(null,{status:channel==='debug'&&!debug?404:200,headers:{'content-length':'12345'}});
   const version=String(url).includes(previous)?previous:target;
   return Response.json({[channel]:{version,sha256:'a'.repeat(64),sizeBytes:12345,downloadUrl:rankedModDownloadUrl(base,channel,version)}});
  });
  const service=new RankedModUpdateService({...environmentFixture(),rankedDownloadBaseUrl:base},remote);
  expect(service.currentRequiredVersion()).toBe(previous);expect(service.admissionVersion(previous,'release')).toBe(previous);
  expect(service.admissionVersion(target,'release')).toBe(target); // exact installed mod still checked at admission
  expect((await service.getPublishedClientUpdates()).release?.version).toBe(previous);
  expect(service.currentRequiredVersion()).toBe(previous);
  debug=true;vi.spyOn(Date,'now').mockReturnValue(Date.now()+21000);
  try{expect((await service.getPublishedClientUpdates()).release?.version).toBe(target);expect(service.currentRequiredVersion()).toBe(target);
   debug=false;expect((await service.getPublishedClientUpdates()).debug?.version).toBe(target);expect(service.currentRequiredVersion()).toBe(target);
  }finally{vi.restoreAllMocks();}
 });
 it('does not promote a descriptor whose package byte count is wrong',async()=>{
  const remote=vi.fn<typeof fetch>(async(url,init)=>init?.method==='HEAD'?new Response(null,{headers:{'content-length':'1'}}):Response.json({[String(url).includes('/debug-')?'debug':'release']:{version:String(url).includes(previous)?previous:target,sha256:'a'.repeat(64),sizeBytes:12345}}));
  const service=new RankedModUpdateService({...environmentFixture(),rankedDownloadBaseUrl:base},remote);
  await service.getPublishedClientUpdates();expect(service.currentRequiredVersion()).toBe(previous);
 });
});
