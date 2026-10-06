import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const inventory = JSON.parse(readFileSync(join(root, 'diagnostics/hook-inventory-beta30.json'), 'utf8'));
function preprocess(source, flags = []) {
    return execFileSync('g++', ['-E', '-P', '-x', 'c++', '-', '-DGEODE_IS_IOS=1', ...flags], {
        input: source.replace(/^\s*#include[^\n]*/gm, ''), encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'],
    });
}
function removeModifyRegistration(source) {
    for (;;) {
        const match = /static void onModify\(auto& self\)\s*\{/.exec(source);
        if (!match) return source;
        let depth = 1, end = match.index + match[0].length;
        while (depth) { const c = source[end++]; if (c === '{') ++depth; if (c === '}') --depth; }
        source = source.slice(0, match.index) + source.slice(end);
    }
}

test('diagnostic exclusions are absent by default; beta30 runtime bodies remain equivalent in Debug ON/OFF', () => {
    for (const file of inventory.runtimeSourceBaselines) {
        const source = readFileSync(join(root, file.path), 'utf8');
        for (const debug of [false, true]) {
            const flags = debug ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1', '-DCORUM_RANKED_DEBUG_BOT_MATCH=1'] : [];
            const body = removeModifyRegistration(preprocess(source, flags)).replace(/\s+/g, '');
            const hash = createHash('sha256').update(body).digest('hex');
            assert.equal(hash, file[debug ? 'debugOnRuntimeSHA256' : 'debugOffRuntimeSHA256'], file.path);
        }
    }
});

test('iOS global hook inventory budgets and compile-time group removal are explicit', () => {
    const installed = inventory.hooks.filter(h => h.iosInstalled);
    assert.equal(installed.length, 56);
    assert.equal(installed.filter(h => h.debugOnly).length, 10);
    assert.equal(installed.filter(h => h.targetClass === 'PlayerObject').length, 7);
    assert.equal(installed.filter(h => h.group === 'C').length, 3);
    assert.equal(installed.filter(h => h.group === 'D').length, 2);
    assert(installed.every(h => !h.inline && h.iosBinding.startsWith('0x')));
    assert(!inventory.hooks.some(h => h.function === 'saveNewScore'));
    const player = readFileSync(join(root, 'src/PlayLayer.cpp'), 'utf8');
    const probes = readFileSync(join(root, 'src/spectator/SpectatorNativeInstrumentation.cpp'), 'utf8');
    const debug = ['-DCORUM_RANKED_DEBUG_SPECTATE=1'];
    assert.match(preprocess(player, debug), /class \$modify\(CorumRankedSpectatorDummyPlayer,/);
    for (const [flag, name] of [['PLAYER', 'DummyPlayer'], ['WORLD', 'CameraHook'], ['PERSISTENCE', 'ProgressSave']]) {
        assert(!preprocess(player, [...debug, `-DCORUM_CRASH_DIAG_NO_${flag}_HOOKS=1`]).includes(`class $modify(CorumRankedSpectator${name},`));
    }
    assert(!preprocess(probes).includes('class $modify('));
    assert(!preprocess(probes, [...debug, '-DCORUM_CRASH_DIAG_NO_PROBES_HOOKS=1']).includes('class $modify('));
    assert.match(preprocess(probes, debug), /class \$modify\(CorumSpectatorNativeInteractionProbe,/);
    assert(!preprocess(probes, debug).includes('void playerTouchedObject(')); // iOS inline binding
});

test('actual diagnostic helper clears gameplay detours before claim and bounds normal-Practice logs', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-crash-isolation-'));
    try {
        mkdirSync(join(dir, 'Geode'));
        writeFileSync(join(dir, 'Geode/Geode.hpp'), `#pragma once
namespace geode::log {
inline int messages=0;
template<class... T>void info(T...){++messages;}
template<class... T>void warn(T...){++messages;}
}
`);
        const cpp = `
#include "${join(root, 'src/diagnostic/CrashHookDiagnostics.hpp')}"
#include <cassert>
#include <map>
#include <string>
struct Modify { std::map<std::string,int> m_hooks{{"PlayLayer::postUpdate",1},{"PlayLayer::levelComplete",2}}; };
int main(){
using namespace corum::ranked::diagnostic;
Modify self;const bool excluded=excludeGameplayGroup(self,"test");
#if defined(CORUM_CRASH_DIAG_NO_GAMEPLAY_HOOKS)
assert(excluded&&self.m_hooks.empty()); // no hooks remain for Geode's next claimHook loop
#else
assert(!excluded&&self.m_hooks.size()==2);
#endif
std::atomic<unsigned> seen{0};int before=geode::log::messages;
for(int i=0;i<100;++i)traceEntry(seen,"PlayerObject::update",true,false,true,true);
for(int i=0;i<100;++i)traceEntry(seen,"PlayerObject::update",true,true,false,true);
for(int i=0;i<100;++i)traceEntry(seen,"PlayerObject::update",true,false,false,false);
assert(seen==0&&geode::log::messages==before);
for(int i=0;i<10000;++i)traceEntry(seen,"PlayerObject::update",true,false,false,true);
assert(seen==3&&geode::log::messages==before+3);
priorityFailure("test");assert(priorityFailures==1);
recordModifyClass(self,"test");assert(modifyClasses==(excluded?0:1));
}
`;
        const file = join(dir, 'test.cpp'), binary = join(dir, 'test'); writeFileSync(file, cpp);
        for (const flags of [[], ['-DCORUM_CRASH_DIAG_NO_GAMEPLAY_HOOKS=1']]) {
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', '-DCORUM_CRASH_DIAG=1', ...flags, '-I', dir, file, '-o', binary], { stdio: 'pipe' });
            execFileSync(binary, [], { stdio: 'pipe' });
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
