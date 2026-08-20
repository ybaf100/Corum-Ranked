import type {
  AllowedModRule,
  RankedMap,
  RankedOperationalConfig,
} from "@corum-ranked/rules";

export interface RankedConfigDocument {
  readonly generation: string;
  readonly generatedAt: string;
  readonly operational: RankedOperationalConfig;
  readonly maps: readonly RankedMap[];
  readonly allowedMods: readonly AllowedModRule[];
}

export interface RankedConfigSourceResponse {
  readonly ok: boolean;
  readonly action: "ranked_config";
  readonly data?: RankedConfigDocument;
  readonly error?: string;
}

export interface RankedConfigSnapshot extends RankedConfigDocument {
  readonly fetchedAt: string;
}

export interface RankedConfigStatus {
  readonly ready: boolean;
  readonly generation: string | null;
  readonly fetchedAt: string | null;
  readonly lastAttemptAt: string | null;
  readonly lastError: string | null;
}
