import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync, readdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const selection = JSON.parse(readFileSync(join(root, 'diagnostics/player-hook-stage2.json'), 'utf8'));
const inventory = JSON.parse(readFileSync(join(root, 'diagnostics/hook-inventory-beta30.json'), 'utf8'));
const player = readFileSync(join(root, 'src/PlayLayer.cpp'), 'utf8');
function preprocess(source, flags) {
    return execFileSync('g++', ['-E', '-P', '-x', 'c++', '-', '-DGEODE_IS_IOS=1', ...flags], {
        input: source.replace(/^\s*#include[^\n]*/gm, ''), encoding: 'utf8', stdio: 'pipe',
    });
}
function block(source, start) {
    const begin = source.indexOf(start);
    if (begin < 0) return '';
    const brace = source.indexOf('{', begin);
    let depth = 1, end = brace + 1;
    while (depth) { const c = source[end++]; if (c === '{') ++depth; if (c === '}') --depth; }
    return source.slice(brace + 1, end - 1);
}
function playerDeclarations(flags) {
    const body = block(preprocess(player, flags), 'class $modify(CorumRankedSpectatorDummyPlayer,');
    return [...body.matchAll(/\bvoid (\w+)\(/g)].map(m => m[1]).filter(name => name !== 'onModify').sort();
}
const diagnosticFlags = ['-DCORUM_CRASH_DIAG=1', '-DCORUM_RANKED_DEBUG_SPECTATE=1'];

test('Stage 2 is an exact legacy 3 / beta29 4 split; eight comparisons keep probes excluded', () => {
    const old = ['update', 'updateRotation', 'spiderTestJump'];
    const added = ['setPosition', 'setRotation', 'setVisible', 'updateSpecial'];
    assert.deepEqual(selection.playerHooks.filter(h => h.cohort === 'legacy').map(h => h.function), old);
    assert.deepEqual(selection.playerHooks.filter(h => h.cohort === 'beta29').map(h => h.function), added);
    for (const h of selection.playerHooks) {
        const original = inventory.hooks.find(x => x.group === 'B' && x.function === h.function);
        assert.equal(h.iosBinding, original.iosBinding);
        assert.equal(h.cohort === 'legacy', original.presentBeforeBeta29);
    }
    const variants = selection.variants.filter(v => v.variant.startsWith('stage2-'));
    assert.equal(variants.length, 8);
    for (const v of variants) {
        assert.equal(v.diagnostic, 'ON'); assert.equal(v.debug, 'ON');
        assert(v.exclusions.includes('CORUM_CRASH_DIAG_NO_PROBES_HOOKS'));
        assert(v.exclusions.every(f => f === 'CORUM_CRASH_DIAG_NO_PROBES_HOOKS' || f.startsWith('CORUM_CRASH_DIAG_NO_PLAYER_')));
        const flags = [...diagnosticFlags, ...v.exclusions.map(f => `-D${f}=1`)];
        assert.deepEqual(playerDeclarations(flags), [...v.enabledPlayerHooks].sort(), v.variant);
        const body = block(preprocess(player, flags), 'class $modify(CorumRankedSpectatorDummyPlayer,');
        const priority = block(body, 'static void onModify(');
        for (const h of selection.playerHooks)
            assert.equal(priority.includes(`"PlayerObject::${h.function}"`), v.enabledPlayerHooks.includes(h.function), `${v.variant} priority ${h.function}`);
        const probe = preprocess(readFileSync(join(root, 'src/spectator/SpectatorNativeInstrumentation.cpp'), 'utf8'), flags);
        assert(!probe.includes('class $modify('));
        assert.match(preprocess(player, flags), /class \$modify\(CorumRankedSpectatorCameraHook,/);
        assert.match(preprocess(player, flags), /class \$modify\(CorumRankedSpectatorProgressSave,/);
    }
    // Individual method switches also work as full-B complement removals.
    for (const h of selection.playerHooks) {
        assert.deepEqual(playerDeclarations([...diagnosticFlags, `-D${h.exclusionOption}=1`]),
            selection.playerHooks.filter(x => x !== h).map(x => x.function).sort());
    }
});

test('Stage 2 introduces no native detour classes and keeps production defaults, versions and wire formats', () => {
    const allSources = [];
    function walk(dir) {
        for (const entry of readdirSync(dir, { withFileTypes: true })) {
            const path = join(dir, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (/\.(cpp|hpp)$/.test(path)) allSources.push(readFileSync(path, 'utf8'));
        }
    }
    walk(join(root, 'src'));
    const classes = allSources.flatMap(s => [...s.matchAll(/class \$modify\((\w+),/g)].map(m => m[1])).sort();
    assert.deepEqual(classes, [...new Set(inventory.hooks.map(h => h.modifyClass))].sort());
    const diagnostics = readFileSync(join(root, 'src/diagnostic/CrashHookDiagnostics.cpp'), 'utf8');
    assert(!/\$modify|Hook::create|createHandler|commonHandlerSpace/.test(diagnostics));
    assert.equal(preprocess(diagnostics, []).trim(), '');
    const cmake = readFileSync(join(root, 'CMakeLists.txt'), 'utf8');
    assert.match(cmake, /CORUM_CRASH_DIAG_NO_PLAYER_\$\{method\}_HOOK[^\n]*OFF\)/);
    assert.match(cmake, /CORUM_CRASH_DIAG_NO_PLAYER_\$\{method\}_HOOK AND NOT CORUM_CRASH_DIAG/);
    for (const mode of ['on', 'off']) {
        const v = selection.variants.find(x => x.variant === `compile-check-beta30-debug-${mode}`);
        assert.equal(v.diagnostic, 'OFF'); assert.deepEqual(v.exclusions, []);
        assert.deepEqual(playerDeclarations(mode === 'on' ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []),
            selection.playerHooks.map(h => h.function).sort());
    }
    assert.equal(selection.version, 'v0.5.0-beta.30');
    assert.equal(JSON.parse(readFileSync(join(root, 'mod.json'), 'utf8')).version, selection.version);
    assert.match(readFileSync(join(root, '../ranked/apps/server/src/session/client-version.ts'), 'utf8'), /v0\.5\.0-beta\.30/);
    assert.equal(selection.protocolVersion, 2); assert.equal(selection.debugRecordingFormatVersion, 2);
    assert.match(readFileSync(join(root, 'src/domain/SpectatorProtocol.hpp'), 'utf8'), /kSpectatorProtocolVersion = 2;/);
    assert.match(readFileSync(join(root, 'src/debug/DebugSpectatePolicy.hpp'), 'utf8'), /kDebugSpectateFormatVersion = 2;/);
});

test('actual Loaded registry callback detects selected, missing, duplicated, disabled and unexpected PlayerObject detours', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-stage2-registry-'));
    try {
        mkdirSync(join(dir, 'Geode/loader'), { recursive: true });
        writeFileSync(join(dir, 'Geode/Geode.hpp'), `#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
namespace geode {
namespace log { inline unsigned warnings=0; template<class... T>void info(T...){ } template<class... T>void warn(T...){++warnings;} }
struct Hook {std::string_view name;bool enabled=true;std::string_view getDisplayName()const{return name;}std::uintptr_t getAddress()const{return 1;}bool isEnabled()const{return enabled;}int getPriority()const{return 0;}};
struct Version {char const* toVString()const{return "v0.5.0-beta.30";}};
struct Mod {std::vector<Hook*> hooks;static Mod* get(){static Mod mod;return &mod;}std::vector<Hook*> getHooks()const{return hooks;}Version getVersion()const{return {};}};
namespace prelude {using geode::Mod;namespace log=geode::log;}
}
#define $on_mod(event) void loadedCallback()
`);
        writeFileSync(join(dir, 'Geode/loader/ModEvent.hpp'), '#pragma once\n#include <Geode/Geode.hpp>\n');
        const stage3 = JSON.parse(readFileSync(join(root, 'diagnostics/player-hook-stage3.json'), 'utf8'));
        for (const v of [...selection.variants, ...stage3.variants].filter(v => v.diagnostic === 'ON')) {
            const file = join(dir, 'test.cpp'), binary = join(dir, 'test');
            writeFileSync(file, `
#include "${join(root, 'src/diagnostic/CrashHookDiagnostics.cpp')}"
#include <cassert>
int main(){
using namespace corum::ranked::diagnostic;
constexpr std::array<bool,7> selected{${selection.playerHooks.map(h => v.enabledPlayerHooks.includes(h.function)).join(',')}};
std::array<geode::Hook,7> hooks{};
auto& registry=geode::Mod::get()->hooks;
for(std::size_t i=0;i<hooks.size();++i){assert(playerHookExpectations[i].selected==selected[i]);hooks[i].name=playerHookExpectations[i].target;if(selected[i])registry.push_back(&hooks[i]);}
loadedCallback();assert(geode::log::warnings==0);
if(!registry.empty()){
auto* first=registry.back();registry.pop_back();loadedCallback();assert(geode::log::warnings==1);registry.push_back(first);
first->enabled=false;loadedCallback();assert(geode::log::warnings==2);first->enabled=true;
registry.push_back(first);loadedCallback();assert(geode::log::warnings==3);registry.pop_back();
}
auto before=geode::log::warnings;
for(std::size_t i=0;i<hooks.size();++i)if(!selected[i]){registry.push_back(&hooks[i]);loadedCallback();assert(geode::log::warnings==before+1);registry.pop_back();++before;}
geode::Hook unknown{"PlayerObject::newAccidentalHook"};registry.push_back(&unknown);loadedCallback();assert(geode::log::warnings==before+1);
}
`);
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', ...diagnosticFlags,
                ...(v.worldOwnedPresentation ? ['-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=1'] : []),
                '-DCORUM_CRASH_DIAG_VARIANT="host-fixture"', ...v.exclusions.map(f => `-D${f}=1`),
                '-I', dir, file, '-o', binary], { stdio: 'pipe' });
            execFileSync(binary, [], { stdio: 'pipe' });
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('CI matrix and artifact verifier validate the entire detour set, not imported names or expectation strings', () => {
    const script = join(root, 'diagnostics/verify-crash-artifact.py');
    const matrix = JSON.parse(execFileSync('python3', [script, '--matrix'], { encoding: 'utf8' }).trim().slice('matrix='.length));
    assert.equal(matrix.include.length, 10);
    for (const row of matrix.include) {
        const v = selection.variants.find(x => x.variant === row.variant);
        assert.equal(row.diagnostic, v.diagnostic); assert.equal(row.debug, v.debug);
        assert.equal(row.exclusions, v.exclusions.map(f => `-D${f}=ON`).join(' '));
    }
    execFileSync('python3', ['-c', `
import importlib.util,struct,sys
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('verification',${JSON.stringify(script)})
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
for v in module.SELECTION['variants']:
 targets=module.expected_targets(v)
 header=struct.pack('<II',0xFEEDFACF,0x0100000C)
 markers=b''.join(('[GEODE_MODIFY_NAME] '+t+' [GEODE_MODIFY_ADDRESS] 0x0 [GEODE_MODIFY_END]\\0').encode() for t in sorted(targets))
 diag=b'[CorumCrashDiag]' if v['diagnostic']=='ON' else b''
 binary=header+markers+diag
 assert set(module.verify_binary(binary,v))==targets
 bad=binary+b'[GEODE_MODIFY_NAME] SpriteAnimationManager::callAnimationFinished [GEODE_MODIFY_ADDRESS] 0x0 '
 try:module.verify_binary(bad,v)
 except AssertionError:pass
 else:raise AssertionError('new native detour was accepted')
 target=sorted(targets)[0]
 bad=binary.replace(('[GEODE_MODIFY_NAME] '+target+' ').encode(),b'[IMPORTED_NAME] ')
 # A plain registry expectation string cannot substitute for a detour marker.
 bad+=target.encode()
 try:module.verify_binary(bad,v)
 except AssertionError:pass
 else:raise AssertionError('missing detour was accepted')
`], { stdio: 'pipe' });
});
