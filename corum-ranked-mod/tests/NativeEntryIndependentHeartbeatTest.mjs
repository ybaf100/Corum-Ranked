import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const read = (path) => readFileSync(resolve(root, path), "utf8");
const play = read("src/PlayLayer.cpp");
const round = read("src/RankedRoundPreparation.cpp");
const header = read("src/RankedRoundPreparation.hpp");
const signature = "void corum::ranked::pumpHeldRankedEntry(PlayLayer* layer) {";
const start = play.indexOf(signature);
assert.ok(start >= 0, "process-owned native entry pump must be implemented");
const end = play.indexOf("\n}\n", start);
assert.ok(end >= 0, "entry pump function closing brace");
const pump = play.slice(start, end + 3);

test("process-owned heartbeat drives held Ranked entry independently of paused scene callbacks", () => {
    assert.match(round, /scheduleUpdateForTarget\(preparation, 1, false\)/);
    assert.match(round, /void RankedRoundPreparation::update\(float dt\) \{[\s\S]*?pumpHeldRankedEntry\(current\.data\(\)\)/);
    assert.match(header, /void pumpHeldRankedEntry\(PlayLayer\* layer\);/);
    assert.match(pump, /managed->blockEntryGameplayUpdate\(\)/);
    assert.match(pump, /std::chrono::milliseconds\(100\)/);
});

test("entry fallback never unpauses unknown scenes or bypasses server authority", () => {
    for (const required of [
        "layer != PlayLayer::get()", "managed->m_fields->rankedInitialized",
        "managed->m_fields->rankedLevel", "managed->m_fields->nativeEntryHeld",
        "managed->m_fields->entryNativeCompletionInProgress",
        "managed->m_fields->autoExitRequested", "managed->m_fields->safetyRetired",
        "layer->isRunning()", "director->getRunningScene()", "director->getNextScene()",
    ]) assert.ok(pump.includes(required), `missing exact ownership gate: ${required}`);
    assert.doesNotMatch(pump, /pauseGame\(|resume\(|m_gamePaused\s*=|m_isPaused\s*=/);
    assert.doesNotMatch(pump, /armCurrentLevelForGameplay|nativeEntryStartGranted|reportAttemptStart/);
});
