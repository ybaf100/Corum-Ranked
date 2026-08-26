#include "RankTransitionAnimator.hpp"

#include "RankedVisuals.hpp"

#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string normalizeRank(std::string rank) {
    std::transform(rank.begin(), rank.end(), rank.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return rank;
}

bool isKnownRank(std::string const& rank) {
    static constexpr std::array<char const*, 6> ranks {
        "UNRANKED", "RED", "AQUA", "BRONZE", "SILVER", "GOLD",
    };
    return std::any_of(ranks.begin(), ranks.end(), [&rank](char const* value) {
        return rank == value;
    });
}

int rankIntensity(std::string const& rank) {
    if (rank == "AQUA") return 1;
    if (rank == "BRONZE") return 2;
    if (rank == "SILVER") return 3;
    if (rank == "GOLD") return 4;
    return 0;
}

float pseudoUnit(int index, int salt) {
    auto const value = static_cast<unsigned int>((index + 1) * 1103515245u + (salt + 17) * 12345u);
    return static_cast<float>((value >> 8) % 1000u) / 999.0f;
}

CCLayerColor* makeFragment(ccColor3B color, float side, bool diamond, GLubyte opacity) {
    auto* fragment = CCLayerColor::create({color.r, color.g, color.b, opacity});
    fragment->setContentSize({side, side});
    fragment->setAnchorPoint({0.5f, 0.5f});
    fragment->ignoreAnchorPointForPosition(false);
    if (diamond) fragment->setRotation(45.0f);
    return fragment;
}

} // namespace

RankTransitionAnimator* RankTransitionAnimator::create() {
    auto* result = new RankTransitionAnimator();
    if (result && result->init()) {
        result->autorelease();
        return result;
    }
    CC_SAFE_DELETE(result);
    return nullptr;
}

bool RankTransitionAnimator::init() {
    if (!CCNode::init()) return false;
    setAnchorPoint({0.5f, 0.5f});
    return true;
}

bool RankTransitionAnimator::playRankTransition(
    std::string oldRank,
    std::string newRank,
    RankTransitionType type,
    CCPoint position,
    std::string eventText,
    std::function<void()> completion
) {
    if (m_playing) return false;

    oldRank = normalizeRank(std::move(oldRank));
    newRank = normalizeRank(std::move(newRank));
    if (!isKnownRank(oldRank) || !isKnownRank(newRank) || oldRank == newRank) return false;

    // These guards mirror presentation invariants only. They never choose a
    // rank; the caller must pass the server-authoritative before/after tiers.
    if (type == RankTransitionType::Placement && (oldRank != "UNRANKED" || newRank == "UNRANKED")) {
        return false;
    }
    if (type == RankTransitionType::Promotion && (oldRank == "UNRANKED" || newRank == "UNRANKED")) {
        return false;
    }
    if (type == RankTransitionType::Demotion &&
        (oldRank == "UNRANKED" || oldRank == "RED" || newRank == "UNRANKED")) {
        return false;
    }

    m_playing = true;
    m_completion = std::move(completion);
    m_eventText = std::move(eventText);
    setPosition(position);

    switch (type) {
        case RankTransitionType::Placement:
            playPlacement(oldRank, newRank);
            break;
        case RankTransitionType::Promotion:
            playPromotion(oldRank, newRank);
            break;
        case RankTransitionType::Demotion:
            playDemotion(oldRank, newRank);
            break;
    }
    return true;
}

void RankTransitionAnimator::cancel() {
    if (!m_playing) return;
    m_playing = false;
    m_completion = {};
    stopAllActions();
    removeFromParentAndCleanup(true);
}

CCSprite* RankTransitionAnimator::makeBadge(std::string const& rank, float size) {
    auto* sprite = trophy(rank, {size, size});
    sprite->setPosition(CCPointZero);
    return sprite;
}

CCSprite* RankTransitionAnimator::makeGlow(std::string const& rank, float size, GLubyte opacity) {
    auto* glow = trophy(rank, {size, size});
    glow->setPosition(CCPointZero);
    glow->setColor(tierColor(rank));
    glow->setOpacity(opacity);
    glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    return glow;
}

void RankTransitionAnimator::addInwardParticles(
    std::string const& rank,
    int count,
    float radius,
    float startDelay
) {
    auto const color = tierColor(rank);
    for (int index = 0; index < count; ++index) {
        auto const angle = (2.0f * kPi * static_cast<float>(index) / static_cast<float>(count)) +
            (pseudoUnit(index, 3) - 0.5f) * 0.34f;
        auto const localRadius = radius * (0.78f + pseudoUnit(index, 7) * 0.34f);
        auto const start = ccp(std::cos(angle) * localRadius, std::sin(angle) * localRadius);
        auto* fragment = makeFragment(color, 2.0f + pseudoUnit(index, 11) * 2.1f, index % 2 == 0, 0);
        fragment->setPosition(start);
        fragment->setScale(0.70f + pseudoUnit(index, 13) * 0.65f);
        addChild(fragment, 7);
        fragment->runAction(CCSequence::create(
            CCDelayTime::create(startDelay + pseudoUnit(index, 17) * 0.13f),
            CCFadeTo::create(0.08f, 225),
            CCSpawn::create(
                CCEaseSineIn::create(CCMoveTo::create(0.30f, CCPointZero)),
                CCRotateBy::create(0.30f, index % 2 == 0 ? 135.0f : -110.0f),
                CCScaleTo::create(0.30f, 0.28f),
                nullptr
            ),
            CCFadeOut::create(0.07f),
            nullptr
        ));
    }
}

void RankTransitionAnimator::addBurstParticles(
    std::string const& rank,
    int count,
    float distance,
    float startDelay,
    bool falling
) {
    auto const color = tierColor(rank);
    for (int index = 0; index < count; ++index) {
        float angle = 0.0f;
        if (falling) {
            angle = -kPi * (0.18f + pseudoUnit(index, 19) * 0.64f);
        } else {
            angle = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(count) +
                (pseudoUnit(index, 23) - 0.5f) * 0.42f;
        }
        auto const travel = distance * (0.65f + pseudoUnit(index, 29) * 0.58f);
        auto const end = ccp(std::cos(angle) * travel, std::sin(angle) * travel);
        auto* fragment = makeFragment(color, 2.2f + pseudoUnit(index, 31) * 2.6f, index % 3 != 0, 0);
        fragment->setPosition(CCPointZero);
        fragment->setScale(0.65f + pseudoUnit(index, 37) * 0.75f);
        addChild(fragment, 8);
        fragment->runAction(CCSequence::create(
            CCDelayTime::create(startDelay + pseudoUnit(index, 41) * 0.08f),
            CCFadeTo::create(0.05f, 245),
            CCSpawn::create(
                CCEaseSineOut::create(CCMoveTo::create(falling ? 0.48f : 0.38f, end)),
                CCRotateBy::create(falling ? 0.48f : 0.38f, index % 2 == 0 ? 180.0f : -150.0f),
                nullptr
            ),
            CCFadeOut::create(falling ? 0.20f : 0.15f),
            nullptr
        ));
    }
}

void RankTransitionAnimator::addFlash(
    std::string const& rank,
    GLubyte peakOpacity,
    float startDelay,
    float size
) {
    auto const color = tierColor(rank);
    auto* flash = CCLayerColor::create({color.r, color.g, color.b, 0});
    flash->setContentSize({size, size});
    flash->setPosition({-size / 2.0f, -size / 2.0f});
    flash->setOpacity(0);
    addChild(flash, 1);
    flash->runAction(CCSequence::create(
        CCDelayTime::create(startDelay),
        CCFadeTo::create(0.055f, peakOpacity),
        CCFadeOut::create(0.13f),
        nullptr
    ));
}

void RankTransitionAnimator::addLightSweep(std::string const& rank, float startDelay) {
    auto const color = tierColor(rank);
    auto* sweep = CCLayerColor::create({color.r, color.g, color.b, 0});
    sweep->setContentSize({5.0f, 70.0f});
    sweep->setAnchorPoint({0.5f, 0.5f});
    sweep->ignoreAnchorPointForPosition(false);
    sweep->setRotation(-22.0f);
    sweep->setPosition({-42.0f, 0.0f});
    addChild(sweep, 12);
    sweep->runAction(CCSequence::create(
        CCDelayTime::create(startDelay),
        CCFadeTo::create(0.06f, 190),
        CCEaseSineInOut::create(CCMoveTo::create(0.25f, {42.0f, 0.0f})),
        CCFadeOut::create(0.08f),
        nullptr
    ));
}

void RankTransitionAnimator::addCaption(
    std::string const& text,
    std::string const& rank,
    float startDelay
) {
    auto const color = tierColor(rank);
    auto* event = label(text, 0.26f, {0.0f, -48.0f}, color, FontWeight::Black);
    event->setOpacity(0);
    event->limitLabelWidth(104.0f, 0.26f, 0.18f);
    addChild(event, 14);
    event->runAction(CCSequence::create(
        CCDelayTime::create(startDelay),
        CCFadeIn::create(0.10f),
        CCEaseBackOut::create(CCScaleTo::create(0.18f, 0.30f)),
        CCDelayTime::create(0.24f),
        CCFadeOut::create(0.12f),
        nullptr
    ));

    auto* tier = label(rank, 0.22f, {0.0f, -61.0f}, color, FontWeight::Black);
    tier->setOpacity(0);
    tier->limitLabelWidth(104.0f, 0.22f, 0.16f);
    addChild(tier, 14);
    tier->runAction(CCSequence::create(
        CCDelayTime::create(startDelay + 0.06f),
        CCFadeIn::create(0.10f),
        CCDelayTime::create(0.34f),
        CCFadeOut::create(0.12f),
        nullptr
    ));
}

void RankTransitionAnimator::runShake(float strength, float startDelay) {
    runAction(CCSequence::create(
        CCDelayTime::create(startDelay),
        CCMoveBy::create(0.035f, {strength, 0.0f}),
        CCMoveBy::create(0.035f, {-strength * 2.0f, 0.0f}),
        CCMoveBy::create(0.035f, {strength * 1.4f, 0.0f}),
        CCMoveBy::create(0.035f, {-strength * 0.4f, 0.0f}),
        nullptr
    ));
}

void RankTransitionAnimator::finishAfter(float duration) {
    runAction(CCSequence::create(
        CCDelayTime::create(duration),
        CallFuncExt::create([this] {
            if (!m_playing) return;
            m_playing = false;
            auto completion = std::move(m_completion);
            m_completion = {};
            if (completion) completion();
        }),
        CCRemoveSelf::create(true),
        nullptr
    ));
}

void RankTransitionAnimator::playPlacement(std::string const& oldRank, std::string const& newRank) {
    auto const intensity = rankIntensity(newRank);
    auto* oldBadge = makeBadge(oldRank, 48.0f);
    addChild(oldBadge, 6);
    oldBadge->runAction(CCSequence::create(
        CCScaleTo::create(0.16f, 0.91f),
        CCSpawn::create(
            CCScaleTo::create(0.22f, 0.66f),
            CCFadeOut::create(0.22f),
            CCRotateBy::create(0.22f, -10.0f),
            nullptr
        ),
        nullptr
    ));

    addBurstParticles("UNRANKED", 6 + intensity, 30.0f + intensity * 2.0f, 0.12f);

    auto* whiteCore = makeFragment({255, 255, 255}, 10.0f, true, 0);
    whiteCore->setPosition(CCPointZero);
    whiteCore->setScale(0.32f);
    addChild(whiteCore, 9);
    whiteCore->runAction(CCSequence::create(
        CCDelayTime::create(0.24f),
        CCFadeTo::create(0.08f, 245),
        CCScaleTo::create(0.13f, 1.18f),
        CCSpawn::create(
            CCFadeOut::create(0.13f),
            CCScaleTo::create(0.13f, 0.45f),
            nullptr
        ),
        nullptr
    ));

    addFlash(newRank, static_cast<GLubyte>(68 + intensity * 13), 0.36f, 88.0f + intensity * 3.0f);
    addInwardParticles(newRank, 8 + intensity * 2, 38.0f + intensity * 2.0f, 0.28f);

    auto* glow = makeGlow(newRank, 62.0f + intensity * 2.0f, static_cast<GLubyte>(85 + intensity * 15));
    glow->setScale(0.45f);
    glow->setOpacity(0);
    addChild(glow, 4);
    glow->runAction(CCSequence::create(
        CCDelayTime::create(0.38f),
        CCFadeIn::create(0.08f),
        CCScaleTo::create(0.26f, 1.15f + intensity * 0.025f),
        CCFadeOut::create(0.30f),
        nullptr
    ));

    auto* newBadge = makeBadge(newRank, 54.0f + intensity * 1.5f);
    newBadge->setScale(0.40f);
    newBadge->setOpacity(0);
    addChild(newBadge, 10);
    auto const overshoot = intensity == 4 ? 1.15f : 1.08f + intensity * 0.012f;
    newBadge->runAction(CCSequence::create(
        CCDelayTime::create(0.42f),
        CCFadeIn::create(0.07f),
        CCEaseBackOut::create(CCScaleTo::create(0.28f, overshoot)),
        CCScaleTo::create(0.13f, 1.0f),
        CCDelayTime::create(0.37f),
        CCFadeOut::create(0.10f),
        nullptr
    ));

    if (newRank == "SILVER" || newRank == "GOLD") addLightSweep(newRank, 0.63f);
    if (intensity >= 2) runShake(newRank == "GOLD" ? 3.2f : 2.0f, 0.60f);
    addFlash(newRank, static_cast<GLubyte>(72 + intensity * 12), 0.76f, 94.0f + intensity * 3.0f);
    addBurstParticles(newRank, 7 + intensity * 2, 34.0f + intensity * 3.0f, 0.77f);
    addCaption(m_eventText, newRank, 0.96f);
    finishAfter(1.58f + intensity * 0.025f);
}

void RankTransitionAnimator::playPromotion(std::string const& oldRank, std::string const& newRank) {
    auto const goldPromotion = oldRank == "SILVER" && newRank == "GOLD";
    auto* oldGlow = makeGlow(oldRank, 60.0f, 82);
    oldGlow->setOpacity(0);
    addChild(oldGlow, 4);
    oldGlow->runAction(CCSequence::create(
        CCFadeTo::create(0.10f, 118),
        CCScaleTo::create(0.20f, 1.15f),
        CCFadeOut::create(0.18f),
        nullptr
    ));

    auto* oldBadge = makeBadge(oldRank, 51.0f);
    addChild(oldBadge, 7);
    oldBadge->runAction(CCSequence::create(
        CCScaleTo::create(0.13f, 0.90f),
        CCDelayTime::create(0.24f),
        CCSpawn::create(
            CCFadeOut::create(0.13f),
            CCScaleTo::create(0.13f, 1.02f),
            nullptr
        ),
        nullptr
    ));

    addInwardParticles(oldRank, goldPromotion ? 15 : 11, goldPromotion ? 46.0f : 40.0f, 0.05f);
    if (goldPromotion) {
        auto* dim = CCLayerColor::create({0, 0, 8, 0});
        dim->setContentSize({102.0f, 104.0f});
        dim->setPosition({-51.0f, -52.0f});
        addChild(dim, 1);
        dim->runAction(CCSequence::create(
            CCFadeTo::create(0.15f, 82),
            CCDelayTime::create(0.25f),
            CCFadeOut::create(0.18f),
            nullptr
        ));
    }

    addFlash(newRank, goldPromotion ? 122 : 92, 0.43f, goldPromotion ? 108.0f : 94.0f);
    auto* newGlow = makeGlow(newRank, goldPromotion ? 70.0f : 64.0f, goldPromotion ? 150 : 110);
    newGlow->setPosition({0.0f, -15.0f});
    newGlow->setScale(0.55f);
    newGlow->setOpacity(0);
    addChild(newGlow, 5);
    newGlow->runAction(CCSequence::create(
        CCDelayTime::create(0.45f),
        CCFadeIn::create(0.08f),
        CCSpawn::create(
            CCEaseSineOut::create(CCMoveTo::create(0.30f, {0.0f, 7.0f})),
            CCScaleTo::create(0.30f, goldPromotion ? 1.28f : 1.18f),
            nullptr
        ),
        CCFadeOut::create(0.28f),
        nullptr
    ));

    auto* newBadge = makeBadge(newRank, goldPromotion ? 60.0f : 56.0f);
    newBadge->setPosition({0.0f, -17.0f});
    newBadge->setScale(0.75f);
    newBadge->setOpacity(0);
    addChild(newBadge, 10);
    newBadge->runAction(CCSequence::create(
        CCDelayTime::create(0.47f),
        CCFadeIn::create(0.07f),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.29f, {0.0f, goldPromotion ? 10.0f : 8.0f})),
            CCEaseBackOut::create(CCScaleTo::create(0.29f, goldPromotion ? 1.15f : 1.12f)),
            nullptr
        ),
        CCSpawn::create(
            CCEaseSineOut::create(CCMoveTo::create(0.13f, CCPointZero)),
            CCScaleTo::create(0.13f, 1.0f),
            nullptr
        ),
        CCDelayTime::create(0.32f),
        CCFadeOut::create(0.10f),
        nullptr
    ));

    if (oldRank == "AQUA") addLightSweep(oldRank, 0.26f);
    if (goldPromotion) addLightSweep(newRank, 0.57f);
    if (oldRank == "BRONZE" || goldPromotion) runShake(goldPromotion ? 3.2f : 2.1f, 0.66f);
    addBurstParticles(newRank, goldPromotion ? 19 : 13, goldPromotion ? 50.0f : 40.0f, 0.72f);
    addCaption(m_eventText, newRank, 0.97f);
    finishAfter(goldPromotion ? 1.70f : 1.55f);
}

void RankTransitionAnimator::playDemotion(std::string const& oldRank, std::string const& newRank) {
    auto* glow = makeGlow(oldRank, 60.0f, 100);
    glow->setOpacity(0);
    addChild(glow, 4);
    glow->runAction(CCSequence::create(
        CCFadeTo::create(0.07f, 110),
        CCFadeOut::create(0.06f),
        CCFadeTo::create(0.07f, 75),
        CCFadeOut::create(0.08f),
        nullptr
    ));

    auto* oldBadge = makeBadge(oldRank, 54.0f);
    addChild(oldBadge, 8);
    oldBadge->runAction(CCSequence::create(
        CCDelayTime::create(0.22f),
        CCSpawn::create(
            CCEaseSineIn::create(CCMoveBy::create(0.30f, {0.0f, -23.0f})),
            CCScaleTo::create(0.30f, 0.85f),
            CCRotateBy::create(0.30f, oldRank == "GOLD" ? 13.0f : 7.0f),
            CCFadeTo::create(0.30f, 80),
            nullptr
        ),
        CCFadeOut::create(0.07f),
        nullptr
    ));

    runShake(1.7f, 0.13f);
    addBurstParticles(oldRank, oldRank == "GOLD" ? 14 : 10, 37.0f, 0.20f, true);
    addFlash(oldRank, 52, 0.50f, 90.0f);

    auto* newBadge = makeBadge(newRank, 51.0f);
    newBadge->setPosition({0.0f, 9.0f});
    newBadge->setScale(0.86f);
    newBadge->setOpacity(0);
    addChild(newBadge, 10);
    newBadge->runAction(CCSequence::create(
        CCDelayTime::create(0.55f),
        CCFadeIn::create(0.08f),
        CCEaseBounceOut::create(CCMoveTo::create(0.28f, CCPointZero)),
        CCScaleTo::create(0.12f, 1.0f),
        CCDelayTime::create(0.31f),
        CCFadeOut::create(0.10f),
        nullptr
    ));
    addBurstParticles(newRank, 6, 25.0f, 0.68f, true);
    addCaption(m_eventText, newRank, 0.91f);
    finishAfter(1.48f);
}

} // namespace corum::ranked::ui
