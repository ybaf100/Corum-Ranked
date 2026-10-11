import { createHash } from "node:crypto";
import type { RankedMap, RankedMapMetadata } from "@corum-ranked/rules";
import type { SqlExecutor } from "../database/database.port.js";
import type { RankedConfigDocument } from "./ranked-config.document.js";

// GDDL Integration Values.h, c6ad1cfa (B1rtek, MIT), including the reform palette.
// https://github.com/B1rtek/Geode-GDDLIntegration/blob/c6ad1cfa70a7c17c508c3e7ec55fbacbd9606244/src/Values.h
// Copyright (c) 2024 B1rtek
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
const tierColors = [
  "360432", "dddfee", "d5d3e9", "d3cbe7", "d3c3e4", "d4bbe2", "d5b0de", "dba7dc",
  "db9fd1", "d991c1", "da86b0", "db7698", "dc6a7d", "dd5a5a", "dc514c", "da493e",
  "da4534", "d93e26", "dc3f23", "c03a1b", "b23415", "a23010", "9b2b0c", "932b0b",
  "892608", "832607", "752105", "6c1e04", "601a02", "5a1802", "521700", "4f0a01",
  "4a0207", "470313", "42041e", "400427", "3d0630", "380635", "300636", "250630",
  "1c072e",
];
export const gddlColor = (tier: number): string | null =>
  Number.isInteger(tier) && tier >= 1 && tier < tierColors.length ? `#${tierColors[tier]}` : null;

const normalized = (name: string): string => name.toLowerCase().replace(/[^a-z0-9]/g, "");
interface OfficialMap { id: number; stars: number | null; seconds: number; gddlId?: number }
// Original GD difficulty, NOT its campaign reward (e.g. xStep rewards 10 stars).
// Completion times: https://geometrydash.wiki.gg/wiki/<level>. GDDL's measured
// Meta.seconds supersedes these fallback times for the three official demons.
const officialRows: readonly (readonly [string, number, number | null, number, number?])[] = [
  ["Stereo Madness", 1, 2, 89], ["Back On Track", 2, 2, 84], ["Polargeist", 3, 3, 93],
  ["Dry Out", 4, 3, 84], ["Base After Base", 5, 4, 86], ["Cant Let Go", 6, 4, 83],
  ["Jumper", 7, 6, 89], ["Time Machine", 8, 6, 99], ["Cycles", 9, 8, 82],
  ["xStep", 10, 8, 84], ["Clutterfunk", 11, 8, 99], ["Theory of Everything", 12, 8, 86],
  ["Electroman Adventures", 13, 6, 88], ["Clubstep", 14, null, 90, 1],
  ["Electrodynamix", 15, 8, 84], ["Hexagon Force", 16, 8, 92], ["Blast Processing", 17, 6, 102],
  ["Theory of Everything II", 18, null, 92, 2], ["Geometrical Dominator", 19, 6, 100],
  ["Deadlocked", 20, null, 99, 3], ["Fingerdash", 21, 8, 85], ["Dash", 22, 8, 96],
];
const officialMaps = new Map<string, OfficialMap>(officialRows.map(([name, id, stars, seconds, gddlId]) =>
  [normalized(name), { id, stars, seconds, ...(gddlId ? { gddlId } : {}) }]));
officialMaps.set("theoryofeverything2", officialMaps.get("theoryofeverythingii")!);

export const officialMap = (name: string): OfficialMap | undefined => officialMaps.get(normalized(name));
const positive = (value: unknown): number | null => typeof value === "number" && Number.isFinite(value) && value > 0 ? value : null;
export const explicitLengthSeconds = (text: string | undefined): number | null => {
  if (!text) return null;
  const match = /^\s*(?:(\d+)\s*m\s*)?(\d+(?:\.\d+)?)\s*s\s*$/i.exec(text);
  return match ? positive(Number(match[1] ?? 0) * 60 + Number(match[2])) : null;
};

export interface GdMapDetails {
  id: string; creator: string; difficulty: string;
  demon: boolean; stars: number | null; verificationSeconds: number | null;
}
/** Server field 57 is 240Hz verification time; 15 is only Tiny..XL, 46 is editor time. */
export const parseGdMaps = (body: string): GdMapDetails[] => {
  const sections = body.split("#"), creators = new Map<string, string>();
  for (const creator of sections[1]?.split("|") ?? []) {
    const [id, name] = creator.split(":"); if (id && name) creators.set(id, name);
  }
  const result: GdMapDetails[] = [];
  for (const row of sections[0]?.split("|") ?? []) {
    const fields = row.split(":"), data = new Map<string, string>();
    for (let i = 0; i + 1 < fields.length; i += 2) data.set(fields[i]!, fields[i + 1]!);
    const id = data.get("1"); if (!id || !/^[1-9]\d*$/.test(id)) continue;
    const demon = data.get("17") === "1", stars = Number(data.get("18"));
    const average = Number(data.get("9")) / Number(data.get("8"));
    const labels = ["N/A", "Easy", "Normal", "Hard", "Harder", "Insane"];
    const demonLabels: Record<string, string> = { "3": "Easy Demon", "4": "Medium Demon", "5": "Insane Demon", "6": "Extreme Demon" };
    result.push({ id, creator: creators.get(data.get("6") ?? "") ?? "",
      demon, stars: Number.isInteger(stars) && stars >= 0 && stars <= 10 ? stars : null,
      difficulty: demon ? demonLabels[data.get("43") ?? ""] ?? "Hard Demon" : data.get("25") === "1" ? "Auto" : labels[average] ?? "N/A",
      verificationSeconds: positive(Number(data.get("57"))) !== null ? Number(data.get("57")) / 240 : null });
  }
  return result;
};

export const parseGddl = (body: unknown, expectedId: number): {
  tier: number | null; seconds: number | null; difficulty: string; creator: string;
} | null => {
  if (!body || typeof body !== "object") return null;
  const row = body as { ID?: unknown; Rating?: unknown; DefaultRating?: unknown;
    Meta?: { seconds?: unknown; Difficulty?: unknown; Publisher?: { name?: unknown } } };
  if (row.ID !== expectedId) return null;
  const raw = positive(row.Rating) ?? positive(row.DefaultRating), rounded = raw !== null ? Math.round(raw) : null;
  const difficulties = new Map(["Easy", "Medium", "Hard", "Insane", "Extreme"].map(name => [name, `${name} Demon`]));
  return { tier: rounded !== null && rounded >= 1 && rounded < tierColors.length ? rounded : null,
    seconds: positive(row.Meta?.seconds),
    difficulty: typeof row.Meta?.Difficulty === "string" ? difficulties.get(row.Meta.Difficulty) ?? "" : "",
    creator: typeof row.Meta?.Publisher?.name === "string" ? row.Meta.Publisher.name : "" };
};

type Resolved = { metadata: RankedMapMetadata; creator: string; difficulty: string };
interface Cached { value: Resolved; fetchedAt: number }
const unknownMetadata = (): RankedMapMetadata => ({ kind: "UNKNOWN", stars: null, gddlTier: null,
  gddlColor: null, durationSeconds: null, officialLevelId: null });

/** One catalog lookup, bounded enrichment, cached across refreshes and server restarts.
 * Failure affects display only. Existing map IDs, pool/qualifying and in-flight snapshots stay exact. */
export class RankedMapMetadataResolver {
  private readonly cache = new Map<string, Cached>();
  public constructor(private readonly fetcher: typeof fetch = fetch, private readonly database?: SqlExecutor,
    private readonly budgetMs = 8_000) {}

  public async enrich(document: RankedConfigDocument): Promise<RankedConfigDocument> {
    const key = (map: RankedMap): string => `${map.playableLevelId}:${officialMap(map.title)?.id ?? 0}`;
    if (this.database) {
      const rows = await this.database.query<{ cache_key: string; data: Resolved; fetched_at: Date | string }>(
        "SELECT cache_key,data,fetched_at FROM ranked_map_metadata_cache WHERE cache_key=ANY($1::text[])",
        [document.maps.map(key)]).catch(() => ({ rows: [], rowCount: 0 }));
      for (const row of rows.rows) if (!this.cache.has(row.cache_key)) this.cache.set(row.cache_key,
        { value: row.data, fetchedAt: new Date(row.fetched_at).getTime() });
    }
    const now = Date.now(), results = new Map<string, Resolved>();
    const pending = document.maps.filter(map => {
      const cached = this.cache.get(key(map));
      if (cached) results.set(key(map), cached.value);
      const complete = cached && cached.value.metadata.kind !== "UNKNOWN" &&
        cached.value.metadata.durationSeconds !== null &&
        (cached.value.metadata.kind !== "DEMON" || cached.value.metadata.gddlTier !== null);
      return map.active && (!cached || now - cached.fetchedAt >= (complete ? 86_400_000 : 300_000));
    });
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), this.budgetMs);
    const gd = new Map<string, GdMapDetails>();
    const request = async (url: string, init: RequestInit = {}): Promise<Response> => {
      const response = await this.fetcher(url, { ...init,
        signal: AbortSignal.any([controller.signal, AbortSignal.timeout(3_000)]) });
      if (!response.ok) throw new Error(`Map metadata HTTP ${response.status}`);
      return response;
    };
    const gdRequest = (path: string, fields: Record<string, string>): Promise<Response> => request(
      `https://www.boomlings.com/database/${path}`, { method: "POST",
        headers: { "content-type": "application/x-www-form-urlencoded", "user-agent": "" },
        body: new URLSearchParams({ gameVersion: "22", binaryVersion: "45", secret: "Wmfd2893gb7", ...fields }).toString() });
    try {
      const online = [...new Set(pending.filter(m => !officialMap(m.title)).map(m => m.playableLevelId))];
      for (let offset = 0; offset < online.length && !controller.signal.aborted; offset += 50) {
        const ids = online.slice(offset, offset + 50);
        try {
          const rows = parseGdMaps(await (await gdRequest("getGJLevels21.php", { type: "19", str: ids.join(",") })).text());
          // Do not spend the entire shared budget repeating an unavailable GD
          // endpoint: leave time for independent GDDL recovery of UNKNOWN IDs.
          if (!rows.length) break;
          for (const map of rows) if (ids.includes(map.id)) gd.set(map.id, map);
        } catch { break; /* The last known metadata remains usable. */ }
      }
      let next = 0;
      const worker = async (): Promise<void> => {
        while (next < pending.length && !controller.signal.aborted) {
          const map = pending[next++]!, official = officialMap(map.title), native = gd.get(map.playableLevelId);
          let metadata = { ...unknownMetadata(), ...(results.get(key(map))?.metadata ?? {}) };
          if (official) metadata = { ...metadata, kind: official.gddlId ? "DEMON" : "NORMAL", stars: official.stars,
            officialLevelId: official.id, durationSeconds: metadata.durationSeconds ?? official.seconds };
          else if (native) metadata = { ...metadata, kind: native.demon ? "DEMON" : "NORMAL", stars: native.demon ? null : native.stars,
            durationSeconds: native.verificationSeconds ?? metadata.durationSeconds };
          const resolved: Resolved = { metadata, creator: official ? "RobTop" : native?.creator ?? results.get(key(map))?.creator ?? "",
            difficulty: official ? official.gddlId ? "Demon" : official.stars === 8 ? "Insane" : official.stars === 6 ? "Harder" : official.stars === 4 ? "Hard" : official.stars === 3 ? "Normal" : "Easy" : native?.difficulty ?? results.get(key(map))?.difficulty ?? "" };
          results.set(key(map), resolved);
          try {
            // A failed GD catalog lookup leaves UNKNOWN, not a normal level.
            // GDDL can still supply its own exact ID, tier and original demon
            // difficulty; do not gate that recovery on a successful GD request.
            if (metadata.kind === "DEMON" || metadata.kind === "UNKNOWN") {
              const id = official?.gddlId ?? Number(map.playableLevelId);
              const rating = parseGddl(await (await request(`https://gdladder.com/api/levels/${id}`)).json(), id);
              if (rating) {
                metadata = { ...metadata,
                  kind: rating.tier !== null || rating.difficulty ? "DEMON" : metadata.kind,
                  gddlTier: rating.tier ?? metadata.gddlTier,
                  gddlColor: rating.tier ? gddlColor(rating.tier) : metadata.gddlColor,
                  durationSeconds: rating.seconds ?? metadata.durationSeconds };
                resolved.metadata = metadata;
                if (!resolved.difficulty || resolved.difficulty === "Demon") resolved.difficulty = rating.difficulty || resolved.difficulty;
                if (!resolved.creator) resolved.creator = rating.creator;
              }
            }
          } catch { /* Retain known metadata when GDDL is unavailable. */ }
          try {
            if (!official && metadata.durationSeconds === null && !controller.signal.aborted) {
              const download = parseGdMaps(await (await gdRequest("downloadGJLevel22.php", { levelID: map.playableLevelId })).text());
              const duration = download.find(m => m.id === map.playableLevelId)?.verificationSeconds;
              if (duration) resolved.metadata = { ...resolved.metadata, durationSeconds: duration };
            }
          } catch { /* Never invent a Tier or exact duration from a length category. */ }
          this.cache.set(key(map), { value: structuredClone(resolved), fetchedAt: now });
          if (this.cache.size > 1024) this.cache.delete(this.cache.keys().next().value!);
          if (this.database && resolved.metadata.kind !== "UNKNOWN") await this.database.query(
            "INSERT INTO ranked_map_metadata_cache(cache_key,data,fetched_at) VALUES ($1,$2::jsonb,$3) ON CONFLICT(cache_key) DO UPDATE SET data=EXCLUDED.data,fetched_at=EXCLUDED.fetched_at",
            [key(map), JSON.stringify(resolved), new Date(now).toISOString()]).catch(() => undefined);
        }
      };
      await Promise.all(Array.from({ length: Math.min(4, pending.length) }, () => worker()));
    } finally { clearTimeout(timer); }
    const maps = document.maps.map(map => {
      const info = results.get(key(map)), official = officialMap(map.title);
      const native = gd.get(map.playableLevelId);
      const fallback: RankedMapMetadata = { ...unknownMetadata(), ...(official ? {
        kind: official.gddlId ? "DEMON" : "NORMAL", stars: official.stars,
        officialLevelId: official.id, durationSeconds: official.seconds,
      } : native ? { kind: native.demon ? "DEMON" : "NORMAL", stars: native.demon ? null : native.stars,
        durationSeconds: native.verificationSeconds } : {}) };
      return { ...map, creator: map.creator || info?.creator || (official ? "RobTop" : ""),
        difficulty: map.difficulty || info?.difficulty || "", metadata: { ...(info?.metadata ?? fallback),
          durationSeconds: explicitLengthSeconds(map.length) ?? info?.metadata.durationSeconds ?? fallback.durationSeconds } };
    });
    // Persisted config snapshots deduplicate by generation. A display change
    // needs a new immutable snapshot; active games retain their old catalog.
    const fingerprint = createHash("sha256").update(JSON.stringify(maps.map(map =>
      [map.playableLevelId,map.creator,map.difficulty,map.metadata]))).digest("hex").slice(0,16);
    const generation = `${document.generation}:map-v1:${fingerprint}`;
    return { ...document, generation, operational: { ...document.operational,generation }, maps };
  }
}
