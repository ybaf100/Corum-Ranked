import { IsIn, IsInt, IsOptional, IsString, Max, Min } from "class-validator";

export class EquipAchievementTitleDto {
  @IsString()
  public achievementId!: string;

  @IsOptional()
  @IsInt()
  @Min(1)
  @Max(5)
  public level?: number;
}

export class ClaimAchievementDto {
  @IsString()
  @IsIn(["ALL_PENDING"])
  public scope: "ALL_PENDING" = "ALL_PENDING";
}

export class ClaimAllAchievementsDto {
  @IsString()
  @IsIn(["SOLO_RANKED","SURVIVAL","SHARED","ALL"])
  public scope!: "SOLO_RANKED"|"SURVIVAL"|"SHARED"|"ALL";
}
