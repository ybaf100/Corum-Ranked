import { Inject, Injectable } from "@nestjs/common";
import { CSMP_TIERS, type CsmpTier } from "@corum-ranked/rules";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "./server-environment.js";

export const CSMP_TIER_SOURCE = Symbol("CSMP_TIER_SOURCE");

export interface CsmpTierSource {
  fetchCurrentTier(gdAccountId: string): Promise<CsmpTier>;
}

interface CsmpMapPayload {
  readonly levelId?: string;
  readonly alternateLevelId?: string;
  readonly title?: string;
}

interface CsmpStagePayload {
  readonly order?: number;
  readonly name?: string;
  readonly required?: number;
  readonly maps?: readonly CsmpMapPayload[];
}

interface PlayerRecordPayload {
  readonly levelId?: string;
  readonly title?: string;
  readonly percent?: number;
  readonly status?: string;
}

export const resolveCsmpTier = (
  stagesInput: readonly CsmpStagePayload[],
  records: readonly PlayerRecordPayload[],
): CsmpTier => {
  const stages = [...stagesInput].sort(
    (left, right) => Number(left.order ?? 0) - Number(right.order ?? 0),
  );
  const completedIds = new Set(
    records
      .filter(
        (record) =>
          Number(record.percent) >= 100 &&
          String(record.status || "unverified").trim().toLowerCase() !== "rejected",
      )
      .map((record) => String(record.levelId || "").trim())
      .filter(Boolean),
  );
  let current: CsmpTier = "NONE";

  for (const stage of stages) {
    const name = String(stage.name || "").trim().toUpperCase();
    if (!CSMP_TIERS.includes(name as CsmpTier) || name === "NONE") continue;
    const canonicalMaps = (stage.maps || []).filter((map) => String(map.levelId || "").trim());
    const required = Math.trunc(Number(stage.required));
    if (canonicalMaps.length === 0 || !Number.isFinite(required) || required <= 0) break;
    const completed = canonicalMaps.filter((map) => {
      const ids = [map.levelId, map.alternateLevelId]
        .map((id) => String(id || "").trim())
        .filter(Boolean);
      return ids.some((id) => completedIds.has(id));
    }).length;
    if (completed < required) break;
    current = name as CsmpTier;
  }
  return current;
};

@Injectable()
export class AppsScriptCsmpTierSource implements CsmpTierSource {
  public constructor(
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
  ) {}

  private async fetchAction(action: string, parameters: Readonly<Record<string, string>> = {}) {
    const url = new URL(this.environment.rankedConfigUrl);
    url.searchParams.set("action", action);
    for (const [key, value] of Object.entries(parameters)) url.searchParams.set(key, value);
    const response = await fetch(url, {
      headers: { accept: "application/json" },
      signal: AbortSignal.timeout(this.environment.rankedConfigFetchTimeoutMs),
    });
    if (!response.ok) throw new Error(`Apps Script ${action} returned HTTP ${response.status}`);
    const body = (await response.json()) as { ok?: boolean };
    if (!body.ok) throw new Error(`Apps Script ${action} returned an error`);
    return body as Record<string, unknown>;
  }

  public async fetchCurrentTier(gdAccountId: string): Promise<CsmpTier> {
    const [csmp, playerRecords] = await Promise.all([
      this.fetchAction("csmp"),
      this.fetchAction("player_records", { gdAccountId }),
    ]);
    const stages = Array.isArray(csmp.tiers) ? (csmp.tiers as CsmpStagePayload[]) : [];
    const players = Array.isArray(playerRecords.players)
      ? (playerRecords.players as { records?: PlayerRecordPayload[] }[])
      : [];
    return resolveCsmpTier(stages, players[0]?.records || []);
  }
}
