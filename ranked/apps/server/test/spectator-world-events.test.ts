import {describe,it,expect} from 'vitest';
import {parseSpectatorKeyframe} from '../src/match/spectator-live.protocol.js';
const pose={x:10,y:120,rotation:0,mode:'SPIDER',mini:false,upsideDown:false,flipped:false,dual:false,dead:false,finished:false};
const event={ordinal:1,objectIndex:3,objectId:1912,originX:100,originY:200,tick:12,elapsedMs:50,outputs:[{group:7,triggerId:17,controlId:23,ordered:false,delay:0,remap:[1,2]}]};
const simulation={elapsedMs:50,tick:12,started:true,resetDelay:false};
describe('production native world-event wire validation',()=>{
 it('preserves selected native Random output, source clock and the exact Follow Y ring independently of percentage',()=>{
  const followHistory={index:233,remainder:.007,y:Array.from({length:200},(_,i)=>i*3.5)};
  const parsed=parseSpectatorKeyframe({...pose,simulation,randomEvents:[event],followHistory,audio:{trackHash:'a'.repeat(64),positionMs:162338},progress:5});
  expect(parsed).toMatchObject({simulation,randomEvents:[event],followHistory,audio:{positionMs:162338},progress:5});
  expect(parseSpectatorKeyframe(pose).simulation).toBeUndefined();
 });
 it('preserves two native Spider snaps with a net unchanged gravity and rejects forged chains',()=>{
  const first={type:'SPIDER_SNAP',fromX:0,fromY:100,toX:1,toY:1000,beforeMode:'SPIDER',afterMode:'SPIDER',beforeGravity:false,afterGravity:true};
  const second={...first,fromX:1,fromY:1000,toX:10,toY:120,beforeGravity:true,afterGravity:false};
  const discontinuity={type:'SPIDER_SNAP',fromX:0,fromY:100,toX:10,toY:120,nativeEvents:[first,second]};
  expect(parseSpectatorKeyframe({...pose,discontinuity}).discontinuity?.nativeEvents).toHaveLength(2);
  expect(()=>parseSpectatorKeyframe({...pose,discontinuity:{...discontinuity,nativeEvents:[{...first,afterGravity:false},second]}})).toThrow();
  expect(()=>parseSpectatorKeyframe({...pose,discontinuity:{...discontinuity,nativeEvents:Array(9).fill(first)}})).toThrow();
 });
 it('round-trips bounded diagnostic event identity without treating it as movement proof',()=>{
  const trace={event:1,tick:12,step:3,elapsedMs:71295,callback:'PORTAL'};
  const native={type:'TELEPORT',fromX:25885.127,fromY:4044,toX:25894.729,toY:5439,beforeMode:'SHIP',afterMode:'SHIP',beforeGravity:false,afterGravity:false,trace};
  const discontinuity={type:'TELEPORT',fromX:native.fromX,fromY:native.fromY,toX:native.toX,toY:native.toY,nativeEvents:[native]};
  // Protocol fixture only: this does not attest an actual Entropie callback.
  expect(parseSpectatorKeyframe({...pose,mode:'SHIP',discontinuity}).discontinuity?.nativeEvents?.[0]?.trace).toEqual(trace);
  for(const bad of [{...trace,event:0},{...trace,tick:-1},{...trace,elapsedMs:Infinity},{...trace,callback:'POSITION_DELTA'}])
    expect(()=>parseSpectatorKeyframe({...pose,discontinuity:{...discontinuity,nativeEvents:[{...native,trace:bad}]}})).toThrow();
 });
 it('rejects partial, nonfinite, oversized, foreign-index and unclocked event state',()=>{
  for(const randomEvents of [[{...event,objectIndex:4294967296}],[{...event,elapsedMs:NaN}],[event,event],Array(17).fill(event),[{...event,outputs:[{...event.outputs[0],remap:Array(33).fill(1)}]}]])expect(()=>parseSpectatorKeyframe({...pose,simulation,randomEvents})).toThrow();
  expect(()=>parseSpectatorKeyframe({...pose,randomEvents:[event]})).toThrow();
  for(const followHistory of [{index:-1,remainder:0,y:Array(200).fill(0)},{index:0,remainder:0,y:Array(199).fill(0)},{index:0,remainder:0,y:Array(200).fill(Infinity)}])expect(()=>parseSpectatorKeyframe({...pose,followHistory})).toThrow();
 });
});
