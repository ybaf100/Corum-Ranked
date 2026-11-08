import { Body, Controller, Get, Header, Post, UseGuards } from "@nestjs/common";
import { IsString, MaxLength } from "class-validator";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { EliminationEffectService } from "./elimination-effect.service.js";

export class EquipEliminationEffectDto {
  @IsString() @MaxLength(32) public effect!: string;
}

@Controller("api/ranked/cosmetics/elimination")
@UseGuards(SessionGuard)
export class EliminationEffectController {
  public constructor(private readonly effects: EliminationEffectService) {}

  @Get()
  @Header("Cache-Control", "no-store")
  public state(@CurrentSession() session: RankedSessionContext) {
    return this.effects.state(session.playerId);
  }

  @Post("equip")
  public equip(@CurrentSession() session: RankedSessionContext, @Body() body: EquipEliminationEffectDto) {
    return this.effects.equip(session.playerId, body.effect);
  }
}
