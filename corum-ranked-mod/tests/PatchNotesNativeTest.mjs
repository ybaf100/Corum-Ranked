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
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', '-isystem', json + '/include', '-isystem', result, join(dir, 'test.cpp'),
            ...['value', 'impl', 'parser', 'dump', 'external/dragonbox'].map(p => json + '/src/' + p + '.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});

test('the update screen shows the patch notes first; Settings opens them', () => {
    const popup = read('src/RankedPopup.cpp');
    const update = popup.slice(popup.indexOf('void renderModUpdate()'), popup.indexOf('void onUpdateChannel(CCObject*)'));
    assert.match(update, /tr\("patch-notes", "PATCH NOTES"\)[\s\S]*"ranked-update-patch-notes"_spr/);
    // Opened once per target release, before the guide; never while it restarts.
    assert.match(update, /!restart && !update\.targetVersion\.empty\(\) && m_patchNotesShownFor != update\.targetVersion/);
    assert(update.indexOf('onPatchNotes(nullptr)') < update.indexOf('addGuideCarousel()'));
    assert.match(popup, /RankedPatchNotesPopup::create\(target\)/);
    const settings = read('src/ui/RankedSettingsPopup.hpp');
    assert.match(settings, /"ranked-settings-patch-notes"_spr/);
    assert.match(settings, /RankedPatchNotesPopup::create\(Mod::get\(\)->getVersion\(\)\.toVString\(\)\)/);
    const notes = read('src/ui/RankedPatchNotesPopup.cpp');
    // Any Korean: the system font, not the bundled atlas; KO / EN start in the UI language.
    assert.match(notes, /CCLabelTTF::create\(\("- "\+line\)\.c_str\(\),noteFont\(\)/);
    assert.match(notes, /return "Malgun Gothic";/);
    assert.match(notes, /m_korean=corum::ranked::i18n::isKorean\(\);/);
    assert.match(read('src/RankedRuntime.cpp'), /patchNotesUrl\(\) const \{ return endpoint\("\/api\/ranked\/patch-notes"\); \}/);
});
