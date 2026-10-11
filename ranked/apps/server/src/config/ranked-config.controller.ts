import { Controller, Get, Header, Inject, Optional } from "@nestjs/common";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../session/client-version.js";
import { RankedConfigService } from "./ranked-config.service.js";
import { DATABASE, type SqlExecutor } from "../database/database.port.js";
import { readRankedAudioVolumes, withRankedAudioVolumes } from "./ranked-audio-volume.js";

@Controller("api/ranked")
export class RankedConfigController {
  public constructor(@Inject(RankedConfigService) private readonly rankedConfig: RankedConfigService,
    @Optional() @Inject(DATABASE) private readonly database?: SqlExecutor) {}

  @Get("maps")
  @Header("Cache-Control", "public, max-age=60, stale-while-revalidate=120")
  public getPublicMaps(): object {
    // Read the last validated sheet2 snapshot even when matchmaking is closed.
    // Never return the complete config, private seed values or source secrets.
    const snapshot = this.rankedConfig.getSnapshot();
    return {
      generation: snapshot.generation,
      fetchedAt: snapshot.fetchedAt,
      maps: snapshot.maps.filter((map) => map.active).map((map) => ({
        title: map.title,
        difficulty: map.difficulty,
        mapCode: map.playableLevelId,
        pool: map.pool,
        qualifyingPercent: map.qualifyingPercent,
      })),
    };
  }

  @Get("config")
  @Header("Cache-Control", "no-store")
  public async getClientConfig(): Promise<object> {
    const snapshot = this.rankedConfig.getSnapshot();
    const volumes = await readRankedAudioVolumes(snapshot.client, this.database);
    return {
      serverNow: new Date().toISOString(),
      generation: snapshot.generation,
      rulesVersion: snapshot.operational.rules.rulesVersion,
      requiredClientVersion: REQUIRED_RANKED_CLIENT_VERSION,
      queueEnabled: snapshot.operational.enabled,
      rules: snapshot.operational.rules,
      cbf: snapshot.operational.cbf,
      client: withRankedAudioVolumes(snapshot.client, volumes) ?? {
        audio: { enabled: false, fadeInSeconds: 0.8, fadeOutSeconds: 0.6, resources: [] },
        ui: { fadeInSeconds: 0.24, fadeOutSeconds: 0.18 },
      },
      allowedMods: snapshot.allowedMods.filter((rule) => rule.enabled),
    };
  }
}
