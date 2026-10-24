import { expect,it,vi } from 'vitest';
import { DatabaseService } from '../src/database/database.service.js';
import { DatabaseWorkSignals,signalAfterCommit } from '../src/database/database-work.js';
function fixture(failure:'statement'|'commit'|null){
 let stored=false;const signals=new DatabaseWorkSignals(),wake=vi.fn(),afterCommit=vi.fn();
 signals.subscribe('survival',wake);const released=vi.fn();
 const client={release:released,query:async(sql:string)=>{
  if(sql==='INSERT'&&failure==='statement')throw new Error('statement rolled back');
  if(sql==='COMMIT'){stored=true;if(failure==='commit')throw new Error('commit acknowledgement lost');}
  return {rows:[],rowCount:0};
 }};
 const db=Object.assign(Object.create(DatabaseService.prototype),{pool:{connect:async()=>client},workSignals:signals,logger:{warn:vi.fn()}}) as DatabaseService;
 const run=()=>db.transaction(async tx=>{signalAfterCommit(tx,'survival');tx.afterCommit!(afterCommit);await tx.query('INSERT');});
 return {run,wake,afterCommit,released,get stored(){return stored;}};
}
it('reconciles durable work if the connection drops after COMMIT, without running unconfirmed transaction callbacks',async()=>{
 const f=fixture('commit');await expect(f.run()).rejects.toThrow('commit acknowledgement lost');
 expect(f.stored).toBe(true);expect(f.wake).toHaveBeenCalledTimes(1);expect(f.afterCommit).not.toHaveBeenCalled();expect(f.released).toHaveBeenCalledTimes(1);
});
it('does not wake idle workers for a rolled-back statement',async()=>{
 const f=fixture('statement');await expect(f.run()).rejects.toThrow('statement rolled back');expect(f.stored).toBe(false);expect(f.wake).not.toHaveBeenCalled();expect(f.afterCommit).not.toHaveBeenCalled();
});
it('runs ordinary after-commit callbacks only after an acknowledged commit',async()=>{
 const f=fixture(null);await f.run();expect(f.stored).toBe(true);expect(f.wake).toHaveBeenCalledTimes(1);expect(f.afterCommit).toHaveBeenCalledTimes(1);
});
