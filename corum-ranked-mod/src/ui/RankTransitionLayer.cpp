#include "RankTransitionLayer.hpp"

#include "RankBadgeAssets.hpp"
#include "RankedVisuals.hpp"

#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float pseudoUnit(int index, int salt) {
    auto const value = static_cast<unsigned int>((index + 1) * 1103515245u + (salt + 19) * 12345u);
    return static_cast<float>((value >> 8) % 1000u) / 999.0f;
}

CCLayerColor* rectangle(
    ccColor3B color,
    CCSize size,
    CCPoint position,
    float rotation = 0.0f,
    GLubyte opacity = 230
) {
    auto* result = CCLayerColor::create({color.r, color.g, color.b, opacity});
    result->setContentSize(size);
    result->setAnchorPoint({0.5f, 0.5f});
    result->ignoreAnchorPointForPosition(false);
    result->setPosition(position);
    result->setRotation(rotation);
    return result;
}

CCNode* buttonVisual(std::string const& text, ccColor3B accent, bool subtle) {
    CCSize const size = subtle ? CCSize{150.0f, 24.0f} : CCSize{142.0f, 34.0f};
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});
    if (!subtle) {
        auto* panel = surface(size, {size.width / 2.0f, size.height / 2.0f}, accent, SurfaceRaised, 244, true);
        root->addChild(panel, 0);
    }
    auto* labelNode = menuLabel(
        text,
        subtle ? 0.25f : 0.34f,
        {size.width / 2.0f, size.height / 2.0f},
        subtle ? TextMuted : Text,
        subtle ? FontWeight::SemiBold : FontWeight::Black
    );
    corum::ranked::ui::fitMenuLabelWidth(labelNode, size.width - 12.0f, subtle ? 0.25f : 0.34f, 0.18f);
    root->addChild(labelNode, 2);
    return root;
}

CCPoint assemblyOffset(std::string const& role, float extent) {
    if (role == "spikes" || role == "radiant" || role == "orbit-back" || role == "orbit-front") {
        return CCPointZero;
    }
    if (role == "frame-top" || role == "crystal-top" || role == "crown") return {0.0f, extent};
    if (role == "frame-bottom" || role == "lower-crest") return {0.0f, -extent};
    if (role == "frame-left" || role == "wing-left" || role == "side-left") return {-extent, 0.0f};
    if (role == "frame-right" || role == "wing-right" || role == "side-right") return {extent, 0.0f};
    return {0.0f, extent * 0.45f};
}

float partDelayBias(std::string const& role) {
    if (role == "side-left" || role == "side-right") return 0.10f;
    if (role == "lower-crest") return 0.22f;
    if (role == "crown") return 0.48f;
    return 0.0f;
}

int demotionPriority(std::string const& role) {
    if (role == "crown") return 0;
    if (role == "side-left" || role == "side-right") return 1;
    if (role == "radiant" || role == "crystal-top") return 2;
    if (role == "lower-crest") return 3;
    return 4;
}

CCSprite* addFxSprite(
    CCNode* parent,
    std::string const& filename,
    CCPoint position,
    CCSize maxSize,
    int z,
    GLubyte opacity,
    ccColor3B const* tint = nullptr
) {
    auto* sprite = rankResourceSprite(filename);
    if (!sprite) return nullptr;
    fitRankSprite(sprite, maxSize);
    sprite->setPosition(position);
    sprite->setOpacity(opacity);
    if (tint) sprite->setColor(*tint);
    sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    parent->addChild(sprite, z);
    return sprite;
}

void animatePartAssembly(RankBadgePart const& part, float delay, CCPoint offset, float rotation) {
    auto* sprite = part.sprite;
    sprite->setPosition(offset);
    sprite->setRotation(rotation);
    sprite->setScale(part.assembledScale * 0.70f);
    sprite->setOpacity(0);
    sprite->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCFadeTo::create(0.12f, 255),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.52f, CCPointZero)),
            CCEaseBackOut::create(CCScaleTo::create(0.52f, part.assembledScale)),
            CCEaseSineOut::create(CCRotateTo::create(0.52f, 0.0f)),
            nullptr
        ),
        nullptr
    ));
}

} // namespace

RankTransitionLayer* RankTransitionLayer::create(
    std::string oldRank,
    std::string newRank,
    RankTransitionType type,
    std::string eventText,
    std::string skipText,
    std::string continueText,
    std::function<void()> completion
) {
    auto* result = new RankTransitionLayer();
    if (result && result->init(
        std::move(oldRank), std::move(newRank), type,
        std::move(eventText), std::move(skipText), std::move(continueText), std::move(completion)
    )) {
        result->autorelease();
        return result;
    }
    CC_SAFE_DELETE(result);
    return nullptr;
}

bool RankTransitionLayer::init(
    std::string oldRank,
    std::string newRank,
    RankTransitionType type,
    std::string eventText,
    std::string skipText,
    std::string continueText,
    std::function<void()> completion
) {
    if (!CCLayerColor::initWithColor({2, 5, 15, 255})) return false;

    m_oldRank = normalizeRankName(std::move(oldRank));
    m_newRank = normalizeRankName(std::move(newRank));
    m_type = type;
    m_eventText = std::move(eventText);
    m_skipText = std::move(skipText);
    m_continueText = std::move(continueText);
    m_completion = std::move(completion);

    if (!isKnownRankName(m_oldRank) || !isKnownRankName(m_newRank) || m_oldRank == m_newRank) return false;
    if (m_type == RankTransitionType::Placement && (m_oldRank != "UNRANKED" || m_newRank == "UNRANKED")) return false;
    if (m_type == RankTransitionType::Promotion && (m_oldRank == "UNRANKED" || m_newRank == "UNRANKED")) return false;
    if (m_type == RankTransitionType::Demotion &&
        (m_oldRank == "UNRANKED" || m_oldRank == "RED" || m_newRank == "UNRANKED")) return false;

    auto const winSize = CCDirector::sharedDirector()->getWinSize();
    setContentSize(winSize);
    setPosition(CCPointZero);
    setKeypadEnabled(true);
    setID("rank-transition-fullscreen"_spr);

    createBackground();
    m_animationRoot = CCNode::create();
    m_animationRoot->setContentSize(winSize);
    m_animationRoot->setPosition(CCPointZero);
    addChild(m_animationRoot, 20);
    createControls();

    switch (m_type) {
        case RankTransitionType::Placement: playPlacement(); break;
        case RankTransitionType::Promotion: playPromotion(); break;
        case RankTransitionType::Demotion: playDemotion(); break;
    }
    runAction(CCSequence::create(
        CCDelayTime::create(1.50f),
        CallFuncExt::create([this] { unlockSkip(); }),
        nullptr
    ));
    return true;
}

void RankTransitionLayer::createBackground() {
    auto const size = getContentSize();
    auto const profile = rankTierProfile(m_newRank);
    m_backgroundRoot = CCNode::create();
    m_backgroundRoot->setContentSize(size);
    m_backgroundRoot->setPosition(CCPointZero);
    addChild(m_backgroundRoot, 1);

    if (auto* backdrop = rankResourceSprite("rank-transition-background.png")) {
        coverRankSprite(backdrop, size);
        backdrop->setPosition({size.width / 2.0f, size.height / 2.0f});
        m_backgroundRoot->addChild(backdrop, 0);
    } else {
        auto* fallback = CCLayerColor::create({3, 8, 22, 255});
        fallback->setContentSize(size);
        m_backgroundRoot->addChild(fallback, 0);
    }
    if (auto* geometry = rankResourceSprite("rank-transition-geometry-overlay.png")) {
        coverRankSprite(geometry, size);
        geometry->setPosition({size.width / 2.0f, size.height / 2.0f});
        geometry->setColor(profile.color);
        geometry->setOpacity(58);
        geometry->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        m_backgroundRoot->addChild(geometry, 2);
        geometry->runAction(CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(1.8f, 78), CCFadeTo::create(1.8f, 42), nullptr
        )));
    }

    auto const glowSize = std::min(size.width, size.height) * 0.98f;
    auto* radial = addFxSprite(
        m_backgroundRoot, "fx-rank-radial-glow-white.png",
        {size.width / 2.0f, size.height / 2.0f + 14.0f},
        {glowSize, glowSize}, 3, m_newRank == "GOLD" ? 105 : 72, &profile.color
    );
    if (radial) radial->runAction(CCRepeatForever::create(CCSequence::create(
        CCFadeTo::create(1.2f, m_newRank == "GOLD" ? 128 : 92),
        CCFadeTo::create(1.2f, m_newRank == "GOLD" ? 76 : 48), nullptr
    )));

    auto* topLine = CCLayerColor::create({profile.color.r, profile.color.g, profile.color.b, 170});
    topLine->setContentSize({size.width - 72.0f, 1.2f});
    topLine->setPosition({36.0f, size.height - 26.0f});
    m_backgroundRoot->addChild(topLine, 6);
}

void RankTransitionLayer::createControls() {
    auto const size = getContentSize();
    m_controlMenu = CCMenu::create();
    m_controlMenu->setPosition(CCPointZero);
    addChild(m_controlMenu, 100);
    auto const accent = rankTierProfile(m_newRank).color;

    auto* skipVisual = buttonVisual(m_skipText, accent, true);
    m_skipItem = CCMenuItemSpriteExtra::create(skipVisual, this, menu_selector(RankTransitionLayer::onSkip));
    m_skipItem->setPosition({size.width / 2.0f, 19.0f});
    m_skipItem->setVisible(false);
    m_skipItem->setEnabled(false);
    m_controlMenu->addChild(m_skipItem, 1);

    auto* continueVisual = buttonVisual(m_continueText, accent, false);
    m_continueItem = CCMenuItemSpriteExtra::create(continueVisual, this, menu_selector(RankTransitionLayer::onContinue));
    m_continueItem->setPosition({size.width / 2.0f, 30.0f});
    m_continueItem->setVisible(false);
    m_continueItem->setEnabled(false);
    m_controlMenu->addChild(m_continueItem, 2);
}

void RankTransitionLayer::addParticles(
    std::string const& rank,
    int count,
    float radius,
    float distance,
    float delay,
    bool inward,
    bool falling
) {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 14.0f);
    auto const profile = rankTierProfile(rank);
    for (int index = 0; index < count; ++index) {
        auto const angle = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count)) +
            (pseudoUnit(index, 7) - 0.5f) * 0.42f;
        auto const ring = radius * (0.78f + pseudoUnit(index, 11) * 0.38f);
        auto const edge = ccp(std::cos(angle) * ring, std::sin(angle) * ring);
        auto const start = inward ? ccp(center.x + edge.x, center.y + edge.y) : center;
        CCPoint delta;
        if (inward) delta = ccp(-edge.x, -edge.y);
        else if (falling) delta = ccp(std::cos(angle) * distance * 0.42f, -distance * (0.72f + pseudoUnit(index, 13) * 0.60f));
        else delta = ccp(std::cos(angle) * distance, std::sin(angle) * distance);

        auto const side = 2.2f + pseudoUnit(index, 17) * 3.6f;
        auto* fragment = rectangle(
            index % 3 == 0 ? profile.secondary : profile.color,
            {side, side}, start, index % 2 == 0 ? 45.0f : 0.0f, 0
        );
        m_animationRoot->addChild(fragment, 14);
        fragment->runAction(CCSequence::create(
            CCDelayTime::create(delay + pseudoUnit(index, 23) * 0.22f),
            CCFadeTo::create(0.08f, static_cast<GLubyte>(155 + pseudoUnit(index, 29) * 95.0f)),
            CCSpawn::create(
                CCEaseSineOut::create(CCMoveBy::create(inward ? 0.48f : 0.62f, delta)),
                CCRotateBy::create(inward ? 0.48f : 0.62f, index % 2 == 0 ? 170.0f : -140.0f),
                CCScaleTo::create(inward ? 0.48f : 0.62f, inward ? 0.35f : 0.72f),
                nullptr
            ),
            CCFadeOut::create(0.16f), CCRemoveSelf::create(), nullptr
        ));
    }
}

void RankTransitionLayer::addFlash(ccColor3B color, GLubyte opacity, float delay) {
    auto* flash = CCLayerColor::create({color.r, color.g, color.b, 0});
    flash->setContentSize(getContentSize());
    flash->setPosition(CCPointZero);
    addChild(flash, 80);
    flash->runAction(CCSequence::create(
        CCDelayTime::create(delay), CCFadeTo::create(0.08f, opacity),
        CCFadeOut::create(0.19f), CCRemoveSelf::create(), nullptr
    ));
}

void RankTransitionLayer::addLightSweep(ccColor3B color, float delay) {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 14.0f);
    auto* sweep = addFxSprite(
        m_animationRoot, "fx-rank-light-sweep-white.png",
        {center.x - 82.0f, center.y}, {158.0f, 158.0f}, 32, 0, &color
    );
    if (!sweep) return;
    sweep->setRotation(-10.0f);
    sweep->runAction(CCSequence::create(
        CCDelayTime::create(delay), CCFadeTo::create(0.08f, 118),
        CCSpawn::create(CCMoveBy::create(0.46f, {164.0f, 0.0f}), CCFadeOut::create(0.46f), nullptr),
        CCRemoveSelf::create(), nullptr
    ));
}

void RankTransitionLayer::addShockwave(ccColor3B color, float delay, bool diamond) {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 14.0f);
    auto* wave = addFxSprite(
        m_animationRoot,
        diamond ? "fx-rank-shockwave-diamond-white.png" : "fx-rank-shockwave-circle-white.png",
        center, {192.0f, 192.0f}, 31, 0, &color
    );
    if (!wave) return;
    auto const finalScale = wave->getScale();
    wave->setScale(finalScale * 0.28f);
    wave->runAction(CCSequence::create(
        CCDelayTime::create(delay), CCFadeTo::create(0.06f, 150),
        CCSpawn::create(
            CCEaseSineOut::create(CCScaleTo::create(0.42f, finalScale * 1.18f)),
            CCFadeOut::create(0.42f), nullptr
        ),
        CCRemoveSelf::create(), nullptr
    ));
}

void RankTransitionLayer::addEnergyColumn(ccColor3B color, float delay) {
    auto const size = getContentSize();
    auto* column = addFxSprite(
        m_animationRoot, "fx-rank-energy-column-white.png",
        {size.width / 2.0f, size.height / 2.0f + 14.0f}, {94.0f, 250.0f}, 8, 0, &color
    );
    if (!column) return;
    column->runAction(CCSequence::create(
        CCDelayTime::create(delay), CCFadeTo::create(0.18f, 112),
        CCFadeTo::create(0.54f, 38), CCFadeOut::create(0.24f), CCRemoveSelf::create(), nullptr
    ));
}

void RankTransitionLayer::runShake(float strength, float delay) {
    auto const origin = m_animationRoot->getPosition();
    m_animationRoot->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCMoveTo::create(0.04f, {origin.x + strength, origin.y}),
        CCMoveTo::create(0.05f, {origin.x - strength * 1.4f, origin.y + strength * 0.25f}),
        CCMoveTo::create(0.04f, {origin.x + strength * 0.55f, origin.y}),
        CCMoveTo::create(0.05f, origin), nullptr
    ));
}

void RankTransitionLayer::scheduleFinalReveal(float duration) {
    runAction(CCSequence::create(
        CCDelayTime::create(duration),
        CallFuncExt::create([this] { revealFinalState(false); }),
        nullptr
    ));
}

void RankTransitionLayer::playPlacement() {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 15.0f);
    auto const target = rankTierProfile(m_newRank);

    auto unranked = createRankBadgeAssembly("UNRANKED", 112.0f);
    unranked.root->setPosition(center);
    unranked.root->setScale(0.50f);
    unranked.root->setOpacity(0);
    m_animationRoot->addChild(unranked.root, 12);
    unranked.root->runAction(CCSequence::create(
        CCFadeIn::create(0.26f), CCEaseBackOut::create(CCScaleTo::create(0.52f, 1.0f)),
        CCSpawn::create(CCRotateBy::create(1.05f, 7.0f), CCScaleTo::create(1.05f, 0.96f), nullptr),
        nullptr
    ));
    addEnergyColumn(target.color, 1.05f);
    if (auto* crack = addFxSprite(m_animationRoot, "fx-rank-crack-white.png", center, {122.0f, 122.0f}, 34, 0, &target.color)) {
        crack->runAction(CCSequence::create(
            CCDelayTime::create(1.48f), CCFadeTo::create(0.10f, 150),
            CCDelayTime::create(0.28f), CCFadeOut::create(0.24f), CCRemoveSelf::create(), nullptr
        ));
    }

    int fragmentIndex = 0;
    for (auto const& part : unranked.fragments) {
        auto const left = part.role.find("-l") != std::string::npos;
        auto const top = part.role.find("t") != std::string::npos;
        auto const delta = ccp(left ? -68.0f : 68.0f, top ? 58.0f : -64.0f);
        part.sprite->runAction(CCSequence::create(
            CCDelayTime::create(1.72f + fragmentIndex++ * 0.035f),
            CCSpawn::create(
                CCEaseSineOut::create(CCMoveBy::create(0.64f, delta)),
                CCRotateBy::create(0.64f, left ? -62.0f : 62.0f),
                CCFadeOut::create(0.64f), nullptr
            ),
            nullptr
        ));
    }
    for (auto const& part : unranked.baseParts) {
        part.sprite->runAction(CCSequence::create(
            CCDelayTime::create(1.76f),
            CCSpawn::create(CCScaleTo::create(0.42f, part.assembledScale * 1.12f), CCFadeOut::create(0.42f), nullptr),
            nullptr
        ));
    }
    addParticles("UNRANKED", 13, 30.0f, 58.0f, 1.72f, false, false);

    auto targetAssembly = createRankBadgeAssembly(m_newRank, m_newRank == "GOLD" ? 128.0f : 118.0f);
    targetAssembly.root->setPosition(center);
    m_animationRoot->addChild(targetAssembly.root, 40);
    for (auto const& part : targetAssembly.baseParts) {
        part.sprite->setOpacity(0);
        part.sprite->setScale(part.assembledScale * 0.62f);
        part.sprite->runAction(CCSequence::create(
            CCDelayTime::create(2.34f), CCFadeIn::create(0.16f),
            CCEaseBackOut::create(CCScaleTo::create(0.52f, part.assembledScale)), nullptr
        ));
    }
    for (std::size_t index = 0; index < targetAssembly.ornaments.size(); ++index) {
        auto const& part = targetAssembly.ornaments[index];
        auto const delay = 2.54f + static_cast<float>(index) * 0.22f + partDelayBias(part.role);
        animatePartAssembly(
            part, delay, assemblyOffset(part.role, m_newRank == "GOLD" ? 108.0f : 86.0f),
            index % 2 == 0 ? -8.0f : 8.0f
        );
    }

    auto const completeAt = 3.12f + static_cast<float>(targetAssembly.ornaments.size()) * 0.22f +
        (m_newRank == "GOLD" ? 0.48f : 0.0f);
    if (target.stage >= 4) addLightSweep(target.color, completeAt - 0.20f);
    addShockwave(target.color, completeAt, target.stage >= 3);
    addFlash(target.color, target.flashOpacity, completeAt + 0.08f);
    addParticles(m_newRank, target.particleCount + 3, 8.0f, target.stage == 5 ? 102.0f : 84.0f, completeAt + 0.10f, false, false);
    if (target.stage == 3 || target.stage == 5) runShake(target.shake, completeAt + 0.06f);
    scheduleFinalReveal(target.stage == 5 ? 7.75f : 6.20f + target.stage * 0.18f);
}

void RankTransitionLayer::playPromotion() {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 15.0f);
    auto const target = rankTierProfile(m_newRank);
    auto const gold = m_newRank == "GOLD";

    auto oldAssembly = createRankBadgeAssembly(m_oldRank, 114.0f);
    oldAssembly.root->setPosition(center);
    oldAssembly.root->setScale(0.72f);
    oldAssembly.root->setOpacity(0);
    m_animationRoot->addChild(oldAssembly.root, 16);
    oldAssembly.root->runAction(CCSequence::create(
        CCFadeIn::create(0.22f), CCEaseBackOut::create(CCScaleTo::create(0.44f, 1.0f)),
        CCSpawn::create(
            CCEaseSineInOut::create(CCRotateBy::create(1.30f, 8.0f)),
            CCEaseSineInOut::create(CCMoveBy::create(1.30f, {0.0f, 10.0f})), nullptr
        ),
        CCDelayTime::create(0.34f),
        CCSpawn::create(CCFadeOut::create(0.36f), CCScaleTo::create(0.36f, 0.86f), nullptr),
        nullptr
    ));
    addParticles(m_oldRank, std::max(8, target.particleCount - 3), 94.0f, 94.0f, 0.96f, true, false);
    addEnergyColumn(target.color, 1.18f);

    auto targetAssembly = createRankBadgeAssembly(m_newRank, gold ? 130.0f : 120.0f);
    targetAssembly.root->setPosition({center.x, center.y - 12.0f});
    m_animationRoot->addChild(targetAssembly.root, 40);
    // Every structural PNG shares one canvas origin. Lift the common root so
    // the body, core, and ornaments stay perfectly registered throughout the
    // assembly instead of moving only the base/core layers by 12 px.
    targetAssembly.root->runAction(CCSequence::create(
        CCDelayTime::create(2.16f),
        CCEaseSineOut::create(CCMoveTo::create(0.52f, center)),
        nullptr
    ));
    for (auto const& part : targetAssembly.baseParts) {
        part.sprite->setOpacity(0);
        part.sprite->setScale(part.assembledScale * 0.70f);
        part.sprite->runAction(CCSequence::create(
            CCDelayTime::create(2.16f), CCFadeIn::create(0.14f),
            CCEaseBackOut::create(CCScaleTo::create(0.52f, part.assembledScale)),
            nullptr
        ));
    }
    for (std::size_t index = 0; index < targetAssembly.ornaments.size(); ++index) {
        auto const& part = targetAssembly.ornaments[index];
        auto const delay = 2.38f + static_cast<float>(index) * 0.19f + partDelayBias(part.role);
        animatePartAssembly(
            part, delay, assemblyOffset(part.role, gold ? 112.0f : 88.0f),
            part.role.find("orbit") != std::string::npos ? -12.0f : (index % 2 == 0 ? -7.0f : 7.0f)
        );
    }

    auto const completeAt = 2.94f + static_cast<float>(targetAssembly.ornaments.size()) * 0.19f +
        (gold ? 0.48f : 0.0f);
    if (target.stage >= 4) addLightSweep(target.color, completeAt - 0.22f);
    addShockwave(target.color, completeAt, target.stage >= 3);
    addFlash(target.color, target.flashOpacity, completeAt + 0.08f);
    addParticles(m_newRank, target.particleCount + (gold ? 5 : 0), 8.0f, gold ? 104.0f : 82.0f, completeAt + 0.10f, false, false);
    if (target.stage == 3 || target.stage == 5) runShake(target.shake, completeAt + 0.04f);
    scheduleFinalReveal(gold ? 6.35f : 5.45f);
}

void RankTransitionLayer::playDemotion() {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 15.0f);
    auto const oldProfile = rankTierProfile(m_oldRank);

    auto oldAssembly = createRankBadgeAssembly(m_oldRank, m_oldRank == "GOLD" ? 128.0f : 118.0f);
    oldAssembly.root->setPosition(center);
    oldAssembly.root->setOpacity(0);
    m_animationRoot->addChild(oldAssembly.root, 24);
    oldAssembly.root->runAction(CCSequence::create(
        CCFadeIn::create(0.24f), CCDelayTime::create(0.38f),
        CCFadeTo::create(0.08f, 118), CCFadeTo::create(0.08f, 255),
        CCFadeTo::create(0.07f, 150), CCFadeTo::create(0.09f, 255), nullptr
    ));
    runShake(1.9f, 0.72f);
    if (auto* crack = addFxSprite(m_animationRoot, "fx-rank-crack-white.png", center, {128.0f, 128.0f}, 38, 0, &oldProfile.color)) {
        crack->runAction(CCSequence::create(
            CCDelayTime::create(0.78f), CCFadeTo::create(0.10f, 142),
            CCDelayTime::create(0.42f), CCFadeOut::create(0.30f), CCRemoveSelf::create(), nullptr
        ));
    }

    auto ornaments = oldAssembly.ornaments;
    std::stable_sort(ornaments.begin(), ornaments.end(), [](auto const& lhs, auto const& rhs) {
        return demotionPriority(lhs.role) < demotionPriority(rhs.role);
    });
    for (std::size_t index = 0; index < ornaments.size(); ++index) {
        auto const& part = ornaments[index];
        auto const delay = 0.92f + static_cast<float>(index) * 0.12f;
        auto const horizontal = part.role.find("left") != std::string::npos ? -22.0f :
            part.role.find("right") != std::string::npos ? 22.0f : (index % 2 == 0 ? -8.0f : 8.0f);
        part.sprite->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCSpawn::create(
                CCEaseSineIn::create(CCMoveBy::create(0.88f, {horizontal, -112.0f})),
                CCRotateBy::create(0.88f, horizontal < 0.0f ? -38.0f : 38.0f),
                CCScaleTo::create(0.88f, part.assembledScale * 0.62f),
                CCFadeOut::create(0.88f), nullptr
            ),
            nullptr
        ));
    }
    for (auto const& part : oldAssembly.baseParts) {
        part.sprite->runAction(CCSequence::create(
            CCDelayTime::create(1.42f),
            CCSpawn::create(
                CCEaseSineIn::create(CCMoveBy::create(0.62f, {0.0f, -31.0f})),
                CCScaleTo::create(0.62f, part.assembledScale * 0.82f), CCFadeOut::create(0.62f), nullptr
            ),
            nullptr
        ));
    }
    addParticles(m_oldRank, oldProfile.particleCount, 18.0f, 88.0f, 0.96f, false, true);
    addShockwave(oldProfile.color, 1.56f, true);
    addFlash(oldProfile.color, 58, 1.62f);

    auto lowerAssembly = createRankBadgeAssembly(m_newRank, 114.0f);
    lowerAssembly.root->setPosition({center.x, center.y + 14.0f});
    lowerAssembly.root->setScale(0.84f);
    lowerAssembly.root->setOpacity(0);
    m_animationRoot->addChild(lowerAssembly.root, 18);
    lowerAssembly.root->runAction(CCSequence::create(
        CCDelayTime::create(2.02f), CCFadeIn::create(0.16f),
        CCEaseBounceOut::create(CCMoveTo::create(0.52f, center)),
        CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.0f)), nullptr
    ));
    addParticles(m_newRank, 7, 8.0f, 36.0f, 2.35f, false, true);
    scheduleFinalReveal(m_oldRank == "GOLD" ? 5.25f : 4.55f);
}

void RankTransitionLayer::unlockSkip() {
    if (m_finalShown || m_finished || !m_skipItem) return;
    m_skipAllowed = true;
    m_skipItem->setEnabled(true);
    m_skipItem->setVisible(true);
}

void RankTransitionLayer::revealFinalState(bool skipped) {
    if (m_finalShown || m_finished) return;
    m_finalShown = true;
    m_skipAllowed = false;
    stopAllActions();
    if (m_skipItem) {
        m_skipItem->setEnabled(false);
        m_skipItem->setVisible(false);
    }
    if (m_animationRoot) {
        m_animationRoot->stopAllActions();
        m_animationRoot->removeAllChildrenWithCleanup(true);
    }

    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 26.0f);
    auto const target = rankTierProfile(m_newRank);
    auto const badgeSize = m_newRank == "GOLD" ? 138.0f : 126.0f;
    auto* finalBadge = createAnimatedRankBadge(
        m_newRank, {badgeSize, badgeSize}, RankBadgeDisplay::Hero
    );
    finalBadge->setPosition(center);
    finalBadge->setScale(skipped ? 1.0f : 0.82f);
    m_animationRoot->addChild(finalBadge, 20);
    if (!skipped) {
        finalBadge->runAction(CCEaseBackOut::create(CCScaleTo::create(0.30f, 1.0f)));
        addFlash(target.color, static_cast<GLubyte>(std::min(105, static_cast<int>(target.flashOpacity))), 0.02f);
        addParticles(m_newRank, 10 + target.stage, 8.0f, 66.0f, 0.04f, false, false);
    }

    auto* eventLabel = menuLabel(
        m_eventText, 0.47f,
        {size.width / 2.0f, size.height / 2.0f - 62.0f},
        target.color, FontWeight::Black
    );
    eventLabel->setOpacity(0);
    m_animationRoot->addChild(eventLabel, 30);
    auto* tierLabel = menuLabel(
        m_newRank, m_newRank == "GOLD" ? 0.78f : 0.68f,
        {size.width / 2.0f, size.height / 2.0f - 92.0f},
        m_newRank == "GOLD" ? Gold : Text, FontWeight::Black
    );
    tierLabel->setOpacity(0);
    m_animationRoot->addChild(tierLabel, 31);
    eventLabel->runAction(CCFadeIn::create(0.18f));
    tierLabel->runAction(CCSequence::create(CCDelayTime::create(0.08f), CCFadeIn::create(0.22f), nullptr));

    if (m_continueItem) {
        m_continueItem->setVisible(true);
        m_continueItem->setEnabled(true);
        m_continueItem->setScale(0.84f);
        m_continueItem->runAction(CCEaseBackOut::create(CCScaleTo::create(0.24f, 1.0f)));
    }
}

void RankTransitionLayer::onSkip(CCObject*) {
    if (!m_skipAllowed || m_finalShown) return;
    revealFinalState(true);
}

void RankTransitionLayer::onContinue(CCObject*) {
    if (!m_finalShown || m_finished) return;
    m_finished = true;
    setKeypadEnabled(false);
    if (m_controlMenu) m_controlMenu->setTouchEnabled(false);
    auto completion = std::move(m_completion);
    if (completion) completion();
    removeFromParentAndCleanup(true);
}

void RankTransitionLayer::keyBackClicked() {
    if (m_skipAllowed && !m_finalShown) revealFinalState(true);
}

} // namespace corum::ranked::ui

