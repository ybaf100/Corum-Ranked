import { IsBoolean,IsString,IsUUID,Length,IsOptional,IsInt,Min,Max } from "class-validator";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
export class FillSurvivalBotsDto extends EnvironmentRecheckDto {
  @IsUUID() public requestId!:string;
  @IsOptional() @IsInt() @Min(0) @Max(63) public botCount?:number;
  @IsString() @Length(1,128) public password!:string;
  @IsOptional() @IsBoolean() public forceDeathMatch?:boolean;
}
// Human event validation is identical to M4. The server supplies participantId;
// no public request can select a different account, map or award.
export { SurvivalDebugEventDto as SurvivalOnlineEventDto } from "./survival-debug.dto.js";
