import { Inject, Injectable, Logger, ServiceUnavailableException } from "@nestjs/common";
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
  private readonly logger = new Logger(AppsScriptCsmpTierSource.name);
  private csmpCache: { readonly expiresAtMs: number; readonly body: Record<string, unknown> } | null = null;
  private csmpInFlight: Promise<Record<string, unknown>> | null = null;

  public constructor(
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
  ) {}

  private isTimeout(error: unknown): boolean {
    return error instanceof Error && (error.name === "TimeoutError" || error.name === "AbortError");
  }

  private unavailable(code: string, message: string): ServiceUnavailableException {
    return new ServiceUnavailableException({
      statusCode: 503,
      error: "Service Unavailable",
      code,
      message,
    });
  }

  private async fetchActionOnce(
    action: string,
    parameters: Readonly<Record<string, string>>,
  ): Promise<Record<string, unknown>> {
    const url = new URL(this.environment.rankedConfigUrl);
    url.searchParams.set("action", action);
    for (const [key, value] of Object.entries(parameters)) url.searchParams.set(key, value);

    let response: Response;
    try {
      response = await fetch(url, {
        headers: { accept: "application/json" },
        signal: AbortSignal.timeout(this.environment.rankedCsmpFetchTimeoutMs),
      });
    } catch (error) {
      if (this.isTimeout(error)) throw error;
      this.logger.error(
        `Apps Script action '${action}' failed before a response was received: ${error instanceof Error ? error.message : String(error)}`,
      );
      throw this.unavailable(
        "CSMP_SOURCE_UNAVAILABLE",
        "CSMP service is temporarily unavailable. Retry shortly.",
      );
    }

    if (!response.ok) {
      this.logger.warn(`Apps Script action '${action}' returned HTTP ${response.status}`);
      throw this.unavailable(
        "CSMP_SOURCE_HTTP_ERROR",
        "CSMP service is temporarily unavailable. Retry shortly.",
      );
    }

    let body: { ok?: boolean };
    try {
      body = (await response.json()) as { ok?: boolean };
    } catch (error) {
      this.logger.warn(
        `Apps Script action '${action}' returned invalid JSON: ${error instanceof Error ? error.message : String(error)}`,
      );
      throw this.unavailable(
        "CSMP_SOURCE_INVALID_RESPONSE",
        "CSMP service returned an invalid response. Retry shortly.",
      );
    }
    if (!body.ok) {
      this.logger.warn(`Apps Script action '${action}' returned ok=false`);
      throw this.unavailable(
        "CSMP_SOURCE_ERROR",
        "CSMP service is temporarily unavailable. Retry shortly.",
      );
    }
    return body as Record<string, unknown>;
  }

  private async fetchAction(
    action: string,
    parameters: Readonly<Record<string, string>> = {},
  ): Promise<Record<string, unknown>> {
    const maximumAttempts = 2;
    for (let attempt = 1; attempt <= maximumAttempts; attempt += 1) {
      try {
        return await this.fetchActionOnce(action, parameters);
      } catch (error) {
        if (!this.isTimeout(error)) throw error;
        this.logger.warn(
          `Apps Script action '${action}' timed out after ${this.environment.rankedCsmpFetchTimeoutMs}ms (attempt ${attempt}/${maximumAttempts})`,
        );
        if (attempt === maximumAttempts) {
          throw this.unavailable(
            "CSMP_SOURCE_TIMEOUT",
            "CSMP service timed out. Retry shortly.",
          );
        }
      }
    }
    throw this.unavailable("CSMP_SOURCE_UNAVAILABLE", "CSMP service is temporarily unavailable.");
  }

  private async fetchCsmpDefinition(): Promise<Record<string, unknown>> {
    const now = Date.now();
    if (this.csmpCache && this.csmpCache.expiresAtMs > now) return this.csmpCache.body;
    if (this.csmpInFlight) return this.csmpInFlight;

    const pending = this.fetchAction("csmp")
      .then((body) => {
        this.csmpCache = {
          body,
          expiresAtMs: Date.now() + this.environment.rankedConfigRefreshMs,
        };
        return body;
      })
      .finally(() => {
        if (this.csmpInFlight === pending) this.csmpInFlight = null;
      });
    this.csmpInFlight = pending;
    return pending;
  }

  public async fetchCurrentTier(gdAccountId: string): Promise<CsmpTier> {
    const [csmp, playerRecords] = await Promise.all([
      this.fetchCsmpDefinition(),
      this.fetchAction("player_records", { gdAccountId }),
    ]);
    const stages = Array.isArray(csmp.tiers) ? (csmp.tiers as CsmpStagePayload[]) : [];
    const players = Array.isArray(playerRecords.players)
      ? (playerRecords.players as { records?: PlayerRecordPayload[] }[])
      : [];
    return resolveCsmpTier(stages, players[0]?.records || []);
  }
}
