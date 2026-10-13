import { HttpException,Inject,Injectable } from "@nestjs/common";
import { DATABASE,type DatabasePort } from "../database/database.port.js";
import { SERVER_CLOCK,type ServerClock } from "../common/runtime.module.js";
@Injectable()
export class SurvivalRateService {
  public constructor(@Inject(DATABASE)private readonly db:DatabasePort,@Inject(SERVER_CLOCK)private readonly clock:ServerClock){}
  public async check(playerId:string,bucket:"queue"|"fill"|"event"|"state"):Promise<void>{
    const limit={queue:60,fill:5,event:600,state:600}[bucket];
    const now=this.clock.now();const start=new Date(Math.floor(now.getTime()/60000)*60000).toISOString();
    const row=(await this.db.query<{requests:number}>(`INSERT INTO sur_request_rates(player_id,bucket,window_started_at,requests)
      VALUES($1,$2,$3,1) ON CONFLICT(player_id,bucket) DO UPDATE SET
      requests=CASE WHEN sur_request_rates.window_started_at=EXCLUDED.window_started_at THEN sur_request_rates.requests+1 ELSE 1 END,
      window_started_at=EXCLUDED.window_started_at RETURNING requests`,[playerId,bucket,start])).rows[0]!;
    if(Number(row.requests)>limit)throw new HttpException({code:"SURVIVAL_RATE_LIMIT",message:"Too many Survival requests",retryAfterSeconds:60-now.getSeconds()},429);
  }
}
