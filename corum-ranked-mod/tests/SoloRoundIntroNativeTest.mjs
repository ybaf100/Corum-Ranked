import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (path) => readFileSync(join(root, path), 'utf8');

test('solo round intro: timeline, revolving reveal lands on the map face, death-match shift adds the slam', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-solo-intro-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), `#include "domain/SoloRoundIntroPolicy.hpp"
#include <cassert>
#include <cmath>
using namespace corum::ranked;
int main(){
    auto normal=soloIntroTimeline(false);
    assert(normal.shift<0 && normal.slam<0);
    assert(std::abs(normal.spin-1.6f)<1e-4 && std::abs(normal.revealed-3.4f)<1e-4);
    assert(std::abs(normal.exit-normal.revealed-1.0f)<1e-4); // 1s hold after the reveal
    assert(normal.board>normal.exit && normal.done>normal.board && normal.done<8.0f); // inside the 10s prepare window
    auto dm=soloIntroTimeline(true);
    assert(dm.shift>0 && dm.slam>dm.shift && dm.spin>dm.slam && dm.board<8.0f);
    auto before=soloRevolveFrame(-1);assert(before.front && before.scaleX==1);
    auto end=soloRevolveFrame(kSoloIntroSpinSeconds+0.5f);
    assert(!end.front && std::abs(end.angleDegrees-900)<1e-2 && std::abs(end.scaleX-1)<1e-3 && std::abs(end.lift-1)<1e-3);
    // Mid-spin the card passes edge-on (both faces alternate).
    bool sawBack=false,sawFront=false;for(int i=1;i<60;++i){auto f=soloRevolveFrame(kSoloIntroSpinSeconds*i/60.f);sawBack|=!f.front;sawFront|=f.front;assert(f.scaleX>=0 && f.scaleX<=1);}
    assert(sawBack && sawFront);
}
`);
        execFileSync('g++', ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', join(root, 'src'), join(dir, 'test.cpp'), '-o', join(dir, 'test')], { stdio: 'pipe' });
        execFileSync(join(dir, 'test'), [], { stdio: 'pipe' });
    } catch (e) { throw new Error(e.stderr?.toString() || e.message); }
    finally { rmSync(dir, { recursive: true, force: true }); }
});

test('solo board: intro plays once per round from PREPARE, no replay controls, spectate is opt-in', () => {
    const popup = read('src/RankedPopup.cpp');
    const prepare = popup.slice(popup.indexOf('    void renderPrepare('), popup.indexOf('    void startSoloIntro('));
    assert.match(prepare, /m_soloIntroPlayed != introKey && phaseSeconds\(\) < 1\.5/);
    const intro = read('src/ui/SoloRoundIntro.hpp');
    assert.doesNotMatch(intro + popup, /REPLAY|다시 보기/);
    assert.match(popup, /if \(spectateOnly && m_spectateRequestKey != spectateKey\) return;/);
    assert.match(popup, /if \(match\.spectatorLiveEnabled\) \{/);
    assert.match(popup, /if \(soloHoldActive\(\)\) \{ m_pendingUiKey = key; return false; \}/);
});
