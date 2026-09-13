import { Body, Controller, Get, Param, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { AchievementService } from "./achievement.service.js";
import { ClaimAchievementDto, EquipAchievementTitleDto } from "./achievement.dto.js";

@Controller("api/ranked/achievements")
@UseGuards(SessionGuard)
export class AchievementController {
  public constructor(private readonly achievements: AchievementService) {}

  @Get()
  public list(@CurrentSession() session: RankedSessionContext) {
    return this.achievements.state(session.playerId);
  }

  @Post(":achievementId/claim")
  public claim(
    @Param("achievementId") achievementId: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() _body: ClaimAchievementDto,
  ) {
    return this.achievements.claim(session.playerId, achievementId);
  }

  @Post("title/equip")
  public equipTitle(
    @CurrentSession() session: RankedSessionContext,
    @Body() body: EquipAchievementTitleDto,
  ) {
    return this.achievements.equipTitle(session.playerId, body.achievementId, body.level);
  }

  @Post("title/clear")
  public clearTitle(@CurrentSession() session: RankedSessionContext) {
    return this.achievements.clearTitle(session.playerId);
  }
}
