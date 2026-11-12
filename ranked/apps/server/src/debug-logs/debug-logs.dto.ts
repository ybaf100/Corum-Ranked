import { Equals, IsInt, IsOptional, IsString, IsUUID, Length, Max, Matches, Min } from "class-validator";

/** One text chunk of one client log or crash log the player picked (Debug
 * builds only, up to six files). Geode names files like Geode_2026-11-07_00.06.07.log. */
export class DebugLogChunkDto {
  @IsUUID() public uploadId!: string;
  @IsInt() @Min(0) @Max(5) public fileIndex!: number;
  @IsOptional() @IsInt() @Min(1) @Max(6) public totalFiles?: number;
  @IsString() @Length(1, 120) @Matches(/^[A-Za-z0-9 ._:+()-]+$/) public fileName!: string;
  @IsInt() @Min(0) @Max(39) public part!: number;
  @IsInt() @Min(1) @Max(40) public totalParts!: number;
  @IsString() @Length(0, 60_000) public content!: string;
}

/** The same chunk sent from the main menu before any Ranked session exists:
 * the player is named the way session creation names it (GD account). */
export class DebugLogMenuChunkDto extends DebugLogChunkDto {
  @IsString() @Matches(/^[1-9][0-9]{0,11}$/) public gdAccountId!: string;
  @IsString() @Length(1, 32) public gdUsername!: string;
  @IsString() @Length(1, 64) public clientVersion!: string;
  @Equals("debug") public clientBuildChannel!: "debug";
  @IsOptional() @IsString() @Length(16, 4096) public argonToken?: string;
}
