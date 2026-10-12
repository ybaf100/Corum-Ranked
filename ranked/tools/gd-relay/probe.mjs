// Run after `npm run build --prefix ranked`; credentials are read from the
// same environment names as Render. This command never connects to the DB.
import { SeededRandom } from "@corum-ranked/rules";
import { GdSurvivalMapProvider } from "../../apps/server/dist/survival/survival-map.provider.js";
import { createSurvivalGdFetch, loadSurvivalGdRelayEnvironment } from "../../apps/server/dist/survival/survival-gd-transport.js";

try {
  const relay = loadSurvivalGdRelayEnvironment(process.env);
  if (!relay) throw new Error("Configure SURVIVAL_GD_RELAY_ORIGIN and SURVIVAL_GD_RELAY_TOKEN before probing");
  const provider = new GdSurvivalMapProvider(new SeededRandom(Date.now()), createSurvivalGdFetch(relay));
  const map = await provider.select({ group: "RED_BRONZE", phase: "ROUND_1", minimumLevelId: 40_000_000, excludedLevelIds: [] });
  console.log(JSON.stringify({ ok: true, transport: "relay", levelId: map.levelId, stars: map.stars,
    geometryBytes: map.geometryBytes, geometrySha256: map.geometrySha256 }));
} catch (error) {
  // Only the provider's sanitized diagnostics are shown; never dump config.
  console.error(JSON.stringify({ ok: false, error: error.message }));
  process.exitCode = 1;
}
