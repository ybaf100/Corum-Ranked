import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { readRoundPace } from "../src/match/round-pace.js";
import { PgliteDatabase } from "./pglite-database.js";

const id = (n: number) => `00000000-0000-4000-8000-${String(n).padStart(12, "0")}`;
let client: PGlite;
let database: PgliteDatabase;
beforeAll(async () => {
  client = new PGlite(); database = new PgliteDatabase(client);
  await client.exec(`CREATE TABLE ranked_rounds (id uuid PRIMARY KEY, match_id uuid NOT NULL, round_number int NOT NULL,
      started_at timestamptz, normal_end_at timestamptz, result text, result_reason text, last_attempt_target text);
    CREATE TABLE ranked_attempts (id uuid PRIMARY KEY, round_id uuid NOT NULL, player_id uuid NOT NULL,
      ended_at timestamptz, cleared boolean NOT NULL DEFAULT FALSE, valid boolean NOT NULL DEFAULT TRUE);`);
}, 60_000);
afterAll(async () => { await client.close(); });

describe("rating V2 round pace (read-only)", () => {
  it("reads each round's length, each side's first valid Clear and the rounds saved with a last-attempt Clear", async () => {
    const match = id(1), a = id(2), b = id(3), other = id(9);
    const start = "2026-11-19T00:00:00Z", end = "2026-11-19T00:05:00Z";
    await database.query(`INSERT INTO ranked_rounds VALUES
      ($1,$4,1,$5,$6,'B','SCORE',NULL), ($2,$4,2,$5,$6,'DRAW','LAST_ATTEMPT_CLEAR','A'), ($3,$4,3,$5,NULL,'A','SCORE',NULL),
      ($7,$4,4,$5,$6,NULL,NULL,NULL), ($8,$9,1,$5,$6,'A','SCORE',NULL)`,
      [id(11), id(12), id(13), match, start, end, id(14), id(15), other]);
    // Round 1: A clears at 2:00 (its earlier 1:00 Clear was invalidated), B at 0:45 and again later; a non-Clear does not count.
    // Round 2: only B clears (4:10). Round 3: A at 1:30, no normal end time stored. Round 4 is unsettled.
    const attempts: [number, number, string, string, boolean, boolean][] = [
      [21, 11, a, "2026-11-19T00:01:00Z", true, false], [22, 11, a, "2026-11-19T00:02:00Z", true, true],
      [23, 11, b, "2026-11-19T00:00:45Z", true, true], [24, 11, b, "2026-11-19T00:03:00Z", true, true],
      [25, 11, a, "2026-11-19T00:00:10Z", false, true], [26, 12, b, "2026-11-19T00:04:10Z", true, true],
      [27, 13, a, "2026-11-19T00:01:30Z", true, true], [28, 14, a, "2026-11-19T00:00:05Z", true, true],
    ];
    for (const [n, round, player, at, cleared, valid] of attempts)
      await database.query("INSERT INTO ranked_attempts VALUES ($1,$2,$3,$4,$5,$6)", [id(n), id(round), player, at, cleared, valid]);
    const pace = await readRoundPace(database, match, a, b, 300);
    expect(pace.rounds).toEqual([
      { roundSeconds: 300, firstClearSecondsA: 120, firstClearSecondsB: 45 },
      { roundSeconds: 300, firstClearSecondsA: null, firstClearSecondsB: 250 },
      { roundSeconds: 300, firstClearSecondsA: 90, firstClearSecondsB: null },
    ]);
    expect(pace.firstRoundWinner).toBe("B");
    expect(pace.savedRoundSides).toEqual(["A"]);
    expect(await readRoundPace(database, id(99), a, b, 300)).toEqual({ rounds: [], firstRoundWinner: null, savedRoundSides: [] });
  });
});
