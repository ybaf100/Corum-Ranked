import { describe, expect, it, vi } from "vitest";
import { CONFIG_READS_FRESH_MS, RankedConfigReadsService } from "../src/config/ranked-config-reads.service.js";
import type { SqlExecutor } from "../src/database/database.port.js";

const audio = (songId: number) => ({ enabled: true, fadeInSeconds: 0.3, fadeOutSeconds: 0.3, resources: [{ key: "menu", label: "Lobby", songId, startSeconds: 0, loop: true }] });
const client = { audio: { enabled: true, fadeInSeconds: 0.3, fadeOutSeconds: 0.3, resources: [{ key: "menu", label: "Lobby", songId: 7, startSeconds: 0, loop: true }] },
  ui: { fadeInSeconds: 0.2, fadeOutSeconds: 0.2 } } as never;

const rig = () => {
  let now = 1_000_000;
  let volume = 80;
  const waiting: (() => void)[] = [];
  let hold = false;
  const query = vi.fn(async (sql: string) => {
    if (hold) await new Promise<void>((resolve) => { waiting.push(resolve); });
    if (sql.includes("ranked_audio_volumes")) return { rows: [{ song_id: 7, volume_percent: volume }], rowCount: 1 };
    return { rows: [{ mode: "SURVIVAL", preseason: false, season_number: 2, target_at: null }], rowCount: 1 };
  });
  const reads = new RankedConfigReadsService({ query } as unknown as SqlExecutor, { now: () => new Date(now) });
  return {
    reads, query,
    advance: (ms: number) => { now += ms; },
    setVolume: (value: number) => { volume = value; },
    hold: () => { hold = true; },
    release: () => { hold = false; for (const resume of waiting.splice(0)) resume(); },
  };
};

describe("cached database reads of GET /config", () => {
  it("reads once, then serves fresh values without touching the database", async () => {
    const r = rig();
    await expect(r.reads.read(client)).resolves.toMatchObject({ volumes: [{ songId: 7, volumePercent: 80 }], seasons: { SURVIVAL: { number: 2 } } });
    expect(r.query).toHaveBeenCalledTimes(2);
    r.advance(CONFIG_READS_FRESH_MS - 1);
    await r.reads.read(client);
    expect(r.query).toHaveBeenCalledTimes(2);
  });

  it("answers a stale request immediately while a background read (which wakes Neon) refreshes it", async () => {
    const r = rig();
    await r.reads.read(client);
    r.advance(CONFIG_READS_FRESH_MS);
    r.setVolume(40);
    r.hold();
    await expect(r.reads.read(client)).resolves.toMatchObject({ volumes: [{ volumePercent: 80 }] });
    expect(r.query).toHaveBeenCalledTimes(4);
    r.release();
    await vi.waitFor(async () => expect((await r.reads.read(client)).volumes[0]!.volumePercent).toBe(40));
  });

  it("shows an admin change on the next request after invalidation", async () => {
    const r = rig();
    await r.reads.read(client);
    r.setVolume(10);
    r.reads.invalidate();
    await expect(r.reads.read(client)).resolves.toMatchObject({ volumes: [{ volumePercent: 10 }] });
  });

  it("re-reads when the configured songs change", async () => {
    const r = rig();
    await r.reads.read(client);
    const other = { audio: audio(8), ui: { fadeInSeconds: 0.2, fadeOutSeconds: 0.2 } } as never;
    await r.reads.read(other);
    expect(r.query).toHaveBeenCalledTimes(4);
  });
});
