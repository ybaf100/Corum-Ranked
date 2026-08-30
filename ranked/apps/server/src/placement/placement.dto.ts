import { Type } from "class-transformer";
import { IsInt, Max, Min } from "class-validator";
import { AttemptEndDto, AttemptProgressDto, AttemptStartDto } from "../match/match.dto.js";
import { EnvironmentRecheckDto } from "../session/session.dto.js";

export class StartPlacementDto extends EnvironmentRecheckDto {
  @Type(() => Number)
  @IsInt()
  @Min(1)
  @Max(6)
  public startingPool!: 1 | 2 | 3 | 4 | 5 | 6;
}

export class ReadyPlacementMapDto extends EnvironmentRecheckDto {}
export class SkipPlacementDto extends EnvironmentRecheckDto {}
export class PlacementAttemptStartDto extends AttemptStartDto {}
export class PlacementAttemptEndDto extends AttemptEndDto {}
export class PlacementAttemptProgressDto extends AttemptProgressDto {}
