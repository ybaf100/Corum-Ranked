import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const root = new URL('..', import.meta.url).pathname;
const read = path => readFileSync(join(root, path), 'utf8');

test('patch notes parse the server list, keep both languages and bound their size', () => {
    const json = process.env.CORUM_MATJSON_SOURCE, result = process.env.CORUM_RESULT_INCLUDE;
    if (!json || !result) throw Error('Pinned JSON dependencies required');
    const dir = mkdtempSync(join(tmpdir(), 'corum-patch-notes-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), String.raw`#include "${join(root, 'src/domain/PatchNotes.hpp')}"
#include <cassert>
using namespace corum::ranked;
int main() {
    auto value = matjson::parse(R"({"notes":[
        {"version":"0.6.0-beta.88","date":"2026-11-20","en":["Patch notes"],"ko":["패치 노트"]},
        {"version":"","en":["no version"]},
        {"version":"0.6.0-beta.87","en":["English only"]},
        {"version":"0.6.0-beta.86","en":[],"ko":[]}]})").unwrap();
    auto notes = patchNotesFromJson(value);
    assert(notes.size() == 2 && notes[0].version == "0.6.0-beta.88" && notes[0].date == "2026-11-20");
    assert(notes[0].lines(true)[0] == "패치 노트" && notes[0].lines(false)[0] == "Patch notes");
    assert(notes[1].lines(true)[0] == "English only"); // no Korean: English
    std::string longLine(1000, 'x');
    auto big = matjson::Value::object(); auto list = matjson::Value::array();
    for (int i = 0; i < 50; ++i) { auto note = matjson::Value::object(); note["version"] = "v" + std::to_string(i);
        auto lines = matjson::Value::array(); for (int j = 0; j < 60; ++j) lines.push(longLine); note["en"] = lines; list.push(note); }
    big["notes"] = list; notes = patchNotesFromJson(big);
    assert(notes.size() == 30 && notes[0].en.size() == 40 && notes[0].en[0].size() == 400);
    assert(patchNotesFromJson(matjson::Value()).empty());
    assert(samePatchVersion("v0.6.0-beta.88", "0.6.0-beta.88") && !samePatchVersion("v0.6.0-beta.88", "0.6.0-beta.87") && !samePatchVersion("", ""));
    // The update card: releases after the installed one up to the new one, newest five.
    assert(patchVersionOrder("v0.6.0-beta.9") < patchVersionOrder("0.6.0-beta.10") && patchVersionOrder("0.6.0-beta.99") < patchVersionOrder("0.6.0"));
    assert(patchVersionOrder("0.7.0-beta.1") > patchVersionOrder("0.6.0") && patchVersionOrder("junk")[0] == -1);
    std::vector<PatchNoteView> all;
    for (int n : {95, 94, 93, 92, 91, 90, 89, 88, 87}) all.push_back({"0.6.0-beta." + std::to_string(n), "", {"en"}, {"ko"}});
    auto between = patchNotesBetween(all, "v0.6.0-beta.87", "v0.6.0-beta.90");
    assert(between.size() == 3 && between[0].version == "0.6.0-beta.90" && between[2].version == "0.6.0-beta.88");
    between = patchNotesBetween(all, "v0.6.0-beta.87", "v0.6.0-beta.95");
    assert(between.size() == 5 && between[0].version == "0.6.0-beta.95" && between[4].version == "0.6.0-beta.91"); // newest five
    assert(patchNotesBetween(all, "", "0.6.0-beta.89").size() == 3); // unknown installed version
    assert(patchNotesBetween(all, "0.6.0-beta.95", "0.6.0-beta.95").empty());
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', '-isystem', json + '/include', '-isystem', result, join(dir, 'test.cpp'),
            ...['value', 'impl', 'parser', 'dump', 'external/dragonbox'].map(p => json + '/src/' + p + '.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});

test('the update screen guide has 8 cards; the first is installed > new with the changes between', () => {
    const popup = read('src/RankedPopup.cpp');
    const update = popup.slice(popup.indexOf('void renderModUpdate()'), popup.indexOf('void onUpdateChannel(CCObject*)'));
    assert.match(update, /addGuideCarousel\(corum::ranked::ui::GuidePatchCard\{update\.installedVersion, update\.targetVersion\}\);/);
    assert.doesNotMatch(update, /RankedPatchNotesPopup/); // no popup on the update screen
    // The resources screen keeps the 7 guide cards.
    const resources = popup.slice(popup.indexOf('void renderResources()'), popup.indexOf('void renderPlacementPoolSelection('));
    assert.match(resources, /addGuideCarousel\(\);/);
    const carousel = read('src/ui/RankedGuideCarousel.hpp');
    assert.match(carousel, /m_count = kGuideSlideCount \+ \(m_patch \? 1 : 0\);/);
    assert.match(carousel, /kGuidePatchReleases = 5;/);
    assert.match(carousel, /patchNotesBetween\(cache\.notes\(\), m_patch->from, m_patch->to, kGuidePatchReleases\)/);
    assert.match(carousel, /fmt::format\("\{\}  >  \{\}", version\(patch\.from\), version\(patch\.to\)\)/);
    assert.match(carousel, /CCLabelTTF::create\(\("\* " \+ line\)\.c_str\(\), patchNotesFont\(\)/);
    // A new update starts on that card, which stays 12 s.
    assert.match(carousel, /if \(m_patch && state\.patchTarget != m_patch->to\) \{ state\.patchTarget = m_patch->to; state\.index = 0; state\.elapsed = 0\.0f; \}/);
    assert.match(carousel, /kGuidePatchSlideSeconds = 12\.0f;/);
    // Settings still opens the full notes.
    const settings = read('src/ui/RankedSettingsPopup.hpp');
    assert.match(settings, /"ranked-settings-patch-notes"_spr/);
    assert.match(settings, /RankedPatchNotesPopup::create\(Mod::get\(\)->getVersion\(\)\.toVString\(\)\)/);
    assert.match(read('src/ui/RankedPatchNotesCache.hpp'), /return "Malgun Gothic";/);
    assert.match(read('src/RankedRuntime.cpp'), /patchNotesUrl\(\) const \{ return endpoint\("\/api\/ranked\/patch-notes"\); \}/);
});
