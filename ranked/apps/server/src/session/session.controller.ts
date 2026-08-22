import {
  Body,
  Controller,
  HttpException,
  HttpStatus,
  Post,
  UseGuards,
} from "@nestjs/common";
import { CurrentSession } from "./current-session.decorator.js";
import { evaluateRankedClientVersion } from "./client-version.js";
import { CreateSessionDto } from "./session.dto.js";
import { SessionGuard } from "./session.guard.js";
import { SessionService } from "./session.service.js";
import type { RankedSessionContext } from "./session.types.js";

@Controller("api/ranked/session")
export class SessionController {
  public constructor(private readonly sessions: SessionService) {}

  @Post()
  public create(@Body() body: CreateSessionDto) {
    // Version compatibility is an HTTP admission rule. Reject stale clients
    // before SessionService can seed a profile or persist a session token.
    const decision = evaluateRankedClientVersion(body.clientVersion, body.installedMods);
    if (!decision.allowed) {
      throw new HttpException({
        code: "RANKED_CLIENT_UPDATE_REQUIRED",
        message: `Corum Ranked ${decision.requiredVersion} is required. Update the mod before entering Ranked.`,
        requiredVersion: decision.requiredVersion,
        clientVersion: decision.clientVersion,
        installedModVersion: decision.installedModVersion,
      }, HttpStatus.UPGRADE_REQUIRED);
    }
    return this.sessions.create(body);
  }

  @Post("heartbeat")
  @UseGuards(SessionGuard)
  public heartbeat(@CurrentSession() session: RankedSessionContext) {
    return this.sessions.heartbeat(session);
  }
}
