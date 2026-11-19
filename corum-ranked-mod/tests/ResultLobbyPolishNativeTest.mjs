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

test('desktop: 1 / 2 open Survival / Solo from the main lobby, with the key shown', () => {
    assert.match(popup, /#if defined\(GEODE_IS_DESKTOP\)\s*\/\/ 1 \/ 2 open Survival \/ Solo from the main lobby \(keyDown\)\.\s*setKeyboardEnabled\(true\);/);
    const key = method(popup, 'void keyDown(enumKeyCodes key, double timestamp) override');
    assert.match(key, /KEY_One \|\| key == KEY_NumPad1/);
    assert.match(key, /KEY_Two \|\| key == KEY_NumPad2/);
    assert.match(key, /survival-card-select"_spr : "solo-ranked-card-select"_spr/);
    assert.match(key, /item->isEnabled\(\) && item->isVisible\(\)\) \{ onSelectMode\(item\); return; \}/);
    assert.match(key, /CCLayerColor::keyDown\(key, timestamp\)/);
    // Inert under popups, overlays and away from L1.
    const available = method(popup, 'bool modeKeyAvailable()');
    for (const guard of ['m_page != Page::Live', 'm_modeNoticeOpen', 'm_resultScoreLayer', 'typeinfo_cast<FLAlertLayer*>(child)', '!isKeypadEnabled()'])
        assert(available.includes(guard), guard);
    assert.match(popup, /makeLabel\(survival \? "1" : "2", fontScale\(15\.0f\)/);
    assert.match(popup, /tr\("mode-key-hint", "KEY"\)/);
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
