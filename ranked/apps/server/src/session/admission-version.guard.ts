import { RankedModUpdateService } from "../config/ranked-mod-update.service.js";
import { CanActivate, ExecutionContext, HttpException, Inject, Injectable, Optional } from "@nestjs/common";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { evaluateRankedClientVersion, REQUIRED_RANKED_CLIENT_VERSION } from "./client-version.js";
import type { RankedRequest } from "./session.types.js";

/** Check new matchmaking admission only. Heartbeats and round entry keep the
 * version admitted for their session so a deployment cannot halt a match. */
@Injectable()
export class AdmissionVersionGuard implements CanActivate {
  constructor(@Optional() @Inject(SERVER_ENVIRONMENT) private readonly environment?: ServerEnvironment,
    @Optional() @Inject(RankedModUpdateService) private readonly updates?:RankedModUpdateService) {}

  canActivate(context: ExecutionContext): boolean {
    const request = context.switchToHttp().getRequest<RankedRequest>();
    const session = request.rankedSession;
    const required = this.updates?.admissionVersion(session?.clientVersion??"",session?.clientBuildChannel??"release") ?? this.environment?.rankedModUpdates?.[session?.clientBuildChannel ?? "release"]?.version
      ?? REQUIRED_RANKED_CLIENT_VERSION;
    const mods: unknown = request.body?.installedMods;
    const installedMods = Array.isArray(mods) ? mods.filter(mod => mod && typeof mod.id === "string" &&
      typeof mod.version === "string" && typeof mod.enabled === "boolean" && typeof mod.loaded === "boolean") : [];
    const decision = evaluateRankedClientVersion(session?.clientVersion ?? "", installedMods, required);
    if (!decision.allowed) throw new HttpException({ code: "RANKED_CLIENT_OUTDATED", ...decision }, 426);
    return true;
  }
}
