import type { SqlExecutor } from "../database/database.port.js";
import type { RankedClientPresentationConfig } from "./ranked-config.document.js";

export interface RankedAudioVolume {
  readonly songId: number;
  readonly volumePercent: number;
}

/** Read current song settings from PostgreSQL, including defaults. No process cache. */
export async function readRankedAudioVolumes(
  client: RankedClientPresentationConfig | undefined,
  database?: SqlExecutor,
): Promise<readonly RankedAudioVolume[]> {
  const ids = [...new Set((client?.audio.resources ?? []).map(resource => resource.songId))];
  if (!ids.length) return [];
  const stored = database ? await database.query<{ song_id: number; volume_percent: number }>(
    "SELECT song_id,volume_percent FROM ranked_audio_volumes WHERE song_id=ANY($1::integer[])", [ids],
  ) : { rows: [] };
  const volumes = new Map(stored.rows.map(row => [row.song_id, row.volume_percent]));
  return ids.map(songId => ({ songId, volumePercent: volumes.get(songId) ?? 100 }));
}

export function withRankedAudioVolumes(
  client: RankedClientPresentationConfig | undefined,
  volumes: readonly RankedAudioVolume[],
): RankedClientPresentationConfig | undefined {
  if (!client) return undefined;
  const bySong = new Map(volumes.map(volume => [volume.songId, volume.volumePercent]));
  return { ...structuredClone(client), audio: { ...structuredClone(client.audio),
    resources: client.audio.resources.map(resource => ({ ...resource,
      volumePercent: bySong.get(resource.songId) ?? 100,
    })),
  } };
}
