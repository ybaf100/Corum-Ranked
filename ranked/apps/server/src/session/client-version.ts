export const PREVIOUS_RANKED_CLIENT_VERSION = "v0.5.0-beta.107";
export const REQUIRED_RANKED_CLIENT_VERSION = "v0.5.0-beta.119";

export interface RankedClientVersionMod {
  readonly id: string;
  readonly version: string;
  readonly enabled: boolean;
  readonly loaded: boolean;
}

export interface RankedClientVersionDecision {
  readonly allowed: boolean;
  readonly requiredVersion: string;
  readonly clientVersion: string;
  readonly installedModVersion: string | null;
}

export const normalizeRankedClientVersion = (version: string): string =>
  version.trim().replace(/^v(?=\d)/i, "");

export const evaluateRankedClientVersion = (
  clientVersion: string,
  installedMods: readonly RankedClientVersionMod[],
  requiredVersion: string = REQUIRED_RANKED_CLIENT_VERSION,
): RankedClientVersionDecision => {
  const normalizedClientVersion = normalizeRankedClientVersion(clientVersion);
  const clientMod = installedMods.find(
    (mod) => mod.id === "hwanhee1.corum_ranked" && mod.enabled && mod.loaded,
  );
  const installedModVersion = clientMod?.version.trim() || null;
  const required = normalizeRankedClientVersion(requiredVersion);
  return {
    allowed:
      normalizedClientVersion === required &&
      installedModVersion !== null &&
      normalizeRankedClientVersion(installedModVersion) === required,
    requiredVersion,
    clientVersion: clientVersion.trim(),
    installedModVersion,
  };
};
