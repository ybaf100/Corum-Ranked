import type {
  AllowedModRule,
  RankedMap,
  RankedOperationalConfig,
} from "@corum-ranked/rules";

export interface RankedClientAudioResource {
  readonly key: string;
  readonly label: string;
  readonly songId: number;
  readonly startSeconds: number;
  readonly loop: boolean;
}

export interface RankedClientAudioConfig {
  readonly enabled: boolean;
  readonly fadeInSeconds: number;
  readonly fadeOutSeconds: number;
  readonly resources: readonly RankedClientAudioResource[];
}

export interface RankedClientUiConfig {
  readonly fadeInSeconds: number;
  readonly fadeOutSeconds: number;
}

export interface RankedClientPresentationConfig {
  readonly audio: RankedClientAudioConfig;
  readonly ui: RankedClientUiConfig;
}

export interface RankedConfigDocument {
  readonly generation: string;
  readonly generatedAt: string;
  readonly operational: RankedOperationalConfig;
  readonly client?: RankedClientPresentationConfig;
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
