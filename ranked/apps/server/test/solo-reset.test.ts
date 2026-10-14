import { readFile,readdir } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import {it,expect} from "vitest";
it("the authorized one-time reset preserves account, Survival and common data and refuses active Solo",async()=>{
 const db=new PGlite();try{
  const dir=new URL("../../../migrations/",import.meta.url);
  for(const file of (await readdir(dir)).filter(n=>n.endsWith('.sql')).sort())await db.exec(await readFile(new URL(file,dir),"utf8"));
  const id="00000000-0000-4000-8000-000000000001";
  await db.query("INSERT INTO ranked_players(id,gd_account_id,gd_username) VALUES($1,1,'reset-test')",[id]);
  await db.query("INSERT INTO ranked_profiles(player_id,displayed_tier,visible_ranked_score,total_ap,wins) VALUES($1,'SILVER1',1150,123,15)",[id]);
  await db.query("INSERT INTO sur_profiles(player_id,lp,displayed_tier) VALUES($1,725,'BRONZE')",[id]);
  const survival=(await db.query("SELECT * FROM sur_profiles")).rows;
  const script=await readFile(new URL("../../../tools/reset-solo-beta69.sql",import.meta.url),"utf8");
  await db.exec(script);
  expect((await db.query("SELECT displayed_tier,visible_ranked_score,hidden_mmr,wins,total_ap FROM ranked_profiles")).rows[0])
    .toEqual({displayed_tier:"UNRANKED",visible_ranked_score:null,hidden_mmr:null,wins:0,total_ap:123});
  expect((await db.query("SELECT * FROM sur_profiles")).rows).toEqual(survival);
  expect((await db.query("SELECT count(*)::int AS n FROM ranked_players")).rows[0]).toEqual({n:1});
  await db.exec(script); // Safe when deliberately re-run while idle.
  await db.query("INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at) VALUES($1,$1,'reset-token','v0.5.0-beta.69','{}',now()+interval '1 hour',now())",[id]);
  await db.query(`INSERT INTO ranked_queue_entries(player_id,session_id,hidden_mmr_snapshot,joined_at,last_heartbeat_at,status)
   VALUES($1,$1,1000,now(),now(),'QUEUED')`,[id]);
  await expect(db.exec(script)).rejects.toThrow("Solo reset refused");
  expect((await db.query("SELECT count(*)::int AS n FROM ranked_queue_entries")).rows[0]).toEqual({n:1});
 } finally {await db.close();}
},60000);
