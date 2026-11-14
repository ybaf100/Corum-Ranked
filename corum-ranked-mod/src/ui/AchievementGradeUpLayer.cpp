#include "AchievementGradeUpLayer.hpp"

#include "RankBadgeAssets.hpp"
#include "RankedInput.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "../domain/RankTierPresentation.hpp"
#include "../domain/SurvivalEliminationEffects.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

constexpr float kPi = 3.14159265f;

float unit(int index, int salt) {
    auto const value = std::sin(static_cast<float>(index * 12.9898f + salt * 78.233f)) * 43758.5453f;
    return value - std::floor(value);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

ccColor3B gradeColor(std::string const& grade) {
    return grade.empty() ? ccColor3B{142, 134, 168} : rankTierProfile(grade).color;
}

CCSprite* fx(CCNode* parent, char const* file, CCPoint at, float extent, int z, ccColor3B color, GLubyte opacity) {
    auto* sprite = rankResourceSprite(file);
    if (!sprite) return nullptr;
    fitRankSprite(sprite, {extent, extent});
    sprite->setPosition(at);
    sprite->setColor(color);
    sprite->setOpacity(opacity);
    sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    parent->addChild(sprite, z);
    return sprite;
}

CCNode* buttonFace(std::string const& text, ccColor3B accent, bool subtle) {
    CCSize const size = subtle ? CCSize{150.0f, 24.0f} : CCSize{142.0f, 34.0f};
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});
    if (!subtle) root->addChild(surface(size, {size.width / 2.0f, size.height / 2.0f}, accent, SurfaceRaised, 244, true), 0);
    auto* caption = menuLabel(text, subtle ? 0.25f : 0.34f, {size.width / 2.0f, size.height / 2.0f},
        subtle ? TextMuted : Text, subtle ? FontWeight::SemiBold : FontWeight::Black);
    fitMenuLabelWidth(caption, size.width - 12.0f, subtle ? 0.25f : 0.34f, 0.18f);
    root->addChild(caption, 2);
    return root;
}

} // namespace

AchievementGradeUpLayer* AchievementGradeUpLayer::create(std::string oldGrade, std::string newGrade, std::function<void()> completion) {
    auto* layer = new AchievementGradeUpLayer();
    if (layer->init(std::move(oldGrade), std::move(newGrade), std::move(completion))) {
        layer->autorelease();
        return layer;
    }
    delete layer;
    return nullptr;
}

bool AchievementGradeUpLayer::init(std::string oldGrade, std::string newGrade, std::function<void()> completion) {
    if (!CCLayerColor::initWithColor({9, 10, 13, 255})) return false;
    m_old = std::move(oldGrade);
    m_new = std::move(newGrade);
    m_completion = std::move(completion);
    // Both cores must exist (downloaded rank assets); otherwise the grade is
    // simply shown by the grade plate without a motion.
    if (m_new.empty() || m_old == m_new || !core(m_new, 10.0f) || !core(m_old, 10.0f)) return false;

    auto const size = CCDirector::sharedDirector()->getWinSize();
    setContentSize(size);
    setKeypadEnabled(true);
    setID("achievement-grade-up"_spr);

    // Ground: ink, three slanted light bands and a radial glow in the new colour.
    auto const tone = gradeColor(m_new);
    struct Band { float x, width; GLubyte alpha; };
    for (auto const band : {Band{0.26f, 0.10f, 16}, Band{0.40f, 0.20f, 26}, Band{0.63f, 0.06f, 16}}) {
        auto* ray = CCLayerColor::create({tone.r, tone.g, tone.b, band.alpha});
        ray->setContentSize({size.width * band.width, size.height * 1.3f});
        ray->setPosition({size.width * band.x, -size.height * 0.15f});
        ray->setSkewX(14.0f);
        addChild(ray, 1);
    }
    if (auto* glow = fx(this, "fx-rank-radial-glow-white.png", center(), std::min(size.width, size.height) * 0.9f, 2, tone, 70)) {
        glow->runAction(CCRepeatForever::create(CCSequence::create(CCFadeTo::create(1.2f, 96), CCFadeTo::create(1.2f, 52), nullptr)));
    }

    m_root = CCNode::create();
    m_root->setContentSize(size);
    addChild(m_root, 20);

    m_menu = CCMenu::create();
    m_menu->setPosition(CCPointZero);
    addChild(m_menu, 100);
    m_skip = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("tap-to-skip", "TAP TO SKIP"), tone, true), this,
        menu_selector(AchievementGradeUpLayer::onSkip));
    m_skip->setPosition({size.width / 2.0f, 19.0f});
    m_skip->setVisible(false);
    m_skip->setEnabled(false);
    m_menu->addChild(m_skip);
    m_continue = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("continue", "CONTINUE"), tone, false), this,
        menu_selector(AchievementGradeUpLayer::onContinue));
    m_continue->setPosition({size.width / 2.0f, 30.0f});
    m_continue->setVisible(false);
    m_continue->setEnabled(false);
    m_menu->addChild(m_continue);

    play();
    runAction(CCSequence::create(CCDelayTime::create(0.6f), CallFuncExt::create([this] {
        if (m_final || m_finished) return;
        m_skipAllowed = true;
        m_skip->setVisible(true);
        m_skip->setEnabled(true);
    }), nullptr));
    return true;
}

CCPoint AchievementGradeUpLayer::center() const {
    auto const size = CCDirector::sharedDirector()->getWinSize();
    return {size.width / 2.0f, size.height / 2.0f + 22.0f};
}

// The grade's core on its 1024 px canvas: ~360 px of opaque diamond for the
// tier cores, ~500 px for the unranked core with its dark backing diamond.
// The artwork is already a square turned 45 degrees, so it is never rotated.
CCSprite* AchievementGradeUpLayer::core(std::string const& grade, float extent) const {
    auto const known = grade == "RED" || grade == "AQUA" || grade == "BRONZE" || grade == "SILVER" || grade == "GOLD";
    auto* sprite = rankResourceSprite("rank-" + (known ? lower(grade) : std::string("unranked")) + "-core.png");
    if (!sprite) return nullptr;
    auto const opaque = (known ? 360.0f : 500.0f) / 1024.0f * sprite->getContentSize().width;
    sprite->setScale(opaque > 0.0f ? extent / opaque : 1.0f);
    return sprite;
}

void AchievementGradeUpLayer::burst(ccColor3B color, int count, float distance, float delay, bool inward) {
    auto const at = center();
    for (int i = 0; i < count; ++i) {
        auto const angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count) + (unit(i, 7) - 0.5f) * 0.4f;
        auto const reach = distance * (0.75f + unit(i, 11) * 0.45f);
        CCPoint const edge{std::cos(angle) * reach, std::sin(angle) * reach};
        auto const side = 2.4f + unit(i, 13) * 3.4f;
        auto* shard = CCLayerColor::create({color.r, color.g, color.b, 0}, side, side);
        shard->ignoreAnchorPointForPosition(false);
        shard->setAnchorPoint({0.5f, 0.5f});
        shard->setRotation(45.0f);
        shard->setPosition(inward ? CCPoint{at.x + edge.x, at.y + edge.y} : at);
        m_root->addChild(shard, 14);
        auto const travel = inward ? CCPoint{-edge.x, -edge.y} : edge;
        shard->runAction(CCSequence::create(
            CCDelayTime::create(delay + unit(i, 17) * 0.18f),
            CCFadeTo::create(0.06f, static_cast<GLubyte>(150 + unit(i, 19) * 100.0f)),
            CCSpawn::create(CCEaseSineOut::create(CCMoveBy::create(inward ? 0.42f : 0.58f, travel)),
                CCRotateBy::create(0.5f, i % 2 ? 150.0f : -150.0f), nullptr),
            CCFadeOut::create(0.14f), CCRemoveSelf::create(), nullptr));
    }
}

void AchievementGradeUpLayer::flash(ccColor3B color, GLubyte opacity, float delay) {
    auto* layer = CCLayerColor::create({color.r, color.g, color.b, 0});
    layer->setContentSize(getContentSize());
    addChild(layer, 80);
    layer->runAction(CCSequence::create(CCDelayTime::create(delay), CCFadeTo::create(0.07f, opacity),
        CCFadeOut::create(0.22f), CCRemoveSelf::create(), nullptr));
}

void AchievementGradeUpLayer::play() {
    auto const at = center();
    auto const before = gradeColor(m_old), after = gradeColor(m_new);
    constexpr float extent = 112.0f;

    // 1. The current core settles in the middle.
    auto* old = core(m_old, extent);
    auto const oldScale = old->getScale();
    old->setPosition(at);
    old->setOpacity(0);
    old->setScale(oldScale * 0.82f);
    m_root->addChild(old, 10);
    old->runAction(CCSequence::create(
        CCSpawn::create(CCFadeIn::create(0.24f), CCEaseBackOut::create(CCScaleTo::create(0.34f, oldScale)), nullptr),
        // 2. It charges: a tightening shake while light gathers into it.
        CCDelayTime::create(0.12f),
        CCRepeat::create(CCSequence::create(CCMoveBy::create(0.035f, {2.2f, 0.0f}), CCMoveBy::create(0.035f, {-4.4f, 1.0f}),
            CCMoveBy::create(0.035f, {2.2f, -1.0f}), nullptr), 7),
        // 3. It breaks outward.
        CCSpawn::create(CCEaseSineOut::create(CCScaleTo::create(0.16f, oldScale * 1.28f)), CCFadeOut::create(0.16f), nullptr),
        CCRemoveSelf::create(), nullptr));
    if (auto* crack = fx(m_root, "fx-rank-crack-white.png", at, extent * 1.15f, 12, before, 0)) {
        crack->runAction(CCSequence::create(CCDelayTime::create(0.62f), CCFadeTo::create(0.5f, 210),
            CCFadeOut::create(0.12f), CCRemoveSelf::create(), nullptr));
    }
    burst(after, 18, 120.0f, 0.55f, true);
    flash({255, 255, 255}, 150, 1.22f);
    burst(before, 22, 150.0f, 1.24f, false);
    if (auto* wave = fx(m_root, "fx-rank-shockwave-diamond-white.png", at, 200.0f, 11, after, 0)) {
        auto const full = wave->getScale();
        wave->setScale(full * 0.25f);
        wave->runAction(CCSequence::create(CCDelayTime::create(1.24f), CCFadeTo::create(0.05f, 170),
            CCSpawn::create(CCEaseSineOut::create(CCScaleTo::create(0.46f, full * 1.2f)), CCFadeOut::create(0.46f), nullptr),
            CCRemoveSelf::create(), nullptr));
    }
    // 4. The new grade's core forms and the result is named.
    runAction(CCSequence::create(CCDelayTime::create(1.28f), CallFuncExt::create([this] { reveal(false); }), nullptr));
}

void AchievementGradeUpLayer::reveal(bool skipped) {
    if (m_final || m_finished) return;
    m_final = true;
    m_skipAllowed = false;
    stopAllActions();
    m_skip->setVisible(false);
    m_skip->setEnabled(false);
    if (skipped) {
        m_root->stopAllActions();
        m_root->removeAllChildrenWithCleanup(true);
    }
    auto const size = getContentSize();
    auto const at = center();
    auto const tone = gradeColor(m_new);

    auto* fresh = core(m_new, 118.0f);
    auto const scale = fresh->getScale();
    fresh->setPosition(at);
    m_root->addChild(fresh, 20);
    if (!skipped) {
        fresh->setScale(scale * 0.3f);
        fresh->runAction(CCEaseBackOut::create(CCScaleTo::create(0.48f, scale)));
        if (auto* glint = fx(m_root, "fx-rank-glint-white.png", {at.x + 30.0f, at.y + 34.0f}, 54.0f, 24, {255, 255, 255}, 0)) {
            glint->runAction(CCSequence::create(CCDelayTime::create(0.4f), CCFadeTo::create(0.1f, 220),
                CCSpawn::create(CCRotateBy::create(0.5f, 90.0f), CCFadeOut::create(0.5f), nullptr), CCRemoveSelf::create(), nullptr));
        }
        if (auto* sweep = fx(m_root, "fx-rank-light-sweep-white.png", {at.x - 80.0f, at.y}, 150.0f, 22, tone, 0)) {
            sweep->setRotation(-10.0f);
            sweep->runAction(CCSequence::create(CCDelayTime::create(0.3f), CCFadeTo::create(0.08f, 120),
                CCSpawn::create(CCMoveBy::create(0.46f, {160.0f, 0.0f}), CCFadeOut::create(0.46f), nullptr), CCRemoveSelf::create(), nullptr));
        }
    }
    // Idle shimmer of the new core.
    if (auto* halo = fx(m_root, "fx-rank-radial-glow-white.png", at, 210.0f, 15, tone, 0)) {
        halo->runAction(CCSequence::create(CCFadeTo::create(0.3f, 120),
            CCRepeat::create(CCSequence::create(CCFadeTo::create(1.1f, 70), CCFadeTo::create(1.1f, 120), nullptr), 1000), nullptr));
    }

    auto addLabel = [&](std::string const& text, float scale, float y, ccColor3B color, FontWeight weight, float delay) {
        auto* label = menuLabel(text, scale, {size.width / 2.0f, y}, color, weight);
        fitMenuLabelWidth(label, size.width - 60.0f, scale, scale * 0.5f);
        label->setOpacity(0);
        m_root->addChild(label, 30);
        label->runAction(CCSequence::create(CCDelayTime::create(skipped ? 0.0f : delay), CCFadeIn::create(0.2f), nullptr));
    };
    addLabel(i18n::text("achievement-grade-up", "ACHIEVEMENT GRADE UP"), 0.44f, at.y - 74.0f, tone, FontWeight::Black, 0.32f);
    addLabel(rankTierDisplay(m_new), 0.72f, at.y - 102.0f, m_new == "GOLD" ? Gold : Text, FontWeight::Black, 0.40f);
    auto const from = m_old.empty() ? i18n::text("achievement-grade-none", "NO GRADE") : rankTierDisplay(m_old);
    auto unlockText = std::string{};
    auto const unlock = m_new == "BRONZE" ? EliminationEffect::Shatter : m_new == "GOLD" ? EliminationEffect::Vortex : EliminationEffect::Default;
    if (unlock != EliminationEffect::Default) {
        auto const code = std::string(eliminationEffectCode(unlock));
        unlockText = "  ·  " + fmt::format(fmt::runtime(i18n::text("achievement-grade-unlock", "Unlocks {}")), i18n::text("elim-fx-" + code, code));
    }
    addLabel(fmt::format("{} → {}{}", from, rankTierDisplay(m_new), unlockText), 0.24f, at.y - 124.0f, TextMuted, FontWeight::SemiBold, 0.48f);

    m_continue->setVisible(true);
    m_continue->setEnabled(true);
    m_continue->setScale(0.84f);
    m_continue->runAction(CCSequence::create(CCDelayTime::create(skipped ? 0.0f : 0.5f), CCEaseBackOut::create(CCScaleTo::create(0.24f, 1.0f)), nullptr));
}

void AchievementGradeUpLayer::onSkip(CCObject*) {
    if (m_skipAllowed && !m_final) reveal(true);
}

void AchievementGradeUpLayer::onContinue(CCObject*) {
    if (m_final) finish();
}

void AchievementGradeUpLayer::keyBackClicked() {
    if (!m_final) { if (m_skipAllowed) reveal(true); return; }
    finish();
}

void AchievementGradeUpLayer::finish() {
    if (m_finished) return;
    m_finished = true;
    setVisible(false);
    if (m_menu) m_menu->setEnabled(false);
    // Keep the target alive until native touch/key dispatch has returned.
    auto const keepAlive = Ref<AchievementGradeUpLayer>(this);
    queueInMainThread([keepAlive] {
        keepAlive->stopAllActions();
        keepAlive->setKeypadEnabled(false);
        if (keepAlive->m_menu) {
            cancelMenuTouch(keepAlive->m_menu);
            keepAlive->m_menu->setTouchEnabled(false);
        }
        auto completion = std::move(keepAlive->m_completion);
        keepAlive->removeFromParentAndCleanup(true);
        if (completion) completion();
    });
}

} // namespace corum::ranked::ui
