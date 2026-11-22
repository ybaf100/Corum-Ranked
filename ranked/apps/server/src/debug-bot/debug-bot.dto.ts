import { Type } from "class-transformer";
import { ArrayMaxSize, IsArray, IsBoolean, IsIn, IsOptional, IsString, Length, ValidateNested } from "class-validator";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
import type {
  DebugBotBan,
  DebugBotDifficulty,
  DebugBotScenario,
} from "../queue/queue.service.js";

export const DEBUG_BOT_DIFFICULTIES: readonly DebugBotDifficulty[] = ["EASY", "NORMAL", "HARD"];
export const DEBUG_BOT_SCENARIOS: readonly DebugBotScenario[] = [
  "NORMAL_MATCH",
  "FORCE_WIN",
  "FORCE_LOSE",
  "FORCE_BOT_1_CLEAR",
  "FORCE_BOT_3_CLEARS",
  "TRIGGER_LAST_ATTEMPT",
  "TRIGGER_ROUND_DRAW",
  "TRIGGER_ROUND_3",
  "TRIGGER_DEATHMATCH",
];
export const DEBUG_BOT_BANS: readonly DebugBotBan[] = ["RANDOM", "NO_BAN"];

/** Designated maps by canonical level id: up to 3 rounds in order ("" =
 * drawn at random) and an optional Death Match map ("" or absent = drawn). */
export class FixedMapsDto {
  @IsArray()
  @ArrayMaxSize(3)
  @IsString({ each: true })
  @Length(0, 40, { each: true })
  public rounds!: string[];

  @IsOptional()
  @IsString()
  @Length(0, 40)
  public deathmatch?: string;
}

export class DebugChallengeSendDto extends EnvironmentRecheckDto {
  @IsString()
  @Length(1, 128)
  public password!: string;

  @IsString()
  @Length(1, 64)
  public targetPlayerId!: string;

  @ValidateNested()
  @Type(() => FixedMapsDto)
  public fixedMaps!: FixedMapsDto;
}

export const fixedMapRequestOf = (dto: FixedMapsDto | undefined) => dto
  ? { rounds: dto.rounds.map((id) => id.trim() || null), deathmatch: dto.deathmatch?.trim() || null }
  : null;

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

  @IsOptional()
  @ValidateNested()
  @Type(() => FixedMapsDto)
  public fixedMaps?: FixedMapsDto;
}
