import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const root = new URL('..', import.meta.url).pathname;
const read = path => readFileSync(join(root, path), 'utf8');

test('fixed-map order: picks fill R1, R2, R3, DM; a second pick takes a map out', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-fixed-order-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), `#include "${join(root, 'src/domain/FixedMapOrder.hpp')}"
#include <cassert>
using corum::ranked::FixedMapOrder;
int main() {
    FixedMapOrder order;
    assert(order.empty() && order.rounds().empty() && order.deathmatch().empty());
    assert(order.pick("a") == 0 && order.pick("b") == 1 && order.pick("c") == 2 && order.pick("d") == 3);
    assert(order.pick("e") == -1);                     // full
    assert(order.rounds() == std::vector<std::string>({"a", "b", "c"}) && order.deathmatch() == "d");
    assert(order.pick("b") == -1 && order.slots[1].empty()); // taken out again
    assert(order.rounds() == std::vector<std::string>({"a", "", "c"})); // a random R2 between
    assert(order.pick("e") == 1 && order.slotOf("e") == 1);  // fills the gap first
    order.clear(2); order.clear(1);
    assert(order.rounds() == std::vector<std::string>({"a"})); // trailing randoms are not sent
    order.clear(0);
    assert(order.rounds().empty() && order.deathmatch() == "d");
    assert(std::string(FixedMapOrder::label(3)) == "DM" && order.pick("") == -1);
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});

test('debug client: fixed maps for the bot, lobby challenges and the incoming offer', () => {
    const runtime = read('src/RankedRuntime.cpp');
    // The bot request carries the order; a challenge goes to the debug challenge API.
    assert.match(runtime, /body\["fixedMaps"\]\["rounds"\] = rounds;\s*body\["fixedMaps"\]\["deathmatch"\] = options\.fixedDeathmatch;/);
    assert.match(runtime, /endpoint\("\/api\/ranked\/debug\/challenges\/lobby"\)/);
    assert.match(runtime, /request\.post\(endpoint\("\/api\/ranked\/debug\/challenges"\)\)/);
    assert.match(runtime, /"\/api\/ranked\/debug\/challenges\/\{\}\/\{\}", id, accept \? "accept" : "decline"/);
    // Idle in the lobby: invites and the own accepted challenge are polled (debug builds only).
    assert.match(runtime, /#if defined\(CORUM_RANKED_DEBUG_BOT_MATCH\)\s*if \(m_view\.stage == RuntimeStage::Ready && now >= m_nextDebugChallengePollAt\) pollDebugChallenges\(\);\s*#endif/);
    assert.match(runtime, /out->status == "ACCEPTED" && !out->matchId\.empty\(\) && out->matchId != m_debugAdoptedMatch/);
    const adopt = runtime.slice(runtime.indexOf('void RankedRuntime::adoptDebugChallengeMatch('), runtime.indexOf('bool RankedRuntime::prepareSurvivalDebugAdmission('));
    assert.match(adopt, /m_view\.stage != RuntimeStage::Ready \|\| m_controlBusy\) return;/);
    assert.match(adopt, /setStage\(RuntimeStage::Matched,/);
    // The picker (design B) is opened from the Debug Bot settings.
    const popup = read('src/DebugBotPopup.cpp');
    assert.match(popup, /button\(this, "Fixed Maps"/);
    assert.match(popup, /corum::ranked::showDebugFixedMapPopup\(std::move\(options\)\);/);
    const picker = read('src/DebugFixedMapPopup.cpp');
    assert.match(picker, /self->m_order\.pick\(self->m_maps\[self->m_shown\[static_cast<std::size_t>\(row\)\]\]\.canonicalLevelId\);/);
    assert.match(picker, /runtime\.startDebugBotMatch\(options\);/);
    assert.match(picker, /runtime\.sendDebugChallenge\(m_bot\.password, lobby\[/);
    assert.match(picker, /if \(view\.stage == RuntimeStage::Matched\) \{ onClose\(nullptr\); return; \}/);
    // The receiver's offer: accept or decline, once per challenge.
    const lobby = read('src/RankedPopup.cpp');
    assert.match(lobby, /createQuickPopup\("FIXED MAP CHALLENGE", body, "DECLINE", "ACCEPT"/);
    assert.match(lobby, /if \(challenge\.id == m_offeredChallenge\) return;/);
});
