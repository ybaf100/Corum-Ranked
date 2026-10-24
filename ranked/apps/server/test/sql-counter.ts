import type {DatabasePort,SqlExecutor} from "../src/database/database.port.js";
/** Counts calls reaching the actual SQL executor, including transaction queries. */
export function countSql(db:DatabasePort,isDiscovery:(sql:string)=>boolean){
 const originalQuery=db.query.bind(db),originalTransaction=db.transaction.bind(db);
 const metrics={queries:0,discovery:0,failures:0};
 const query=async<Row>(executor:SqlExecutor,sql:string,values:readonly unknown[]=[])=>{
  metrics.queries++;if(isDiscovery(sql))metrics.discovery++;
  try{return await executor.query<Row>(sql,values);}catch(error){metrics.failures++;throw error;}
 };
 db.query=<Row>(sql:string,values:readonly unknown[]=[])=>query<Row>({query:originalQuery},sql,values);
 db.transaction=<T>(operation:(tx:SqlExecutor)=>Promise<T>)=>originalTransaction(tx=>operation({...(tx.workSignals?{workSignals:tx.workSignals}:{}),...(tx.afterCommit?{afterCommit:tx.afterCommit.bind(tx)}:{}),query:<Row>(sql:string,values:readonly unknown[]=[])=>query<Row>(tx,sql,values)}));
 return {metrics,reset:()=>{metrics.queries=metrics.discovery=metrics.failures=0;},stop:()=>{db.query=originalQuery;db.transaction=originalTransaction;}};
}

/** Inject one executor failure at the chosen boundary, then resume the real DB. */
export function failSqlOnce(db:DatabasePort,matches:(sql:string)=>boolean){
 const originalQuery=db.query.bind(db),originalTransaction=db.transaction.bind(db);let failed=false;
 const query=<Row>(executor:SqlExecutor,sql:string,values:readonly unknown[]=[])=>{
  if(!failed&&matches(sql)){failed=true;return Promise.reject(new Error("Injected database failure"));}
  return executor.query<Row>(sql,values);
 };
 db.query=<Row>(sql:string,values:readonly unknown[]=[])=>query<Row>({query:originalQuery},sql,values);
 db.transaction=<T>(operation:(tx:SqlExecutor)=>Promise<T>)=>originalTransaction(tx=>operation({...(tx.workSignals?{workSignals:tx.workSignals}:{}),...(tx.afterCommit?{afterCommit:tx.afterCommit.bind(tx)}:{}),query:<Row>(sql:string,values:readonly unknown[]=[])=>query<Row>(tx,sql,values)}));
 return {get failed(){return failed;},stop:()=>{db.query=originalQuery;db.transaction=originalTransaction;}};
}
