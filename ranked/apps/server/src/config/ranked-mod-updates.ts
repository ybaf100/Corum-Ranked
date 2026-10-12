import { RankedDomainError } from "@corum-ranked/rules";

export type RankedBuildChannel = "debug" | "release";
export interface RankedModUpdate {
  readonly version: string;
  readonly sha256: string;
  readonly sizeBytes: number;
  readonly downloadUrl?: string;
}
export type RankedModUpdates = Readonly<Partial<Record<RankedBuildChannel, RankedModUpdate>>>;

export const normalizeRankedDownloadBaseUrl=(text:string|undefined):string|undefined=>{
  if(!text?.trim())return undefined;
  let url:URL;
  try {url=new URL(text.trim());}catch {throw new RankedDomainError("INVALID_CONFIG","Invalid RANKED_DOWNLOAD_BASE_URL");}
  if(url.protocol!=="https:" || url.username || url.password || url.search || url.hash || url.pathname!=="/")
    throw new RankedDomainError("INVALID_CONFIG","RANKED_DOWNLOAD_BASE_URL must be a public HTTPS bucket origin");
  return url.origin;
};
export const rankedModDownloadUrl=(base:string,channel:RankedBuildChannel,version:string):string=>
  `${base}/ranked/${channel}-${version}/Corum-Ranked-${version}-${channel}.geode`;

// Operator-selected immutable packages, never a moving latest-version alias.
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
    let downloadUrl:string|undefined;
    if(metadata.downloadUrl!==undefined) {
      try {
        if(typeof metadata.downloadUrl!=="string" || metadata.downloadUrl.length>2048)throw new Error();
        const url=new URL(metadata.downloadUrl);
        if(url.protocol!=="https:" || url.username || url.password || url.hash)throw new Error();
        downloadUrl=url.href;
      }catch {throw new RankedDomainError("INVALID_CONFIG",`Invalid Ranked ${channel} download URL`);}
    }
    result[channel] = { version: metadata.version, sha256: metadata.sha256.toLowerCase(), sizeBytes: Number(metadata.sizeBytes),
      ...(downloadUrl?{downloadUrl}:{}) };
  }
  return result;
};
