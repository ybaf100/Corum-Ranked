import 'reflect-metadata';
import {describe,it,expect,vi} from 'vitest';
import {DebugUpdateChannelController} from '../src/config/debug-update-channel.controller.js';
import type {ServerEnvironment} from '../src/config/server-environment.js';
import {SERVER_ENVIRONMENT} from '../src/config/server-environment.js';
import {Test} from '@nestjs/testing';
import {ValidationPipe} from '@nestjs/common';
describe('Debug update channel authorization',()=>{
 const rig=()=>new DebugUpdateChannelController({debugBot:{password:'test-only-password'},debugSpectate:null} as unknown as ServerEnvironment);
 it('requires the configured secret and never returns it or creates a game session',()=>{
  const gate=rig();expect(()=>gate.verify({password:'wrong'},{ip:'test'})).toThrow('Incorrect password');
  const result=gate.verify({password:'test-only-password'},{ip:'test'});expect(result).toEqual({authorized:true,channel:'debug'});expect(JSON.stringify(result)).not.toContain('test-only-password');
 });
 it('rejects invalid input and missing Debug configuration',()=>{
  for(const body of [null,{},'text',{password:''},{password:'x'.repeat(129)}])expect(()=>rig().verify(body,{ip:'test'})).toThrow('Invalid password');
  const gate=new DebugUpdateChannelController({debugBot:null,debugSpectate:null} as unknown as ServerEnvironment);expect(()=>gate.verify({password:'test'},{ip:'test'})).toThrow('unavailable');
 });
 it('bounds attempts per address and expires inactive buckets without timers or database calls',()=>{
  const now=vi.spyOn(Date,'now').mockReturnValue(1000);const gate=rig();
  try{for(let i=0;i<5;i++)expect(()=>gate.verify({password:'bad'},{ip:'test'})).toThrow('Incorrect');expect(()=>gate.verify({password:'test-only-password'},{ip:'test'})).toThrow('Try again');
   expect(gate.verify({password:'test-only-password'},{ip:'other'}).authorized).toBe(true);now.mockReturnValue(61001);expect(gate.verify({password:'test-only-password'},{ip:'test'}).authorized).toBe(true);
  }finally{now.mockRestore();}
 });
 it('accepts a pre-session HTTP request with the production validation policy and rejects wrong passwords',async()=>{
  const module=await Test.createTestingModule({controllers:[DebugUpdateChannelController],providers:[
   {provide:SERVER_ENVIRONMENT,useValue:{debugBot:{password:'http-fixture-password'},debugSpectate:null}},
  ]}).compile();
  const app=module.createNestApplication();app.useLogger(false);
  app.useGlobalPipes(new ValidationPipe({transform:true,whitelist:true,forbidNonWhitelisted:true}));
  try{
   await app.listen(0,'127.0.0.1');const url=await app.getUrl();
   const send=(password:string)=>fetch(`${url}/api/ranked/updates/channel/debug`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password})});
   const denied=await send('wrong');expect(denied.status).toBe(403);
   const allowed=await send('http-fixture-password');expect(allowed.status).toBe(201);expect(allowed.headers.get('cache-control')).toBe('no-store');
   expect(await allowed.json()).toEqual({authorized:true,channel:'debug'});
  }finally{await app.close();}
 });
});
