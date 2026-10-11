import { IsIn, IsInt, IsOptional, IsString, Length, Max, Min, Equals } from "class-validator";
export const ADMIN_ACTIONS = ["UNLOCK_ALL", "SET_LP", "SET_MMR", "SET_BOTH", "DELETE_BOTS", "RESET_HISTORY", "RESET_DB", "GET_AUDIO_VOLUMES", "SET_AUDIO_VOLUME"] as const;
export class AdminActionDto {
  @IsString() @Length(1,128) public password!: string;
  @IsIn(ADMIN_ACTIONS) public action!: typeof ADMIN_ACTIONS[number];
  @IsOptional() @IsString() @Length(1,32) public username?: string;
  @IsOptional() @IsInt() @Min(0) @Max(2147483647) public lp?: number;
  @IsOptional() @IsInt() @Min(-2147483648) @Max(2147483647) public mmr?: number;
  @IsOptional() @Equals("RESET") public confirmation?: string;
  @IsOptional() @IsInt() @Min(1) @Max(2147483647) public songId?: number;
  @IsOptional() @IsInt() @Min(0) @Max(100) public volumePercent?: number;
}
