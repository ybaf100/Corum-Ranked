import { IsBoolean, IsIn, IsString, Length } from "class-validator";
import {
  DEBUG_BOT_BAN_MODES,
  DEBUG_BOT_DIFFICULTIES,
  DEBUG_BOT_SCENARIOS,
  type DebugBotBanMode,
  type DebugBotDifficulty,
  type DebugBotScenario,
} from "./debug-bot.types.js";

export class CreateDebugBotMatchDto {
  @IsString()
  @Length(1, 128)
  public password!: string;

  @IsIn(DEBUG_BOT_DIFFICULTIES)
  public difficulty!: DebugBotDifficulty;

  @IsIn(DEBUG_BOT_SCENARIOS)
  public scenario!: DebugBotScenario;

  @IsIn(DEBUG_BOT_BAN_MODES)
  public botBan!: DebugBotBanMode;

  @IsBoolean()
  public sendDiscordEvents!: boolean;
}
