import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const root = new URL('..', import.meta.url).pathname;
const read = path => readFileSync(join(root, path), 'utf8');
const popup = read('src/RankedPopup.cpp');
const guide = read('src/ui/RankedTierGuidePopup.cpp');
const method = (src, sig) => {
    const at = src.indexOf(sig); assert(at >= 0, sig);
    let depth = 0, i = src.indexOf('{', at);
    for (; i < src.length; i++) { if (src[i] === '{') depth++; else if (src[i] === '}' && --depth === 0) break; }
    return src.slice(at, i + 1);
};
const compile = (name, code) => {
    const dir = mkdtempSync(join(tmpdir(), name));
    try {
        writeFileSync(join(dir, 'test.cpp'), code);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
};

test('mouse wheel no longer hides the Solo result round rows', () => {
    // Geode's ScrollLayer content layer runs this on every scroll (wheel or drag):
    // a child is shown only while its own box overlaps the viewport.
    compile('corum-scroll-cull-', `#include <cassert>
struct Child {float y, height;};
bool shown(float contentY, Child c, float viewport) {
    float childY = contentY + c.y, top = childY + c.height, bottom = childY;
    return top > 0 && bottom < viewport;
}
int main() {
    float const viewport = 26 * 3.5f, contentH = 26 * 4, rowH = 26;
    float const top = viewport - contentH; // scrollToTop
    // Before: a zero-size motion group at the origin for every row -> hidden on the first wheel tick.
    assert(!shown(top, {0, 0}, viewport) && !shown(top + 5, {0, 0}, viewport));
    // After: each row is a row-sized slot at its own place; the visible rows stay.
    int visible = 0;
    for (int i = 0; i < 4; ++i) visible += shown(top + 5, {contentH - rowH * (i + 1), rowH}, viewport);
    assert(visible >= 3);
}
`);
    const result = method(popup, 'void renderResult(RuntimeView const& view)');
    assert.match(result, /slot->setContentSize\(\{tableW, rowH\}\); slot->setPosition\(\{0\.0f, y - rowH \/ 2\.0f\}\)/);
    assert.match(result, /makeMotionGroup\(xShift > 0\.0f \? CCPoint\{0\.0f, -\(y - rowH \/ 2\.0f\)\} : CCPointZero\)/);
});

test('Solo result header draws in, with a single slanted band', () => {
    const result = method(popup, 'void renderResult(RuntimeView const& view)');
    // One polygon instead of 2 px strips (they showed as a staircase).
    assert.match(result, /band->drawPolygon\(shape, 4,/);
    assert.doesNotMatch(result, /strip->setPosition/);
    assert.match(result, /band->setScaleX\(0\.0f\);\s*band->runAction\(CCEaseExponentialOut::create\(CCScaleTo::create/);
    // Verdict text from the left, the series plate from the right; once per match.
    assert.match(result, /headerGroup->setPositionX\(-28\.0f\); revealGroup\(headerGroup/);
    assert.match(result, /plateGroup->setPositionX\(28\.0f\); revealGroup\(plateGroup/);
    assert(result.indexOf('auto const animateRows') < result.indexOf('drawPolygon'));
});

test('the achievements slot is always shown under the round table', () => {
    const result = method(popup, 'void renderResult(RuntimeView const& view)');
    assert.match(result, /tr\("result-no-achievements", "NO NEW ACHIEVEMENTS"\)/);
    assert.match(result, /body->setID\("ranked-result-achievements"_spr\)/);
    // Same place as the sliding unlock notice.
    assert.match(result, /slotX = std::min\(size\.width \/ 2\.0f, 12\.0f \+ 165\.0f\), slotY = 78\.0f/);
    assert.match(method(popup, 'void playNextAchievementToast()'), /std::min\(size\.width \/ 2\.0f, 12\.0f \+ 165\.0f\)/);
    assert.match(method(popup, 'void playNextAchievementToast()'), /\{toastX, 78\.0f\}/);
});

test('ALL RECORDS cards name the players of matches of up to two', () => {
    const row = method(popup, 'CCNode* historyRow(HistoryMatchView const& item,float width)');
    assert.match(row, /historyAllRecords\(\)\)\{\s*auto const a=item\.record\["players"\]\["A"\]\["name"\]/);
    assert.match(row, /"\{\} · \{\} vs \{\}"/);
    // Survival only with one or two entrants (never 3+).
    assert.match(row, /historyAllRecords\(\)&&total>0&&total<=2/);
});

test('desktop keyboard: lobby, queue, ban, result, play again and claim all, each with its key shown', () => {
    assert.match(popup, /#if defined\(GEODE_IS_DESKTOP\)\s*\/\/ 1 \/ 2 open Survival \/ Solo from the main lobby \(keyDown\)\.\s*setKeyboardEnabled\(true\);/);
    const key = method(popup, 'void keyDown(enumKeyCodes key, double timestamp) override');
    assert.match(key, /if \(keyboardShortcut\(key\)\) return;\s*CCLayerColor::keyDown\(key, timestamp\);/);
    const shortcut = method(popup, 'bool keyboardShortcut(enumKeyCodes key)');
    // 1 / 2 on the main lobby; 1-9 / 0 on the ban board (number row and keypad).
    assert.match(shortcut, /KEY_Zero && key <= KEY_Nine \? key - KEY_Zero : key >= KEY_NumPad0 && key <= KEY_NumPad9 \? key - KEY_NumPad0/);
    assert.match(shortcut, /"ranked-ban-card"_spr, digit/);
    assert.match(shortcut, /digit == 0 && pressKeyTarget\(\{"ranked-ban-none"_spr\}\)/);
    assert.match(shortcut, /digit == 1 \? "survival-card-select"_spr : "solo-ranked-card-select"_spr/);
    // Enter / Space: the screen's main button, in this order.
    assert.match(shortcut, /key == KEY_Enter \|\| key == KEY_Space\)\s*return pressKeyTarget\(\{"ranked-result-queue-again"_spr, "ranked-solo-play-again"_spr, "ranked-placement-play-again"_spr,\s*"ranked-start-button"_spr, "survival-start-button"_spr, "ranked-achievement-claim-all"_spr\}\)/);
    assert.match(shortcut, /key == KEY_H\) return pressKeyTarget\(\{"ranked-result-history"_spr\}\)/);
    // Only an enabled button on screen (every ancestor visible) is pressed, as a click.
    const target = method(popup, 'CCMenuItemSpriteExtra* keyTarget(');
    assert.match(target, /!item \|\| !item->isEnabled\(\)/);
    assert.match(target, /for \(CCNode\* node = item; node && shown; node = node->getParent\(\)\) shown = node->isVisible\(\);/);
    assert.match(method(popup, 'bool pressKeyTarget('), /item->activate\(\);/);
    // Inert under popups and overlays.
    const available = method(popup, 'bool keyboardAvailable()');
    for (const guard of ['m_modeNoticeOpen', 'm_modeTransition.moving()', 'm_resultScoreLayer', 'm_rankTransitionLayer', 'typeinfo_cast<FLAlertLayer*>(child)', '!isKeypadEnabled()'])
        assert(available.includes(guard), guard);
    // Esc cancels matching before it navigates back.
    const back = method(popup, 'void keyBackClicked() override');
    assert.match(back, /pressKeyTarget\(\{"ranked-queue-leave"_spr, "survival-queue-leave"_spr\}\)\) return;/);
    assert(back.indexOf('ranked-queue-leave') < back.indexOf('stepBack()'));
    // Every target shows its key (desktop only, enabled buttons only).
    const hint = method(popup, 'static void addKeyHint(CCMenuItemSpriteExtra* item, std::string const& key)');
    assert.match(hint, /#if defined\(GEODE_IS_DESKTOP\)\s*if \(!item \|\| !item->isEnabled\(\)\) return;/);
    for (const [id, cap] of [['ranked-start-button', 'ENTER'], ['survival-start-button', 'ENTER']])
        assert.match(popup, new RegExp(`setID\\("${id}"_spr\\); m_menu->addChild\\(\\w+\\);\\s*addKeyHint\\(\\w+, "${cap}"\\);`), id);
    for (const [id, cap] of [['ranked-ban-none', '0'], ['ranked-placement-play-again', 'ENTER'], ['ranked-solo-play-again', 'ENTER'],
        ['ranked-result-queue-again', 'ENTER'], ['ranked-result-history', 'H']])
        assert.match(popup, new RegExp(`"${id}"_spr\\), "${cap}"\\);`), id);
    assert.equal((popup.match(/addKeyHint\(m_queueLeave, "ESC"\);/g) || []).length, 2);
    assert.match(popup, /button->setID\(fmt::format\("\{\}-\{\}", "ranked-ban-card"_spr, i \+ 1\)\); addKeyHint\(button, std::to_string\(i \+ 1\)\);/);
    assert.match(popup, /setID\("ranked-achievement-claim-all"_spr\)[\s\S]{0,400}addKeyHint\(button,"ENTER"\);/);
    assert.match(popup, /makeLabel\(survival \? "1" : "2", fontScale\(15\.0f\)/);
    assert.match(read('src/ui/RankedVisuals.hpp'), /inline CCNode\* keycap\(std::string const& key\)/);
});

test('tier guide: Survival promotion series and the Solo MASTER / CHAMPION panel', () => {
    compile('corum-series-rule-', `#include "${join(root, 'src/domain/TierGuidePresentation.hpp')}"
#include <cassert>
using namespace corum::ranked;
int main() {
    std::vector<SurvivalSeriesRuleView> rules{{"RED",10,40,3},{"AQUA",5,15,3},{"GOLD",0,0,0}};
    auto red = survivalSeriesRule(rules, "RED");
    assert(red && survivalSeriesRequiredRank(*red, 16) == 10 && survivalSeriesRequiredRank(*red, 64) == 40 && red->chances == 3);
    assert(!survivalSeriesRule(rules, "GOLD") && !survivalSeriesRule(rules, "SILVER"));
}
`);
    const render = method(guide, 'void RankedTierGuidePopup::render()');
    assert.match(render, /survivalSeriesRule\(view\.survival\.promotionRules,column\.base\)/);
    assert.match(render, /i18n::text\("tier-guide-series-note"/);
    assert.match(render, /i18n::text\("tier-guide-survival-series"/);
    assert.doesNotMatch(render, /tier-guide-survival-promotion/); // the old "promote at once" text was wrong
    assert.match(render, /auto const panels=m_survival\?2:3;/);
    assert.match(render, /cutoff\("championCutoffLp"\),cutoff\("masterCutoffLp"\)/);
    const runtime = read('src/RankedRuntime.cpp');
    assert.match(runtime, /sur\.promotionRules\.push_back\(\{band\["tier"\]/);
    assert.match(runtime, /root\["promotionFailurePenaltyLp"\]/);
});

test('a restarted attempt records and restores the collision state its reset kept', () => {
    const native = read('src/replay/SurvivalReplayNative.cpp'), cbf = read('src/replay/SurvivalReplayCbfNative.cpp');
    // Source: no longer refused; the kept targets/log keys are recorded by map object.
    assert.match(native, /replayInitialKeepsCollisions\(initial\)\|\|corum::ranked::replay::cbfInitialLogsKept\(initial\["cbfInitial"\]\)/);
    assert.match(native, /initial\["initialCollisions"\]=kept;/);
    // Playback: older captures without the record still fail closed; new ones are restored after the snapshot.
    assert.match(native, /replayInitialKeepsCollisions\(initial\)&&!initial\["initialCollisions"\]\.isObject\(\)\)problem="INITIAL_COLLISION_IDENTITY_UNAVAILABLE"/);
    const reset = native.slice(native.indexOf('void resetReplayInitial()'), native.indexOf('void resetReplayTrace()'));
    assert(reset.indexOf('applyReplayPlayerState(m_player1') < reset.indexOf('restoreCbfInitialCollisions(this'));
    // Live gameplay is never changed: the capture only reads.
    const capture = cbf.slice(cbf.indexOf('Json captureCbfInitialCollisions('), cbf.indexOf('bool cbfInitialLogsKept('));
    assert.doesNotMatch(capture, /setObject|removeAllObjects|m_lastCollision\w+\s*=/);
    assert.match(capture, /typeinfo_cast<GameObject\*>\(entryObject\)\)!=object\.instance\)return nullptr/);
    // Geode v5 removed CCDICT_FOREACH (a static_assert): iterate with CCDictionaryExt.
    assert.doesNotMatch(cbf, /CCDICT_FOREACH/);
});
