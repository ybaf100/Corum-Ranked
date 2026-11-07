import { Controller, Get, Header, Param, UseGuards } from "@nestjs/common";
import { Matches } from "class-validator";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { MapPoolService } from "./map-pool.service.js";

class MapPoolParams {
  @Matches(/^[1-9]\d{0,11}$/) public canonicalLevelId!: string;
}

// Solo map pool list and per-map statistics. A session is required so the
// detail can include the viewer's own record on that map.
@Controller("api/ranked/solo/map-pool")
@UseGuards(SessionGuard)
export class MapPoolController {
  public constructor(private readonly maps: MapPoolService) {}

  @Get()
  @Header("Cache-Control", "no-store")
  public list() {
    return this.maps.pool();
  }

  @Get(":canonicalLevelId")
  @Header("Cache-Control", "no-store")
  public detail(@CurrentSession() session: RankedSessionContext, @Param() params: MapPoolParams) {
    return this.maps.detail(params.canonicalLevelId, session.playerId);
  }
}
