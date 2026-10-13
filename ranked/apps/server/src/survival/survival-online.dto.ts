import { IsString,IsUUID,Length } from "class-validator";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
export class FillSurvivalBotsDto extends EnvironmentRecheckDto {
  @IsUUID() public requestId!:string;
  @IsString() @Length(1,128) public password!:string;
}
// Human event validation is identical to M4. The server supplies participantId;
// no public request can select a different account, map or award.
export { SurvivalDebugEventDto as SurvivalOnlineEventDto } from "./survival-debug.dto.js";
