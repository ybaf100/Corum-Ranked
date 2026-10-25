import {PGlite} from '@electric-sql/pglite';
import {it,expect} from 'vitest';
import {PgliteDatabase} from './pglite-database.js';
import {readSurvivalRecords} from '../src/survival/survival-records.js';
const id=(n:number)=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
it('counts only confirmed champions as wins/streaks and preserves visibility/settlement boundaries in PostgreSQL',async()=>{
 const pg=new PGlite(),db=new PgliteDatabase(pg),player=id(1);
 try{
  await pg.exec(`CREATE TABLE sur_profiles(player_id uuid,history_visible_since timestamptz);
   CREATE TABLE sur_matches(id uuid,state text,finished_at timestamptz);
   CREATE TABLE sur_match_players(player_id uuid,match_id uuid,final_rank int);
   CREATE TABLE sur_online_players(player_id uuid,match_id uuid,settled_at timestamptz);
   CREATE TABLE sur_debug_matches(match_id uuid,settled_at timestamptz);`);
  await db.query('INSERT INTO sur_profiles VALUES($1,$2)',[player,'2026-01-01']);
  const ranks=[1,2,1,1];
  for(let n=0;n<ranks.length;n++){
   await db.query('INSERT INTO sur_matches VALUES($1,$2,$3)',[id(n+10),'FINAL_RESULT',`2026-01-0${n+2}T00:00:00Z`]);
   await db.query('INSERT INTO sur_match_players VALUES($1,$2,$3)',[player,id(n+10),ranks[n]]);
  }
  expect((await readSurvivalRecords(db,[player]))[player]).toEqual({completedMatches:4,bestRank:1,wins:3,losses:1,winStreak:2});
  await db.query('UPDATE sur_match_players SET final_rank=3 WHERE match_id=$1',[id(13)]);
  expect((await readSurvivalRecords(db,[player]))[player]).toMatchObject({completedMatches:4,wins:2,losses:2,winStreak:0});
  await db.query('INSERT INTO sur_online_players VALUES($1,$2,NULL)',[player,id(13)]);
  expect((await readSurvivalRecords(db,[player]))[player]).toMatchObject({completedMatches:3,wins:2,losses:1,winStreak:1});
  await db.query('UPDATE sur_profiles SET history_visible_since=$1',['2026-01-03T12:00:00Z']);
  expect((await readSurvivalRecords(db,[player]))[player]).toMatchObject({completedMatches:1,wins:1,losses:0,winStreak:1});
  await db.query('UPDATE sur_matches SET state=$1 WHERE id=$2',['PLAYING',id(12)]);
  expect((await readSurvivalRecords(db,[player]))[player]).toMatchObject({completedMatches:0,wins:0,losses:0,winStreak:0});
 }finally{await pg.close();}
},30_000);
