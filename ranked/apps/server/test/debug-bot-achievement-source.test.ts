import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
const source=readFileSync(fileURLToPath(new URL("../src/match/match.service.ts",import.meta.url)),"utf8");
describe("Debug Bot Achievement result eligibility",()=>{
 it("awards v2 achievements only on the normal PvP result path",()=>{
  const start=source.indexOf('if (current.match_type === "RANKED_PVP") {');
  expect(start).toBeGreaterThan(0);
  const result=source.indexOf('this.achievements.recordMatchResult(',start);
  expect(result).toBeGreaterThan(start);
  expect(source.slice(start,result)).not.toContain('DEBUG_BOT');
 });
});
