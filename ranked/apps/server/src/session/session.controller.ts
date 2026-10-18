import { RankedModUpdateService } from "../config/ranked-mod-update.service.js";
import {
  Body,
  Controller,
  HttpException,
  Post,
  UseGuards,
  Inject,
  Optional,
} from "@nestjs/common";
import { CurrentSession } from "./current-session.decorator.js";
import { evaluateRankedClientVersion, REQUIRED_RANKED_CLIENT_VERSION } from "./client-version.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { CreateSessionDto } from "./session.dto.js";
import { SessionGuard } from "./session.guard.js";
import { SessionService } from "./session.service.js";
import type { RankedSessionContext } from "./session.types.js";

@Controller("api/ranked/session")
export class SessionController {
  public constructor(private readonly sessions: SessionService,
    @Optional() @Inject(SERVER_ENVIRONMENT) private readonly environment?: ServerEnvironment,
    @Optional() @Inject(RankedModUpdateService) private readonly updates?:RankedModUpdateService) {}

  @Post()
  public create(@Body() body: CreateSessionDto) {
    // Version compatibility is an HTTP admission rule. Reject stale clients
    // before SessionService can seed a profile or persist a session token.
    const required = this.updates?.admissionVersion(body.clientVersion,body.clientBuildChannel??"release") ?? this.environment?.rankedModUpdates?.[body.clientBuildChannel ?? "release"]?.version ?? REQUIRED_RANKED_CLIENT_VERSION;
    const decision = evaluateRankedClientVersion(body.clientVersion, body.installedMods, required);
    if (!decision.allowed) {
      throw new HttpException({
        code: "RANKED_CLIENT_UPDATE_REQUIRED",
        message: `Corum Ranked ${decision.requiredVersion} is required. Update the mod before entering Ranked.`,
        requiredVersion: decision.requiredVersion,
        clientVersion: decision.clientVersion,
        installedModVersion: decision.installedModVersion,
      }, 426);
    }
    return this.sessions.create(body);
  }

  @Post("heartbeat")
  @UseGuards(SessionGuard)
  public heartbeat(@CurrentSession() session: RankedSessionContext) {
    return this.sessions.heartbeat(session);
  }

  @Post("close")
  @UseGuards(SessionGuard)
  public close(@CurrentSession() session: RankedSessionContext) {
    return this.sessions.close(session);
  }
}
