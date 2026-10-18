import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const moduleRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const serverOrigin = "https://corum-ranked-production.up.railway.app";
const obsoleteOrigin = "https://corum-ranked.onrender.com";
const targets = [
  ["src/RankedRuntime.cpp", ['kDefaultRankedServerURL = "' + serverOrigin + '"']],
  ["src/SurvivalRuntime.cpp", [
    serverOrigin + "/api/ranked/survival/online",
    serverOrigin + "/api/ranked/survival/queue",
    serverOrigin + "/api/ranked/survival/debug",
  ]],
  ["src/SpectatorLiveRuntime.cpp", ['kDefaultRankedServerURL = "' + serverOrigin + '"']],
  ["src/debug/DebugAdminPopup.cpp", [serverOrigin + "/api/ranked/debug/admin"]],
  ["src/debug/DebugSpectateRuntime.cpp", ['kServer = "' + serverOrigin + '"']],
];

for (const [file, expectedEndpoints] of targets) {
  test(`production endpoints in ${file} use Railway`, () => {
    const source = readFileSync(resolve(moduleRoot, file), "utf8");
    assert.equal(source.includes(obsoleteOrigin), false, "Render origin must not remain in active client source");
    for (const endpoint of expectedEndpoints) {
      assert.ok(source.includes(endpoint), `missing Railway endpoint: ${endpoint}`);
    }
  });
}
