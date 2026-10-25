import {BadRequestException,Body,Controller,ForbiddenException,Header,HttpException,Inject,Post,Req} from '@nestjs/common';
import {timingSafeEqual} from 'node:crypto';
import {SERVER_ENVIRONMENT,type ServerEnvironment} from './server-environment.js';
/** Update admission has no game session yet. Verify the configured Debug secret
 * without starting a match, reading Neon, or returning/logging credentials. */
@Controller('api/ranked/updates/channel')
export class DebugUpdateChannelController{
 private readonly attempts=new Map<string,{count:number;until:number}>();
 constructor(@Inject(SERVER_ENVIRONMENT)private readonly environment:ServerEnvironment){}
 @Post('debug') @Header('Cache-Control','no-store')
 verify(@Body()body:unknown,@Req()request:{ip?:string}){
  const now=Date.now(),key=request.ip??'unknown';
  for(const [ip,entry]of this.attempts)if(entry.until<=now)this.attempts.delete(ip);
  let entry=this.attempts.get(key);
  if(!entry){if(this.attempts.size>=1024)throw new HttpException('Try again later',429);entry={count:0,until:now+60_000};this.attempts.set(key,entry);}
  if(++entry.count>5)throw new HttpException('Try again later',429);
  const password=body&&typeof body==='object'&&'password'in body?body.password:null;
  if(typeof password!=='string'||password.length===0||password.length>128)throw new BadRequestException('Invalid password');
  const expected=this.environment.debugBot?.password??this.environment.debugSpectate?.password;
  if(!expected)throw new ForbiddenException('Debug channel is unavailable');
  const actualBytes=Buffer.from(password),expectedBytes=Buffer.from(expected);
  if(actualBytes.length!==expectedBytes.length||!timingSafeEqual(actualBytes,expectedBytes))throw new ForbiddenException('Incorrect password');
  return {authorized:true,channel:'debug'};
 }
}
