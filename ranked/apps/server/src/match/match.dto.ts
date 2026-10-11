import { Transform, Type } from "class-transformer";
import {
  IsBoolean,
  IsISO8601,
  IsIn,
  IsInt,
  IsNumber,
  IsOptional,
  IsString,
  Length,
  Max,
  Min,
} from "class-validator";
import { EnvironmentRecheckDto } from "../session/session.dto.js";

export class ReadyMatchDto extends EnvironmentRecheckDto {}

export class NativeSceneEnteredDto {
  @IsInt() @Min(0) @Max(3)
  public roundNumber!: number;
  @IsInt() @Min(0) @Max(10000)
  public deathmatchSequence!: number;
  @IsString() @Length(1, 40)
  public playableLevelId!: string;
}

export class SubmitBanDto {
  @IsOptional()
  @IsString()
  @Length(1, 40)
  public canonicalLevelId?: string | null;
}

export class ResourceFailureDto {
  @IsString()
  @IsIn(["MAP"])
  public resource!: "MAP";
}

export class PracticePresenceDto {
  @IsBoolean()
  public practice!: boolean;
}

export class MatchPauseDto {
  @IsString() @Length(1, 100)
  public leaseId!: string;
  @IsBoolean()
  public paused!: boolean;
}

export class SurrenderDto {
  @IsString() @IsIn(["ROUND", "MATCH"])
  public scope!: "ROUND" | "MATCH";
  @IsString() @Length(1, 100)
  public clientEventId!: string;
  @IsInt() @Min(0) @Max(3)
  public roundNumber!: number;
  @IsInt() @Min(0) @Max(10000)
  public deathmatchSequence!: number;
}

export class AttemptStartIntentDto {
  @IsString()
  @Length(1, 40)
  public levelId!: string;

  @IsString()
  @Length(1, 100)
  public clientEventId!: string;

  @IsISO8601()
  public clientStartedAt!: string;
}

export class AttemptStartDto {
  @IsString()
  @Length(1, 40)
  public levelId!: string;

  @IsString()
  @Length(1, 100)
  public clientEventId!: string;

  @IsOptional()
  @IsISO8601()
  public clientStartedAt?: string;
}

export const normalizeAttemptEndProgress = (value: unknown): number => {
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric < 0 || numeric > 100) return numeric;
  return Math.round(numeric * 1_000) / 1_000;
};

export class AttemptEndDto {
  @IsOptional()
  @IsBoolean()
  public nativeDeath?: boolean;
  @IsString()
  @Length(1, 40)
  public levelId!: string;

  @IsString()
  @Length(1, 40)
  public attemptId!: string;

  @IsString()
  @Length(1, 100)
  public clientEventId!: string;

  // Older clients can send the raw Geometry Dash percentage with many binary
  // decimal places. Normalize valid in-range input before validating the
  // protocol's three-decimal transport contract; invalid/range errors remain
  // invalid and are still rejected below.
  @Transform(({ value }) => normalizeAttemptEndProgress(value), { toClassOnly: true })
  @IsNumber({ maxDecimalPlaces: 3 })
  @Min(0)
  @Max(100)
  public progressPercent!: number;

  @IsBoolean()
  public cleared!: boolean;

  @IsOptional()
  @IsISO8601()
  public clientEndedAt?: string;
}

export class AttemptProgressDto {
  @IsString()
  @Length(1, 40)
  public levelId!: string;

  @IsString()
  @Length(1, 40)
  public attemptId!: string;

  @Type(() => Number)
  @IsInt()
  @Min(0)
  @Max(100)
  public progressPercent!: number;
}
