import { Type } from "class-transformer";
import { IsBoolean, IsIn, IsString, Length } from "class-validator";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
import type {
  DebugBotBan,
  DebugBotDifficulty,
  DebugBotScenario,
} from "../queue/queue.service.js";

export const DEBUG_BOT_DIFFICULTIES: readonly DebugBotDifficulty[] = ["EASY", "NORMAL", "HARD"];
export const DEBUG_BOT_SCENARIOS: readonly DebugBotScenario[] = [
  "NORMAL_MATCH",
  "FORCE_BOT_1_CLEAR",
  "FORCE_BOT_2_CLEARS",
  "TRIGGER_LAST_ATTEMPT",
  "TRIGGER_ROUND_DRAW",
  "TRIGGER_ROUND_3",
  "TRIGGER_DEATHMATCH",
];
export const DEBUG_BOT_BANS: readonly DebugBotBan[] = ["RANDOM", "NO_BAN"];

export class CreateDebugBotMatchDto extends EnvironmentRecheckDto {
  @IsString()
  @Length(1, 128)
  public password!: string;

  @IsIn(DEBUG_BOT_DIFFICULTIES)
  public difficulty!: DebugBotDifficulty;

  @IsIn(DEBUG_BOT_SCENARIOS)
  public scenario!: DebugBotScenario;

  @IsIn(DEBUG_BOT_BANS)
  public botBan!: DebugBotBan;

  @Type(() => Boolean)
  @IsBoolean()
  public sendDiscordEvents!: boolean;
}
