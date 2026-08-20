import { Type } from "class-transformer";
import {
  IsBoolean,
  IsISO8601,
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

export class SubmitBanDto {
  @IsOptional()
  @IsString()
  @Length(1, 40)
  public canonicalLevelId?: string | null;
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

export class AttemptEndDto {
  @IsString()
  @Length(1, 40)
  public levelId!: string;

  @IsString()
  @Length(1, 40)
  public attemptId!: string;

  @IsString()
  @Length(1, 100)
  public clientEventId!: string;

  @Type(() => Number)
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
