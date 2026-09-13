import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const read = (file) => readFileSync(fileURLToPath(new URL(file, import.meta.url)), 'utf8');
const popup = read('../src/RankedPopup.cpp');
const runtime = read('../src/RankedRuntime.cpp');
const gameplay = read('../src/GameplayUx.cpp');
const playLayer = read('../src/PlayLayer.cpp');
const presentation = read('../src/domain/AchievementPresentation.hpp');

const body = (source, from, to) => {
    const start = source.indexOf(from);
    const end = source.indexOf(to, start + from.length);
    assert(start >= 0 && end > start, `Missing source boundary: ${from} -> ${to}`);
    return source.slice(start, end);
};

test('Achievement cards are server-configured and all Korean UI uses Corum labels', () => {
    const cards = body(runtime, 'void RankedRuntime::parseAchievementState(', 'void RankedRuntime::observeServerNow(');
    for (const field of ['nameKo', 'nameEn', 'descriptionKo', 'descriptionEn']) {
        assert(cards.includes(`value["${field}"]`));
    }
    assert(cards.includes('item["displayColor"]'));
    assert.match(body(popup, 'CCLabelBMFont* makeLabel(', 'CCNode* makePanel('), /corum::ranked::ui::label/);
    for (const hardcoded of ['승리자', '경쟁자', '클리어러', '완벽주의자', '한 끗 차이']) {
        assert.doesNotMatch(popup, new RegExp(hardcoded));
    }
});

test('Trophy badge and Claim state remain independent and bounded', () => {
    const main = body(popup, 'void renderMain(', 'void renderPlacementMap(');
    assert.match(main, /onHistory/);
    assert.match(main, /onAchievements/);
    assert.match(main, /achievementBadgeText/);
    const achievements = body(popup, 'void renderAchievements(', 'void renderHistoryList(');
    assert.match(achievements, /achievementClaimState/);
    assert.match(achievements, /claimableRewards/);
    assert.match(presentation, /if \(count > 99\) return "99\+"/);
});

test('Result notices are queued only from settled Match and Placement Result views', () => {
    const matchResult = body(popup, 'void renderResult(', 'ccColor3B achievementCardAccent(');
    const placementResult = body(popup, 'void renderPlacementFinal(', 'void renderPlacement(');
    assert.match(matchResult, /enqueueAchievementToasts/);
    assert.match(placementResult, /enqueueAchievementToasts/);
    assert.doesNotMatch(gameplay, /AchievementToast|enqueueAchievementToasts/);
    assert.doesNotMatch(playLayer, /AchievementToast|enqueueAchievementToasts/);
    assert.match(body(popup, 'void playNextAchievementToast()', 'void enqueueAchievementToasts('), /m_rankTransitionLayer/);
});

test('Achievement actions use authenticated server endpoints, never local AP mutation', () => {
    assert.match(runtime, /request\.get\(endpoint\("\/api\/ranked\/achievements"\)\)/);
    assert.match(runtime, /"\/api\/ranked\/achievements\/" \+ achievementId \+ "\/claim"/);
    assert.match(runtime, /"\/api\/ranked\/achievements\/title\/equip"/);
    assert.match(runtime, /"\/api\/ranked\/achievements\/title\/clear"/);
    assert.doesNotMatch(popup, /totalAp\s*\+=|totalAp\+\+/);
});

test('both Death Match icons use committed-best plus valid live progress', () => {
    const icons = body(gameplay, 'void updateDeathmatchProgressIcons(', 'void updateDeathmatchUx(');
    assert.equal((icons.match(/deathmatchIconProgress\(/g) ?? []).length, 2);
    assert.match(icons, /ownCommitted/);
    assert.match(icons, /opponentCommitted/);
    assert.match(icons, /ownServerDisplay/);
    assert.match(icons, /opponentServerDisplay/);
});
