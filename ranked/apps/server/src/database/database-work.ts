import type { SqlExecutor } from './database.port.js';

export type WorkDomain = 'survival' | 'survival-queue' | 'debug-bot' | 'outbox';
/** Process-local hints for the single authoritative writer. Durable rows remain
 * authoritative; every worker does one startup reconciliation after a restart. */
export class DatabaseWorkSignals {
  private readonly listeners = new Map<WorkDomain, Set<() => void>>();
  public subscribe(domain: WorkDomain, callback: () => void): () => void {
    const group = this.listeners.get(domain) ?? new Set();
    group.add(callback); this.listeners.set(domain, group);
    return () => { group.delete(callback); };
  }
  public notify(domain: WorkDomain): void { for (const callback of this.listeners.get(domain) ?? []) callback(); }
}
const signals = new WeakMap<SqlExecutor, DatabaseWorkSignals>();
export function databaseWork(executor: SqlExecutor): DatabaseWorkSignals {
  if (executor.workSignals) return executor.workSignals;
  let value = signals.get(executor);
  if (!value) { value = new DatabaseWorkSignals(); signals.set(executor, value); }
  return value;
}
export function signalAfterCommit(executor: SqlExecutor, domain: WorkDomain): void {
  const notify = () => databaseWork(executor).notify(domain);
  if (executor.afterCommit) executor.afterCommit(notify);
  else notify(); // Lightweight isolated test executors; production always defers.
}

/** No idle database watchdog. Only a committed business event or startup wakes
 * this worker. Keep the existing active cadence, including the 250ms checkpoint.
 * Generation fencing prevents a concurrent enqueue from losing its wake-up. */
export class DemandWorker {
  private timer: ReturnType<typeof setInterval> | null = null;
  private pending: Promise<void> | null = null;
  private started = false;
  private generation = 0;
  private readonly unsubscribe: () => void;
  public constructor(executor: SqlExecutor, domain: WorkDomain, private readonly period: number,
    private readonly run: () => Promise<boolean>, private readonly error: (e: unknown) => void) {
    this.unsubscribe = databaseWork(executor).subscribe(domain, () => { void this.wake(); });
  }
  public async start(): Promise<void> { this.started = true; await this.wake(); }
  public wake(): Promise<void> {
    ++this.generation;
    if (!this.started) return Promise.resolve();
    this.arm(); return this.cycle();
  }
  private arm(): void {
    if (this.timer || !this.started) return;
    this.timer = setInterval(() => { void this.cycle(); }, this.period); this.timer.unref();
  }
  private cycle(): Promise<void> {
    if (this.pending) return this.pending;
    if (!this.started) return Promise.resolve();
    const generation = this.generation;
    this.pending = this.run().then(more => {
      if (!more && generation === this.generation && this.timer) { clearInterval(this.timer); this.timer = null; }
    }).catch(e => { this.error(e); this.arm(); }).finally(() => { this.pending = null; });
    return this.pending;
  }
  public async stop(): Promise<void> {
    this.started = false; this.unsubscribe();
    if (this.timer) clearInterval(this.timer); this.timer = null;
    await this.pending;
  }
  public get active(): boolean { return this.timer !== null; }
}
