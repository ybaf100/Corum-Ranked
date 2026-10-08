import { readFile } from 'node:fs/promises';
import { PGlite } from '@electric-sql/pglite';
import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';
import { ForumRecordsService } from '../src/forum/forum-records.service.js';
import { COJONG, PublicForumRecordsSource, parseGdkfRecord, parsePointercrateRecord,
  type ForumRecord, type ForumRecordsSource } from '../src/forum/forum-records.source.js';
import { MatchPlayerInfoController } from '../src/forum/match-player-info.controller.js';
import type { MatchAccessService } from '../src/match/match-access.service.js';
import type { RankedSessionContext } from '../src/session/session.types.js';
import { PgliteDatabase } from './pglite-database.js';

describe('public forum identity and API responses', () => {
  it('uses a unique exact name or a previously linked ID, including CoJong team prefixes', () => {
    const rows = [{ id: 60108, name: '[떡볶이과자] CoJong', rank: 241, banned: false },
      { id: 2, name: 'OtherCoJong', rank: 12, banned: false }];
    expect(parsePointercrateRecord(rows, 'CoJong', null)).toMatchObject({ externalId: 60108, rank: 241 });
    expect(parsePointercrateRecord([...rows, { id: 3, name: 'CoJong', rank: 9 }], 'CoJong', null).status).toBe('UNLINKED');
    expect(parsePointercrateRecord([{ id: 60108, name: 'Renamed', rank: 250 }], 'CoJong', 60108).rank).toBe(250);
    expect(parsePointercrateRecord([{ id: 2, name: 'OtherCoJong', rank: 12 }], 'CoJong', null).rank).toBeNull();
    expect(parseGdkfRecord({ status: 'success', data: { user_id: 3251, nickname: 'CoJong', demonlist: { rank: 18 } } }, 'cojong', null).rank).toBe(18);
    expect(parseGdkfRecord({ status: 'success', data: { user_id: 2, nickname: 'Other', demonlist: { rank: 1 } } }, 'CoJong', null).status).toBe('UNLINKED');
    expect(parsePointercrateRecord([{ id: 60108, name: 'CoJong', rank: 241, banned: true }], 'CoJong', null).rank).toBeNull();
    expect(() => parseGdkfRecord({ status: 'error' }, 'CoJong', null)).toThrow();
  });

  it('uses the public ranking filter and follows the opaque next Link instead of rank pagination', async () => {
    const urls: URL[] = [];
    const request = vi.fn(async (input: URL | RequestInfo) => {
      urls.push(new URL(String(input)));
      if (urls.length === 1) return new Response(JSON.stringify([{ id: 2, name: 'OtherCoJong', rank: 241 }]), {
        headers: { Link: '<https://pointercrate.com/api/v1/players/ranking/?after=opaque%2Fcursor>; rel="next"' },
      });
      return new Response(JSON.stringify([{ id: 60108, name: '[Team] CoJong', rank: 241 }]));
    }) as unknown as typeof fetch;
    const source = new PublicForumRecordsSource(request);
    expect((await source.fetch('POINTERCRATE', 'CoJong', null)).rank).toBe(241);
    expect(urls[0]!.searchParams.get('name_contains')).toBe('CoJong');
    expect(urls[1]!.searchParams.get('after')).toBe('opaque/cursor');
    expect(urls[1]!.searchParams.get('name_contains')).toBe('CoJong');
  });

  it('honors 429 cooldown and refuses pagination to another host', async () => {
    const request = vi.fn(async () => new Response('', { status: 429, headers: { 'Retry-After': '120' } })) as typeof fetch;
    const source = new PublicForumRecordsSource(request);
    await expect(source.fetch('GDKF', 'CoJong', null)).rejects.toThrow('429');
    await expect(source.fetch('GDKF', 'CoJong', null)).rejects.toThrow('cooldown');
    expect(request).toHaveBeenCalledTimes(1);
    const unsafe = new PublicForumRecordsSource(vi.fn(async () => new Response('[]', {
      headers: { Link: '<https://example.com/?after=1>; rel="next"' },
    })) as typeof fetch);
    await expect(unsafe.fetch('POINTERCRATE', 'CoJong', null)).rejects.toThrow('pagination URL');
  });
});

let client: PGlite, database: PgliteDatabase;
beforeAll(async () => {
  client = new PGlite(); database = new PgliteDatabase(client);
  await client.exec(await readFile(new URL('../../../migrations/0014_forum_records.sql', import.meta.url), 'utf8'));
  await client.exec(await readFile(new URL('../../../migrations/0015_forum_refresh_cooldown.sql', import.meta.url), 'utf8'));
}, 60_000);
afterAll(async () => { await client.close(); });

describe('durable forum cache', () => {
  it('limits manual refresh per account for one hour across workers/restarts, with an exact server deadline', async () => {
    let now = Date.parse('2026-10-10T00:00:00Z');
    let fail = false;
    const fetchRecord = vi.fn(async (provider: ForumRecord['provider'], username: string, id: number | null) => {
      if (fail && username === 'CooldownUser') throw new Error('API unavailable');
      return { provider, externalId: id ?? 42, externalName: username, rank: 10, status: 'RANKED' as const };
    });
    const createService = () => new ForumRecordsService(database, { now: () => new Date(now) }, { fetch: fetchRecord });
    const service = createService(), otherWorker = createService();
    const ownFetchCount = () => fetchRecord.mock.calls.filter(([, name]) => name === 'CooldownUser').length;
    const account = '12345';
    expect((await service.read(account)).refreshAvailableAt).toBeNull();
    const clicks = await Promise.all([service.refreshOwn(account, 'CooldownUser'), otherWorker.refreshOwn(account, 'CooldownUser')]);
    expect(clicks.filter(reply => reply.refreshAccepted)).toHaveLength(1);
    expect(clicks.every(reply => reply.refreshAvailableAt === '2026-10-10T01:00:00.000Z')).toBe(true);
    expect(ownFetchCount()).toBe(2);
    expect(await otherWorker.read(account)).toMatchObject({ refreshAvailableAt: '2026-10-10T01:00:00.000Z', serverNow: '2026-10-10T00:00:00.000Z' });
    now += 3_599_999;
    expect((await createService().refreshOwn(account, 'CooldownUser')).refreshAccepted).toBe(false);
    expect(ownFetchCount()).toBe(2);
    now += 1;
    fail = true;
    const failed = await otherWorker.refreshOwn(account, 'CooldownUser');
    expect(failed).toMatchObject({ refreshAccepted: true, refreshAvailableAt: '2026-10-10T02:00:00.000Z' });
    expect(failed.records.every(row => row.status === 'ERROR' && row.rank === 10)).toBe(true);
    expect(ownFetchCount()).toBe(4); // failure preserves ranks but still consumes the request
    expect((await createService().refreshOwn(account, 'CooldownUser')).refreshAccepted).toBe(false);
    const independent = createService();
    expect((await independent.refreshOwn('12346', 'OtherUser')).refreshAccepted).toBe(true);
    // Automatic initialization remains independent from manual user cooldowns.
    await service.refresh('12347', 'NewProfile', false);
    expect((await service.read('12347')).refreshAvailableAt).toBeNull();
    await Promise.all([service, otherWorker, independent].flatMap(worker =>
      [...(worker as unknown as { pending: Map<string, Promise<void>> }).pending.values()]));
    await database.query('DELETE FROM ranked_forum_records WHERE gd_account_id = $1', [COJONG.accountId]);
  });
  it('fetches once, reads instantly, deduplicates refreshes and preserves last good ranks on API failure', async () => {
    let now = Date.parse('2026-10-08T00:00:00Z');
    let fail = false;
    const fetchRecord = vi.fn(async (provider: ForumRecord['provider']) => {
      if (fail && provider === 'GDKF') throw new Error('timeout');
      return { provider, externalId: provider === 'GDKF' ? 3251 : 60108, externalName: 'CoJong',
        rank: provider === 'GDKF' ? 18 : 241, status: 'RANKED' as const };
    });
    const service = new ForumRecordsService(database, { now: () => new Date(now) }, { fetch: fetchRecord });
    await service.refresh('1234', 'CoJong', false);
    expect(fetchRecord).toHaveBeenCalledTimes(2);
    await service.refresh('1234', 'CoJong', false); await service.read('1234');
    expect(fetchRecord).toHaveBeenCalledTimes(2);
    now += 61_000;
    await service.refresh('1234', 'CoJong', false);
    expect(fetchRecord).toHaveBeenCalledTimes(2); // no TTL fetch on lobby re-entry
    fail = true;
    await Promise.all([service.refresh('1234', 'CoJong'), service.refresh('1234', 'CoJong')]);
    expect(fetchRecord).toHaveBeenCalledTimes(4);
    const gdkf = (await service.read('1234')).records.find((row) => row.provider === 'GDKF')!;
    expect(gdkf).toMatchObject({ rank: 18, status: 'ERROR', lastSuccessAt: '2026-10-08T00:00:00.000Z' });
    await service.refresh('1234', 'CoJong'); expect(fetchRecord).toHaveBeenCalledTimes(4);
  });

  it('always initializes CoJong without creating a Ranked player or rating profile', async () => {
    const source: ForumRecordsSource = { fetch: vi.fn(async (provider, _name, id) => ({
      provider, externalId: id, externalName: 'CoJong', rank: 1, status: 'RANKED' as const,
    })) };
    const service = new ForumRecordsService(database, { now: () => new Date('2026-10-09T00:00:00Z') }, source);
    service.onModuleInit();
    await Promise.all((service as unknown as { pending: Map<string, Promise<void>> }).pending.values());
    const records = await service.read(COJONG.accountId);
    expect(records.records.map((row) => row.externalId)).toEqual([3251, 60108]);
    expect(source.fetch).toHaveBeenCalledWith('GDKF', 'CoJong', 3251);
    expect(source.fetch).toHaveBeenCalledWith('POINTERCRATE', 'CoJong', 60108);
  });

  it('requires match authorization and selects CoJong only for the debug Bot side', async () => {
    const query = vi.fn(async () => ({ rows: [{ match_type: 'DEBUG_BOT', account_a: '1234',
      account_b: '9999', name_a: 'Human', name_b: 'BOT NORMAL' }], rowCount: 1 }));
    const authorize = vi.fn(async () => ({ side: 'A' }));
    const read = vi.fn(async (id: string) => ({ gdAccountId: id, records: [] }));
    const controller = new MatchPlayerInfoController({ query } as unknown as PgliteDatabase,
      { authorize } as unknown as MatchAccessService, { read } as unknown as ForumRecordsService);
    const session = { playerId: 'human' } as RankedSessionContext;
    expect(await controller.read('match', 'B', 'token', session)).toMatchObject({ gdAccountId: COJONG.accountId, gdUsername: 'CoJong', botReference: true });
    expect(await controller.read('match', 'A', 'token', session)).toMatchObject({ gdAccountId: '1234', botReference: false });
    authorize.mockRejectedValueOnce(new Error('unauthorized'));
    await expect(controller.read('match', 'B', 'bad', session)).rejects.toThrow('unauthorized');
    expect(query).toHaveBeenCalledTimes(2); expect(read).toHaveBeenCalledTimes(2);
    await expect(controller.read('match', 'C', 'token', session)).rejects.toThrow('Invalid player side');
  });
});
