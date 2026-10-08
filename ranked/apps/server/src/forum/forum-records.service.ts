import { Inject, Injectable, Logger, type OnModuleInit } from '@nestjs/common';
import { SERVER_CLOCK, type ServerClock } from '../common/runtime.module.js';
import { DATABASE, type DatabasePort } from '../database/database.port.js';
import { COJONG, FORUM_PROVIDERS, FORUM_RECORDS_SOURCE,
  type ForumRecordsSource, type ForumProvider, type ForumStatus } from './forum-records.source.js';

interface CacheRow {
  provider: ForumProvider;
  external_id: string | number | null;
  external_name: string | null;
  rank: number | null;
  status: ForumStatus;
  fetched_at: Date | string | null;
  last_success_at: Date | string | null;
}
const iso = (value: Date | string | null): string | null => value ? new Date(value).toISOString() : null;

@Injectable()
export class ForumRecordsService implements OnModuleInit {
  private readonly logger = new Logger(ForumRecordsService.name);
  private readonly pending = new Map<string, Promise<void>>();
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(FORUM_RECORDS_SOURCE) private readonly source: ForumRecordsSource,
  ) {}

  public onModuleInit(): void {
    // Always initialize/update CoJong, independent of Ranked membership.
    this.enqueue(COJONG.accountId, COJONG.username, true);
  }

  public enqueue(accountId: string, username: string, force = false): void {
    if (this.pending.has(accountId)) return;
    const work = this.refresh(accountId, username, force).then(() => undefined)
      .catch(() => { this.logger.warn('Forum record refresh failed; cached records were retained'); })
      .finally(() => { this.pending.delete(accountId); });
    this.pending.set(accountId, work);
  }

  public async read(accountId: string) {
    const result = await this.database.query<CacheRow>(
      `SELECT provider, external_id, external_name, rank, status, fetched_at, last_success_at
       FROM ranked_forum_records WHERE gd_account_id = $1 ORDER BY provider`, [accountId]);
    return { gdAccountId: accountId, records: FORUM_PROVIDERS.map((provider) => {
      const row = result.rows.find((candidate) => candidate.provider === provider);
      return { provider, externalId: row?.external_id ? Number(row.external_id) : null,
        externalName: row?.external_name ?? null, rank: row?.rank ?? null,
        status: row?.status ?? 'PENDING', fetchedAt: iso(row?.fetched_at ?? null),
        lastSuccessAt: iso(row?.last_success_at ?? null) };
    }), serverNow: this.clock.now().toISOString() };
  }

  public async refresh(accountId: string, username: string, force = true) {
    const now = this.clock.now();
    await Promise.all(FORUM_PROVIDERS.map(async (provider) => {
      const seedId = accountId === COJONG.accountId ? COJONG[provider] : null;
      await this.database.query(
        `INSERT INTO ranked_forum_records (gd_account_id, provider, gd_username, external_id)
         VALUES ($1, $2, $3, $4) ON CONFLICT (gd_account_id, provider) DO NOTHING`,
        [accountId, provider, username, seedId]);
      // Durable cooldown/lease: two HTTP clicks or overlapping Render workers
      // cannot both refresh the same source. A crashed worker's lease expires.
      const claimed = await this.database.query<CacheRow>(
        `UPDATE ranked_forum_records SET refresh_started_at = $4, gd_username = $3
         WHERE gd_account_id = $1 AND provider = $2
           AND (refresh_started_at IS NULL OR refresh_started_at <= $5)
           AND ($6 OR fetched_at IS NULL)
           AND (fetched_at IS NULL OR fetched_at <= $5)
         RETURNING provider, external_id, external_name, rank, status, fetched_at, last_success_at`,
        [accountId, provider, username, now.toISOString(), new Date(now.getTime() - 60_000).toISOString(), force]);
      const row = claimed.rows[0];
      if (!row) return;
      try {
        const lookupName = provider === 'POINTERCRATE' && row.external_id && row.external_name
          ? row.external_name : username;
        const record = await this.source.fetch(provider, lookupName, row.external_id ? Number(row.external_id) : null);
        await this.database.query(
          `UPDATE ranked_forum_records SET external_id = COALESCE($3, external_id), external_name = COALESCE($4, external_name),
             rank = $5, status = $6, fetched_at = $7, last_success_at = $7, refresh_started_at = NULL
           WHERE gd_account_id = $1 AND provider = $2 AND refresh_started_at = $8`,
          [accountId, provider, record.externalId, record.externalName, record.rank, record.status,
            this.clock.now().toISOString(), now.toISOString()]);
      } catch {
        await this.database.query(
          `UPDATE ranked_forum_records SET status = 'ERROR', fetched_at = $3, refresh_started_at = NULL
           WHERE gd_account_id = $1 AND provider = $2 AND refresh_started_at = $4`,
          [accountId, provider, this.clock.now().toISOString(), now.toISOString()]);
      }
    }));
    return this.read(accountId);
  }

  public async refreshOwn(accountId: string, username: string) {
    // Explicit refresh includes the bot reference as requested. Never refresh
    // another matched player's records on behalf of a viewer.
    const result = await this.refresh(accountId, username);
    if (accountId !== COJONG.accountId) this.enqueue(COJONG.accountId, COJONG.username, true);
    return result;
  }
}
