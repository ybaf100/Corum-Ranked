import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));

test('free-play download card shows the mod update or the combined resource percent', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-download-overlay-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), `
#include "${join(root, 'src/domain/DownloadOverlayPolicy.hpp')}"
#include <cassert>
#include <limits>
using namespace corum::ranked;
int main() {
    assert(!downloadOverlayState({}).visible);
    auto update = downloadOverlayState({.modUpdating = true, .modProgress = .426f, .assetsDownloading = true});
    assert(update.visible && update.modUpdate && update.percent == 42);
    // Two of four songs plus half of the third, assets at 40%: (62.5% + 40%) / 2.
    auto both = downloadOverlayState({.assetsDownloading = true, .assetProgress = .4f, .audioDownloading = true,
        .audioReady = 2, .audioTotal = 4, .audioActivePercent = 50});
    assert(both.visible && !both.modUpdate && both.percent == 51);
    // Resources never claim 100% before the managers report ready.
    assert(downloadOverlayState({.audioDownloading = true, .audioReady = 4, .audioTotal = 4}).percent == 99);
    assert(downloadOverlayState({.assetsDownloading = true, .assetProgress = std::numeric_limits<float>::quiet_NaN()}).percent == 0);
    assert(!downloadOverlayState({.audioDownloading = true, .audioTotal = 0}).visible);
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});

test('the card is pumped on every scene outside Ranked and keeps the resource queue moving', () => {
    const source = readFileSync(join(root, 'src/ui/SurvivalQueueLayer.cpp'), 'utf8');
    const pump = source.slice(source.indexOf('static void pumpRankedDownloadOverlay('), source.indexOf('void pumpSurvivalQueueOverlay()'));
    assert.match(pump, /if\(!lobby\)RankedAudioManager::get\(\)\.tickDownloads\(\);/);
    assert.match(pump, /state\.visible && !lobby && !CCDirector::sharedDirector\(\)->getNextScene\(\)/);
    assert.match(source, /struct DownloadPump\{CCScene\* scene;bool queue=false;~DownloadPump\(\)\{pumpRankedDownloadOverlay\(scene,queue\);\}\} download\{scene\};/);
});
