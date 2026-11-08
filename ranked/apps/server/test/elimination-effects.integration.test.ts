import { readFile, readdir } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { EliminationEffectService } from "../src/cosmetics/elimination-effect.service.js";
import { PgliteDatabase } from "./pglite-database.js";

const directory = new URL("../../../migrations/", import.meta.url);
let pglite: PGlite, database: PgliteDatabase, service: EliminationEffectService;
const uuid = (n: number) => `00000000-0000-4000-8000-${String(n).padStart(12, "0")}`;
const [ace, bee] = [uuid(1), uuid(2)];

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const name of (await readdir(directory)).filter((file) => file.endsWith(".sql")).sort())
    await pglite.exec(await readFile(new URL(name, directory), "utf8"));
  await database.query(`INSERT INTO ranked_players(id,gd_account_id,gd_username,is_bot) VALUES ($1,1,'Ace',FALSE),($2,2,'Bee',FALSE)`, [ace, bee]);
  await database.query(`INSERT INTO ranked_profiles(player_id,displayed_tier,total_ap) VALUES ($1,'SILVER',1240),($2,'GOLD',50)`, [ace, bee]);
  await database.query(`INSERT INTO sur_profiles(player_id,lp,displayed_tier) VALUES ($1,1400,'SILVER'),($2,900,'AQUA')`, [ace, bee]);
  await database.query(`INSERT INTO ranked_achievement_metrics(player_id,survival_gold_reached) VALUES ($1,FALSE),($2,TRUE)`, [ace, bee]);
  service = new EliminationEffectService(database);
});
afterAll(async () => { await pglite.close(); });

describe("elimination effects", () => {
  it("derives unlocks from peak tiers and the AP grade", async () => {
    const state = await service.state(ace);
    expect(state.equipped).toBe("DEFAULT");
    expect(state.grade.tier).toBe("BRONZE");
    expect(Object.fromEntries(state.effects.map((e) => [e.id, e.unlocked])))
      .toEqual({ DEFAULT: true, BOLT: true, SLICE: false, SHATTER: true, VORTEX: false });
    // Bee: Survival GOLD reached before (flag) and Solo GOLD now; grade below RED.
    const other = await service.state(bee);
    expect(Object.fromEntries(other.effects.map((e) => [e.id, e.unlocked])))
      .toEqual({ DEFAULT: true, BOLT: true, SLICE: true, SHATTER: false, VORTEX: false });
    expect(other.grade.tier).toBe(null);
  });
  it("equips only unlocked effects and snapshots them for admission", async () => {
    await expect(service.equip(ace, "SLICE")).rejects.toThrow(/locked/);
    await expect(service.equip(ace, "NOPE")).rejects.toThrow(/Unknown/);
    expect((await service.equip(ace, "SHATTER")).equipped).toBe("SHATTER");
    expect((await service.equip(bee, "SLICE")).equipped).toBe("SLICE");
    const equipped = await database.transaction((tx) => EliminationEffectService.equippedWithin(tx, [ace, bee]));
    expect(equipped.get(ace)).toBe("SHATTER");
    expect(equipped.get(bee)).toBe("SLICE");
  });
});
