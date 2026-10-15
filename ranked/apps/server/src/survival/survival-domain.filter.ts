import { ArgumentsHost, Catch, type ExceptionFilter, HttpStatus, Logger } from "@nestjs/common";
import { RankedDomainError } from "@corum-ranked/rules";

/** Survival protocol conflicts are permanent responses, not transport failures.
 * Scope this boundary to Survival so Solo exception behavior stays unchanged. */
@Catch(RankedDomainError)
export class SurvivalDomainFilter implements ExceptionFilter {
  public catch(error:RankedDomainError,host:ArgumentsHost):void {
    const conflict=["INVALID_MATCH_TRANSITION","INVALID_ROUND_TRANSITION","ROUND_ALREADY_FINISHED",
      "ATTEMPT_ALREADY_ACTIVE","ATTEMPT_START_WINDOW_CLOSED"].includes(error.code);
    const invalidInput=["INVALID_MAP","INVALID_ATTEMPT","INVALID_DEATHMATCH","INVALID_BAN"].includes(error.code);
    const status=conflict?HttpStatus.CONFLICT:invalidInput?HttpStatus.BAD_REQUEST:HttpStatus.INTERNAL_SERVER_ERROR;
    if(status===HttpStatus.INTERNAL_SERVER_ERROR)Logger.error(error.stack??error.message,"SurvivalDomainFilter");
    host.switchToHttp().getResponse<{status(code:number):{json(value:unknown):void}}>()
      .status(status).json({statusCode:status,code:error.code,message:error.message});
  }
}
