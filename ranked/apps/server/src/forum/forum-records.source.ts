export const FORUM_RECORDS_SOURCE = Symbol('FORUM_RECORDS_SOURCE');
export type ForumProvider = 'GDKF' | 'POINTERCRATE';
export type ForumStatus = 'PENDING' | 'RANKED' | 'UNRANKED' | 'UNLINKED' | 'ERROR';
export interface ForumRecord {
  provider: ForumProvider;
  externalId: number | null;
  externalName: string | null;
  rank: number | null;
  status: ForumStatus;
}
export interface ForumRecordsSource {
  fetch(provider: ForumProvider, username: string, externalId: number | null): Promise<ForumRecord>;
}
export const COJONG = { accountId: '16792985', username: 'CoJong', GDKF: 3251, POINTERCRATE: 60108 } as const;
export const FORUM_PROVIDERS: readonly ForumProvider[] = ['GDKF', 'POINTERCRATE'];
const positive = (value: unknown): number | null => {
  const number = Number(value);
  return Number.isSafeInteger(number) && number > 0 ? number : null;
};
const object = (value: unknown): Record<string, unknown> =>
  value && typeof value === 'object' ? value as Record<string, unknown> : {};
const normalizedName = (name: string): string => name.trim().toLocaleLowerCase('en-US');
// Pointercrate often prefixes a public team tag. Do not fuzzy-match arbitrary
// substrings: multiple exact canonical candidates must remain unlinked.
export const canonicalPointercrateName = (name: string): string =>
  normalizedName(name.replace(/^(?:\[[^\]]+\]\s*)+/, ''));

export function parseGdkfRecord(payload: unknown, username: string, externalId: number | null): ForumRecord {
  const root = object(payload);
  if (root.status !== 'success') throw new Error('GDKF returned an unsuccessful response');
  const data = object(root.data);
  const id = positive(data.user_id);
  const name = typeof data.nickname === 'string' ? data.nickname : '';
  if (!id || !name) throw new Error('GDKF returned incomplete player data');
  if (externalId ? id !== externalId : normalizedName(name) !== normalizedName(username)) {
    return { provider: 'GDKF', externalId: null, externalName: null, rank: null, status: 'UNLINKED' };
  }
  const rank = positive(object(data.demonlist).rank);
  return { provider: 'GDKF', externalId: id, externalName: name, rank, status: rank ? 'RANKED' : 'UNRANKED' };
}

export function parsePointercrateRecord(payload: unknown, username: string, externalId: number | null): ForumRecord {
  if (!Array.isArray(payload)) throw new Error('Pointercrate returned an invalid ranking response');
  const candidates = payload.map(object).filter((entry) => externalId
    ? positive(entry.id) === externalId
    : typeof entry.name === 'string' && canonicalPointercrateName(entry.name) === normalizedName(username));
  if (candidates.length !== 1) return {
    provider: 'POINTERCRATE', externalId, externalName: null, rank: null,
    status: candidates.length > 1 ? 'UNLINKED' : 'UNRANKED',
  };
  const player = candidates[0]!;
  const id = positive(player.id);
  const name = typeof player.name === 'string' ? player.name : '';
  if (!id || !name) throw new Error('Pointercrate returned incomplete player data');
  const rank = player.banned ? null : positive(player.rank);
  return { provider: 'POINTERCRATE', externalId: id, externalName: name, rank, status: rank ? 'RANKED' : 'UNRANKED' };
}

export class PublicForumRecordsSource implements ForumRecordsSource {
  private readonly requests = new Map<ForumProvider, number[]>();
  private readonly blockedUntil = new Map<ForumProvider, number>();
  public constructor(private readonly request: typeof fetch = fetch) {}

  private async json(provider: ForumProvider, url: URL): Promise<{ data: unknown; next: URL | null }> {
    const now = Date.now();
    const recent = (this.requests.get(provider) ?? []).filter((at) => at > now - 1_800_000);
    // GDKF's public quota is 1000 / 30 minutes / IP. Keep some headroom;
    // database refresh leases also deduplicate overlapping server instances.
    if ((this.blockedUntil.get(provider) ?? 0) > now || recent.length >= 900) throw new Error('Forum API cooldown');
    recent.push(now); this.requests.set(provider, recent);
    const response = await this.request(url, {
      headers: { Accept: 'application/json', 'User-Agent': 'Corum-Ranked/0.5 (+https://github.com/ybaf100/Corum-Ranked)' },
      signal: AbortSignal.timeout(8_000),
    });
    if (response.status === 429) {
      const retry = response.headers.get('retry-after') ?? '';
      const seconds = Number(retry);
      const until = retry && Number.isFinite(seconds)
        ? now + seconds * 1_000 : Date.parse(retry);
      this.blockedUntil.set(provider, Math.max(now + 60_000, Number.isFinite(until) ? until : 0));
    }
    if (response.status === 404) return { data: null, next: null };
    if (!response.ok) throw new Error(`Forum HTTP ${response.status}`);
    const text = await response.text();
    if (text.length > 512_000) throw new Error('Forum response is too large');
    const link = response.headers.get('link')?.match(/<([^>]+)>;\s*rel="?next"?/);
    const next = link ? new URL(link[1]!, url) : null;
    if (next && (next.origin !== url.origin || next.pathname !== url.pathname)) {
      throw new Error('Forum pagination URL is invalid');
    }
    return { data: JSON.parse(text) as unknown, next };
  }

  public async fetch(provider: ForumProvider, username: string, externalId: number | null): Promise<ForumRecord> {
    if (provider === 'GDKF') {
      const key = externalId ? `id/${externalId}` : `name/${encodeURIComponent(username)}`;
      const url = new URL(`https://gmdkoreaforum.com/api/users/${key}`);
      url.searchParams.set('config', 'demonlist');
      const { data: payload } = await this.json(provider, url);
      return payload === null
        ? { provider, externalId, externalName: null, rank: null, status: 'UNRANKED' }
        : parseGdkfRecord(payload, username, externalId);
    }
    let url = new URL('https://pointercrate.com/api/v1/players/ranking/');
    url.searchParams.set('name_contains', username);
    url.searchParams.set('limit', '100');
    const entries: unknown[] = [];
    // Bounded pagination avoids silently choosing the first substring result.
    for (let page = 0; page < 5; page += 1) {
      const { data: payload, next } = await this.json(provider, url);
      if (!Array.isArray(payload)) throw new Error('Pointercrate ranking unavailable');
      entries.push(...payload);
      if (!next) return parsePointercrateRecord(entries, username, externalId);
      // Ranking pagination uses an opaque pseudo-field, not the rank itself.
      url = next; url.searchParams.set('name_contains', username);
    }
    throw new Error('Pointercrate search is too broad');
  }
}
