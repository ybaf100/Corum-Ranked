import { Controller, Get, Header, Inject, UseGuards } from "@nestjs/common";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { readStandings, unrankedStanding } from "./standing.js";

@Controller("api/ranked/session/standing")
@UseGuards(SessionGuard)
export class StandingController {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
  ) {}

  @Get()
  @Header("Cache-Control", "no-store")
  public async read(@CurrentSession() session: RankedSessionContext) {
    const views = await readStandings(this.database, [session.playerId]);
    return { serverNow: this.clock.now().toISOString(),
      standing: views.get("current")?.get(session.playerId) ?? unrankedStanding() };
  }
}
