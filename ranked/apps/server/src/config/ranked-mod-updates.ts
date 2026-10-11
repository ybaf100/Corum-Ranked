import { RankedDomainError } from "@corum-ranked/rules";

export type RankedBuildChannel = "debug" | "release";
export interface RankedModUpdate {
  readonly version: string;
  readonly sha256: string;
  readonly sizeBytes: number;
}
export type RankedModUpdates = Readonly<Partial<Record<RankedBuildChannel, RankedModUpdate>>>;

// Operator-selected immutable releases, never GitHub's moving `latest` alias.
export const parseRankedModUpdates = (text: string | undefined): RankedModUpdates => {
  if (!text?.trim()) return {};
  let root: unknown;
  try { root = JSON.parse(text); } catch { throw new RankedDomainError("INVALID_CONFIG", "Invalid RANKED_MOD_UPDATES_JSON"); }
  if (!root || typeof root !== "object" || Array.isArray(root)) {
    throw new RankedDomainError("INVALID_CONFIG", "RANKED_MOD_UPDATES_JSON must be an object");
  }
  const value = root as Record<string, unknown>;
  if (Object.keys(value).some(key => key !== "debug" && key !== "release")) {
    throw new RankedDomainError("INVALID_CONFIG", "Unknown Ranked update channel");
  }
  const result: Partial<Record<RankedBuildChannel, RankedModUpdate>> = {};
  for (const channel of ["debug", "release"] as const) {
    const entry = value[channel];
    if (entry === undefined) continue;
    if (!entry || typeof entry !== "object" || Array.isArray(entry)) throw new RankedDomainError("INVALID_CONFIG", "Invalid Ranked update descriptor");
    const metadata = entry as Record<string, unknown>;
    if (typeof metadata.version !== "string" || !/^v\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?$/.test(metadata.version) || metadata.version.length > 40 ||
      typeof metadata.sha256 !== "string" || !/^[a-fA-F0-9]{64}$/.test(metadata.sha256) ||
      !Number.isSafeInteger(metadata.sizeBytes) || Number(metadata.sizeBytes) < 1 || Number(metadata.sizeBytes) > 128 * 1024 * 1024) {
      throw new RankedDomainError("INVALID_CONFIG", `Invalid Ranked ${channel} update descriptor`);
    }
    result[channel] = { version: metadata.version, sha256: metadata.sha256.toLowerCase(), sizeBytes: Number(metadata.sizeBytes) };
  }
  return result;
};
