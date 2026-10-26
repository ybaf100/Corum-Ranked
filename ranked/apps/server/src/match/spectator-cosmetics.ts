import { BadRequestException } from '@nestjs/common';
export interface SpectatorAvatarCosmetics {
  readonly icons: readonly number[];
  readonly primary: readonly number[];
  readonly secondary: readonly number[];
  readonly glowColor: readonly number[];
  readonly glow: boolean;
  readonly trail: number;
}
export interface SpectatorCosmetics {
  readonly revision: number;
  readonly players: readonly SpectatorAvatarCosmetics[];
}
const record=(v:unknown):v is Record<string,unknown>=>typeof v==='object'&&v!==null&&!Array.isArray(v);
const ints=(v:unknown,n:number,min:number,max:number):number[]=>{
  if(!Array.isArray(v)||v.length!==n||v.some(x=>!Number.isSafeInteger(x)||x<min||x>max))
    throw new BadRequestException('INVALID_SPECTATOR_COSMETICS');
  return [...v] as number[];
};
export const parseSpectatorCosmetics=(v:unknown):SpectatorCosmetics=>{
  if(!record(v)||!Number.isSafeInteger(v.revision)||Number(v.revision)<1||Number(v.revision)>1_000_000||!Array.isArray(v.players)||v.players.length!==2)
    throw new BadRequestException('INVALID_SPECTATOR_COSMETICS');
  const players=v.players.map((p:unknown):SpectatorAvatarCosmetics=>{
    if(!record(p)||typeof p.glow!=='boolean'||!Number.isSafeInteger(p.trail)||Number(p.trail)<1||Number(p.trail)>1000)
      throw new BadRequestException('INVALID_SPECTATOR_COSMETICS');
    return {icons:ints(p.icons,8,1,10000),primary:ints(p.primary,3,0,255),secondary:ints(p.secondary,3,0,255),
      glowColor:ints(p.glowColor,3,0,255),glow:p.glow,trail:Number(p.trail)};
  });
  return {revision:Number(v.revision),players};
};
