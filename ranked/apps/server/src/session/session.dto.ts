import { Type } from "class-transformer";
import {
  IsArray,
  IsBoolean,
  IsObject,
  IsOptional,
  IsString,
  Length,
  Matches,
  ValidateNested,
} from "class-validator";

export class InstalledModDto {
  @IsString()
  @Length(1, 160)
  public id!: string;

  @IsString()
  @Length(1, 80)
  public version!: string;

  @IsBoolean()
  public enabled!: boolean;

  @IsBoolean()
  public loaded!: boolean;

  @IsBoolean()
  public internal!: boolean;

  @IsBoolean()
  public system!: boolean;

  @IsOptional()
  @IsObject()
  public settings?: Record<string, boolean | number | string>;
}

export class CreateSessionDto {
  @IsString()
  @Matches(/^[1-9]\d{0,19}$/)
  public gdAccountId!: string;

  @IsString()
  @Length(1, 32)
  public gdUsername!: string;

  @IsString()
  @Length(1, 40)
  public clientVersion!: string;

  @IsArray()
  @ValidateNested({ each: true })
  @Type(() => InstalledModDto)
  public installedMods!: InstalledModDto[];
}

export class EnvironmentRecheckDto {
  @IsArray()
  @ValidateNested({ each: true })
  @Type(() => InstalledModDto)
  public installedMods!: InstalledModDto[];
}
