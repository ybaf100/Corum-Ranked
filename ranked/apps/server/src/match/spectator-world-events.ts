import {BadRequestException} from '@nestjs/common';
export interface SpectatorRandomEvent {readonly ordinal:number;readonly objectIndex:number;readonly objectId:number;readonly originX:number;readonly originY:number;readonly tick:number;readonly elapsedMs:number;readonly outputs:readonly {readonly group:number;readonly triggerId?:number;readonly controlId?:number;readonly ordered:boolean;readonly delay:number;readonly remap:readonly number[]}[];}
export function parseSpectatorRandomEvents(value:unknown):readonly SpectatorRandomEvent[]{
 const fail=():never=>{throw new BadRequestException('Invalid spectator random events');};
 const object=(v:unknown):Record<string,unknown>=>v&&typeof v==='object'&&!Array.isArray(v)?v as Record<string,unknown>:fail();
 const number=(v:unknown,min:number,max:number,integer=false):number=>typeof v==='number'&&Number.isFinite(v)&&v>=min&&v<=max&&(!integer||Number.isSafeInteger(v))?v:fail();
 if(!Array.isArray(value)||value.length>16)return fail();let previous=0;
 return value.map(raw=>{const e=object(raw),ordinal=number(e.ordinal,1,1000000,true);if(ordinal<=previous)return fail();previous=ordinal;
   if(!Array.isArray(e.outputs)||e.outputs.length>4)return fail();
   return {ordinal,objectIndex:number(e.objectIndex,0,1000000,true),objectId:number(e.objectId,1,1000000,true),originX:number(e.originX,-10000000,10000000),originY:number(e.originY,-10000000,10000000),tick:number(e.tick,0,1000000000,true),elapsedMs:number(e.elapsedMs,0,86400000),
    outputs:e.outputs.map(raw=>{const s=object(raw);if(typeof s.ordered!=='boolean'||!Array.isArray(s.remap)||s.remap.length>32)return fail();return {group:number(s.group,0,1000000,true),...(s.triggerId===undefined?{}:{triggerId:number(s.triggerId,-2147483648,2147483647,true)}),...(s.controlId===undefined?{}:{controlId:number(s.controlId,-2147483648,2147483647,true)}),ordered:s.ordered,delay:number(s.delay,0,600),remap:s.remap.map(v=>number(v,0,1000000,true))};})};
 });
}
