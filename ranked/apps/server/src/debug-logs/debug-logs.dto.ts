import { IsInt, IsString, IsUUID, Length, Max, Matches, Min } from "class-validator";

/** One text chunk of one recent client log file (Debug builds only). */
export class DebugLogChunkDto {
  @IsUUID() public uploadId!: string;
  @IsInt() @Min(0) @Max(2) public fileIndex!: number;
  @IsString() @Length(1, 120) @Matches(/^[A-Za-z0-9 ._:+-]+$/) public fileName!: string;
  @IsInt() @Min(0) @Max(39) public part!: number;
  @IsInt() @Min(1) @Max(40) public totalParts!: number;
  @IsString() @Length(0, 60_000) public content!: string;
}
