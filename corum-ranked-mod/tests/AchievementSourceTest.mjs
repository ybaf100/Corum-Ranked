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
    assert.match(body(popup, 'CCLabelBMFont* makeLabel(', 'CCNode* makePanel('), /corum::ranked::ui::menuLabel/);
    for (const hardcoded of ['승리자', '경쟁자', '클리어러', '완벽주의자', '한 끗 차이']) {
        assert.doesNotMatch(popup, new RegExp(hardcoded));
    }
});

test('Trophy badge and Claim state remain independent and bounded', () => {
    const main = body(popup, 'void renderModePanel(', 'void animateModeLobby(');
    assert.match(main, /onHistory/);
    assert.match(main, /onAchievements/);
    assert.match(main, /achievementBadgeText/);
    const achievements = body(popup, 'void renderAchievements(', 'void renderHistoryList(');
    assert.match(achievements, /achievementClaimState/);
    assert.match(achievements, /claimableRewards/);
    assert.match(presentation, /if \(count > 99\) return "99\+"/);
    assert.match(achievements, /titleButton->setEnabled\(titleUnlocked && !view\.achievements\.loading\)/);
    assert.match(achievements, /if \(!titleUnlocked\) corum::ranked::ui::grayButtonVisual\(titleButton\)/);
    const selector = body(popup, 'void renderAchievementTitleSelector(', 'void renderAchievements(');
    assert.match(selector, /achievementTitleLevelSelectable\(\s*level,\s*card\.currentLevel\s*\)/);
    assert.match(selector, /if \(!available\) corum::ranked::ui::grayButtonVisual\(button\)/);
});

// These values intentionally guard the effective native-device readability floor:
// action captions are scaled again by their button roots, so reducing either layer
// can make Korean/English labels unreadably small even when the layout still fits.
test('Lobby and Achievement text keep native-readable sizing', () => {
    const actionVisual = body(popup, 'CCNode* makeActionButtonVisual(', 'std::string resourceKey(');
    const language = body(popup, 'void addLanguageSelector()', 'void addStatusPill(');
    const main = body(popup, 'void renderModePanel(', 'void animateModeLobby(');
    const selector = body(popup, 'void renderAchievementTitleSelector(', 'void renderAchievements(');
    const achievements = body(popup, 'void renderAchievements(', 'void renderHistoryList(');

    assert.match(actionVisual, /text,\s*0\.62f/);
    assert.match(actionVisual, /fitMenuLabelWidth\(caption, width - 20\.0f, 0\.62f, 0\.30f\)/);
    assert.equal((language.match(/setScale\(0\.48f\)/g) ?? []).length, 2);
    assert.match(main, /historyVisual->setScale\(0\.66f\)/);
    assert.match(main, /trophyVisual->setScale\(0\.66f\)/);

    assert.match(selector, /static_cast<float>\(level - 1\) \* 72\.0f/);
    assert.match(selector, /onAchievementTitleLevel[\s\S]{0,120}0\.52f/);
    assert.match(selector, /onAchievementTitleClear[\s\S]{0,120}0\.56f/);
    assert.match(selector, /onAchievementTitleSelectorClose[\s\S]{0,120}0\.56f/);

    assert.match(achievements, /constexpr float cardHeight = 88\.0f/);
    assert.match(achievements, /title,\s*0\.40f/);
    assert.match(achievements, /shorten\(description[\s\S]{0,120}0\.25f/);
    assert.match(achievements, /progressText,\s*0\.30f/);
    assert.match(achievements, /onAchievementClaim[\s\S]{0,120}0\.50f/);
    assert.match(achievements, /onAchievementTitle\)[\s\S]{0,120}0\.50f/);
    assert.match(achievements, /onAchievementPrevious[\s\S]{0,120}0\.52f/);
    assert.match(achievements, /onAchievementNext[\s\S]{0,120}0\.52f/);
});

test('Achievement selector is a true modal and the lobby badge uses stable Corum chrome', () => {
    const main = body(popup, 'void renderModePanel(', 'void animateModeLobby(');
    const achievements = body(popup, 'void renderAchievements(', 'void renderHistoryList(');

    assert.doesNotMatch(main, /GJ_button_06\.png/);
    assert.match(main, /corum::ranked::ui::menuPill/);
    assert.match(main, /ranked-achievement-notification-badge/);

    // m_menu is a sibling above m_root, so the selector must omit every base-page
    // menu item instead of relying on the dim/panel child z-order to cover it.
    assert.match(achievements, /auto const titleSelectorOpen = !m_titleSelectorAchievement\.empty\(\)/);
    assert.match(achievements, /if \(!titleSelectorOpen\) addTopBack\(false\)/);
    assert.match(achievements, /if \(!titleSelectorOpen\) \{[\s\S]*onAchievementClaim[\s\S]*onAchievementTitle/);
    assert.match(achievements, /if \(pageCount > 1 && !titleSelectorOpen\)/);
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
