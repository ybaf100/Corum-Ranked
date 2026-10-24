import { afterEach, describe, expect, it, vi } from 'vitest';
import { DatabaseWorkSignals, DemandWorker, signalAfterCommit } from '../src/database/database-work.js';

afterEach(()=>vi.useRealTimers());
const executor=()=>({workSignals:new DatabaseWorkSignals(),query:vi.fn(async()=>({rows:[],rowCount:0}))});
describe('committed activity workers',()=>{
 it('reconciles once, makes no idle SQL calls for ten minutes, and resumes at 250ms',async()=>{
  vi.useFakeTimers();const db=executor();let work=false;
  const run=vi.fn(async()=>{await db.query();return work;}),worker=new DemandWorker(db,'survival',250,run,()=>{});
  await worker.start();expect(run).toHaveBeenCalledTimes(1);expect(worker.active).toBe(false);
  await vi.advanceTimersByTimeAsync(600000);expect(db.query).toHaveBeenCalledTimes(1);
  work=true;db.workSignals.notify('survival');await vi.advanceTimersByTimeAsync(750);
  expect(run).toHaveBeenCalledTimes(5);expect(worker.active).toBe(true);
  work=false;await vi.advanceTimersByTimeAsync(250);expect(worker.active).toBe(false);
  const count=run.mock.calls.length;await vi.advanceTimersByTimeAsync(600000);expect(run).toHaveBeenCalledTimes(count);
  await worker.stop();db.workSignals.notify('survival');expect(worker.active).toBe(false);
 });
 it('defers wakeup until commit and discards the hook on rollback',()=>{
  const db=executor(),committed:Array<()=>void>=[],wake=vi.fn();db.workSignals.subscribe('survival',wake);
  signalAfterCommit({...db,afterCommit:callback=>committed.push(callback)},'survival');expect(wake).not.toHaveBeenCalled();
  committed.splice(0).forEach(callback=>callback());expect(wake).toHaveBeenCalledTimes(1);
  signalAfterCommit({...db,afterCommit:callback=>committed.push(callback)},'survival');committed.length=0;
  expect(wake).toHaveBeenCalledTimes(1);
 });
 it('does not lose a concurrent commit while an empty scan is in flight',async()=>{
  vi.useFakeTimers();const db=executor();let resolve!:(value:boolean)=>void;
  const run=vi.fn().mockImplementationOnce(()=>new Promise<boolean>(r=>{resolve=r;})).mockResolvedValue(false);
  const worker=new DemandWorker(db,'survival',250,run,()=>{}),started=worker.start();
  db.workSignals.notify('survival');resolve(false);await started;expect(worker.active).toBe(true);
  await vi.advanceTimersByTimeAsync(250);expect(run).toHaveBeenCalledTimes(2);expect(worker.active).toBe(false);await worker.stop();
 });
 it('retries an unavailable database at the active cadence and parks only after successful reconciliation',async()=>{
  vi.useFakeTimers();const error=vi.fn(),run=vi.fn().mockRejectedValueOnce(new Error('connection unavailable')).mockResolvedValue(false);
  const worker=new DemandWorker(executor(),'outbox',500,run,error);await worker.start();expect(worker.active).toBe(true);
  await vi.advanceTimersByTimeAsync(500);expect(error).toHaveBeenCalledTimes(1);expect(run).toHaveBeenCalledTimes(2);expect(worker.active).toBe(false);await worker.stop();
 });
});
