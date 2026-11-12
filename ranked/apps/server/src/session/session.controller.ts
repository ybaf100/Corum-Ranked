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
import { ArgonService } from "./argon.service.js";

@Controller("api/ranked/session")
export class SessionController {
  public constructor(private readonly sessions: SessionService,
    @Optional() @Inject(SERVER_ENVIRONMENT) private readonly environment?: ServerEnvironment,
    @Optional() @Inject(RankedModUpdateService) private readonly updates?:RankedModUpdateService,
    @Optional() @Inject(ArgonService) private readonly argon?: ArgonService) {}

  @Post()
  public async create(@Body() body: CreateSessionDto) {
    // Version compatibility is an HTTP admission rule. Reject stale clients
    // before SessionService can seed a profile or persist a session token.
    const required = this.updates?.admissionVersion(body.clientVersion,body.clientBuildChannel??"release") ?? this.environment?.rankedModUpdates?.[body.clientBuildChannel ?? "release"]?.version ?? REQUIRED_RANKED_CLIENT_VERSION;
    const decision = evaluateRankedClientVersion(body.clientVersion, body.installedMods, required);
    if (!decision.allowed) {
      throw new HttpException({
        code: "RANKED_CLIENT_UPDATE_REQUIRED",
        message: `Ranked ${decision.requiredVersion} is required. Update the mod before entering Ranked.`,
        requiredVersion: decision.requiredVersion,
        clientVersion: decision.clientVersion,
        installedModVersion: decision.installedModVersion,
      }, 426);
    }
    // Ownership proof precedes any player upsert: a claimed account ID can no
    // longer rename, take over or play as another account. The stored name is
    // the one inside the Argon token, not the client's claim.
    const { argonToken, ...claim } = body;
    const identity = await this.argon?.verify(claim.gdAccountId, claim.gdUsername, argonToken);
    return this.sessions.create(identity ? { ...claim, gdUsername: identity.username } : claim);
  }

  @Post("heartbeat")
  @UseGuards(SessionGuard)
  public heartbeat(@CurrentSession() session: RankedSessionContext,@Body() body?:unknown) {
    const includePopulation=Boolean(body&&typeof body==='object'&&'includePopulation'in body&&body.includePopulation===true);
    return this.sessions.heartbeat(session,includePopulation);
  }

  @Post("close")
  @UseGuards(SessionGuard)
  public close(@CurrentSession() session: RankedSessionContext) {
    return this.sessions.close(session);
  }
}
