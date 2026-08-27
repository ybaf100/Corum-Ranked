#include "RankTransitionLayer.hpp"

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

struct TierProfile {
    ccColor3B color;
    ccColor3B secondary;
    int stage;
    int particleCount;
    GLubyte flashOpacity;
    float shake;
};

std::string normalizeRank(std::string rank) {
    std::transform(rank.begin(), rank.end(), rank.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return rank;
}

int rankStage(std::string const& rank) {
    if (rank == "RED") return 1;
    if (rank == "AQUA") return 2;
    if (rank == "BRONZE") return 3;
    if (rank == "SILVER") return 4;
    if (rank == "GOLD") return 5;
    return 0;
}

bool isKnownRank(std::string const& rank) {
    return rank == "UNRANKED" || rankStage(rank) > 0;
}

TierProfile profileFor(std::string const& rank) {
    if (rank == "RED") return {{244, 76, 91}, {255, 128, 91}, 1, 13, 94, 1.4f};
    if (rank == "AQUA") return {{55, 220, 234}, {91, 154, 255}, 2, 15, 104, 1.6f};
    if (rank == "BRONZE") return {{208, 137, 70}, {255, 174, 78}, 3, 17, 110, 2.5f};
    if (rank == "SILVER") return {{201, 215, 235}, {111, 195, 255}, 4, 19, 122, 2.0f};
    if (rank == "GOLD") return {{255, 207, 74}, {255, 242, 145}, 5, 24, 142, 3.6f};
    return {{145, 160, 184}, {202, 211, 224}, 0, 9, 72, 0.8f};
}

float pseudoUnit(int index, int salt) {
    auto const value = static_cast<unsigned int>((index + 1) * 1103515245u + (salt + 19) * 12345u);
    return static_cast<float>((value >> 8) % 1000u) / 999.0f;
}

CCLayerColor* transparentContainer() {
    auto* result = CCLayerColor::create({255, 255, 255, 255});
    result->setContentSize(CCSizeZero);
    result->setAnchorPoint({0.5f, 0.5f});
    result->ignoreAnchorPointForPosition(false);
    result->setCascadeOpacityEnabled(true);
    return result;
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

    auto* labelNode = label(
        text,
        subtle ? 0.25f : 0.34f,
        {size.width / 2.0f, size.height / 2.0f},
        subtle ? TextMuted : Text,
        subtle ? FontWeight::SemiBold : FontWeight::Black
    );
    labelNode->limitLabelWidth(size.width - 12.0f, subtle ? 0.25f : 0.34f, 0.18f);
    root->addChild(labelNode, 2);
    return root;
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
        std::move(oldRank),
        std::move(newRank),
        type,
        std::move(eventText),
        std::move(skipText),
        std::move(continueText),
        std::move(completion)
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

    m_oldRank = normalizeRank(std::move(oldRank));
    m_newRank = normalizeRank(std::move(newRank));
    m_type = type;
    m_eventText = std::move(eventText);
    m_skipText = std::move(skipText);
    m_continueText = std::move(continueText);
    m_completion = std::move(completion);

    if (!isKnownRank(m_oldRank) || !isKnownRank(m_newRank) || m_oldRank == m_newRank) return false;
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
    auto const profile = profileFor(m_newRank);

    m_backgroundRoot = CCNode::create();
    m_backgroundRoot->setContentSize(size);
    m_backgroundRoot->setPosition(CCPointZero);
    addChild(m_backgroundRoot, 1);

    auto* upperGlow = CCLayerColor::create({profile.color.r, profile.color.g, profile.color.b, 28});
    upperGlow->setContentSize({size.width, size.height * 0.62f});
    upperGlow->setPosition({0.0f, size.height * 0.20f});
    m_backgroundRoot->addChild(upperGlow, 0);

    for (int index = 0; index < 6; ++index) {
        auto const side = 62.0f + static_cast<float>(index % 3) * 31.0f;
        auto* diamond = rectangle(
            index % 2 == 0 ? profile.color : profile.secondary,
            {side, side},
            {
                size.width * (0.10f + pseudoUnit(index, 2) * 0.80f),
                size.height * (0.12f + pseudoUnit(index, 5) * 0.76f),
            },
            45.0f,
            static_cast<GLubyte>(8 + index * 2)
        );
        m_backgroundRoot->addChild(diamond, 1);
        diamond->runAction(CCRepeatForever::create(CCRotateBy::create(
            12.0f + static_cast<float>(index),
            index % 2 == 0 ? 360.0f : -360.0f
        )));
    }

    for (int index = 0; index < 7; ++index) {
        auto* line = CCLayerColor::create({profile.color.r, profile.color.g, profile.color.b, 18});
        line->setContentSize({size.width - 46.0f, 0.7f});
        line->setPosition({23.0f, 32.0f + index * ((size.height - 64.0f) / 6.0f)});
        m_backgroundRoot->addChild(line, 2);
    }

    auto* topLine = CCLayerColor::create({profile.color.r, profile.color.g, profile.color.b, 150});
    topLine->setContentSize({size.width - 72.0f, 1.2f});
    topLine->setPosition({36.0f, size.height - 26.0f});
    m_backgroundRoot->addChild(topLine, 5);
}

void RankTransitionLayer::createControls() {
    auto const size = getContentSize();
    m_controlMenu = CCMenu::create();
    m_controlMenu->setPosition(CCPointZero);
    addChild(m_controlMenu, 100);

    auto* skipVisual = buttonVisual(m_skipText, profileFor(m_newRank).color, true);
    m_skipItem = CCMenuItemSpriteExtra::create(skipVisual, this, menu_selector(RankTransitionLayer::onSkip));
    m_skipItem->setPosition({size.width / 2.0f, 19.0f});
    m_skipItem->setVisible(false);
    m_skipItem->setEnabled(false);
    m_controlMenu->addChild(m_skipItem, 1);

    auto* continueVisual = buttonVisual(m_continueText, profileFor(m_newRank).color, false);
    m_continueItem = CCMenuItemSpriteExtra::create(continueVisual, this, menu_selector(RankTransitionLayer::onContinue));
    m_continueItem->setPosition({size.width / 2.0f, 30.0f});
    m_continueItem->setVisible(false);
    m_continueItem->setEnabled(false);
    m_controlMenu->addChild(m_continueItem, 2);
}

void RankTransitionLayer::addStageOrnaments(CCNode* parent, int stage, bool ambient) {
    auto const stageRank = std::array<std::string, 6>{"UNRANKED", "RED", "AQUA", "BRONZE", "SILVER", "GOLD"};
    auto const profile = profileFor(stageRank[std::clamp(stage, 0, 5)]);

    if (stage == 1) {
        for (int index = 0; index < 4; ++index) {
            auto const angle = 90.0f * index;
            auto const radians = angle * kPi / 180.0f;
            parent->addChild(rectangle(
                profile.color,
                {7.0f, 18.0f},
                {std::cos(radians) * 48.0f, std::sin(radians) * 48.0f},
                angle,
                228
            ), 2);
        }
        return;
    }

    if (stage == 2) {
        auto* orbit = transparentContainer();
        parent->addChild(orbit, 2);
        for (int index = 0; index < 12; ++index) {
            auto const angle = 2.0f * kPi * static_cast<float>(index) / 12.0f;
            auto* diamond = rectangle(
                profile.color,
                {3.2f + (index % 3 == 0 ? 1.5f : 0.0f), 3.2f + (index % 3 == 0 ? 1.5f : 0.0f)},
                {std::cos(angle) * 57.0f, std::sin(angle) * 57.0f},
                45.0f,
                static_cast<GLubyte>(index % 2 == 0 ? 238 : 142)
            );
            orbit->addChild(diamond, 2);
        }
        if (ambient) orbit->runAction(CCRepeatForever::create(CCRotateBy::create(4.6f, 360.0f)));
        return;
    }

    if (stage == 3) {
        for (int xSign : {-1, 1}) {
            for (int ySign : {-1, 1}) {
                auto const x = static_cast<float>(xSign) * 53.0f;
                auto const y = static_cast<float>(ySign) * 48.0f;
                parent->addChild(rectangle(profile.color, {17.0f, 5.0f}, {x - xSign * 6.0f, y}, 0.0f, 235), 3);
                parent->addChild(rectangle(profile.secondary, {5.0f, 17.0f}, {x, y - ySign * 6.0f}, 0.0f, 215), 3);
            }
        }
        return;
    }

    if (stage == 4) {
        for (int sign : {-1, 1}) {
            for (int index = 0; index < 3; ++index) {
                auto const x = static_cast<float>(sign) * (55.0f + index * 10.0f);
                auto const y = 15.0f - index * 12.0f;
                parent->addChild(rectangle(
                    index == 1 ? profile.secondary : profile.color,
                    {8.0f, 19.0f - index * 2.0f},
                    {x, y},
                    sign < 0 ? -52.0f + index * 7.0f : 52.0f - index * 7.0f,
                    225
                ), 4);
            }
        }
        return;
    }

    if (stage == 5) {
        for (int index = -2; index <= 2; ++index) {
            auto const height = index == 0 ? 20.0f : (std::abs(index) == 1 ? 15.0f : 10.0f);
            parent->addChild(rectangle(
                index % 2 == 0 ? profile.color : profile.secondary,
                {8.0f, height},
                {index * 10.0f, 59.0f + height / 2.0f},
                static_cast<float>(index) * 5.0f,
                245
            ), 5);
        }
        parent->addChild(rectangle(profile.color, {54.0f, 5.0f}, {0.0f, 58.0f}, 0.0f, 242), 5);
        for (int sign : {-1, 1}) {
            parent->addChild(rectangle(
                profile.secondary,
                {5.0f, 21.0f},
                {static_cast<float>(sign) * 73.0f, 6.0f},
                static_cast<float>(sign) * 58.0f,
                215
            ), 5);
        }
    }
}

CCLayerColor* RankTransitionLayer::makeOrnamentGroup(int stage, bool ambient) {
    auto* group = transparentContainer();
    addStageOrnaments(group, stage, ambient);
    return group;
}

CCLayerColor* RankTransitionLayer::makeAssembly(std::string const& rank, float badgeSize, bool ambient) {
    auto* assembly = transparentContainer();
    auto const profile = profileFor(rank);

    auto* glow = trophy(rank, {badgeSize + 18.0f, badgeSize + 18.0f});
    glow->setPosition(CCPointZero);
    glow->setColor(profile.color);
    glow->setOpacity(rank == "GOLD" ? 116 : 72);
    glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    assembly->addChild(glow, 0);
    if (ambient) {
        glow->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(0.75f, glow->getScale() * 1.06f),
            CCScaleTo::create(0.75f, glow->getScale()),
            nullptr
        )));
    }

    auto* badge = trophy(rank, {badgeSize, badgeSize});
    badge->setPosition(CCPointZero);
    assembly->addChild(badge, 3);

    for (int stage = 1; stage <= rankStage(rank); ++stage) {
        auto* ornaments = makeOrnamentGroup(stage, ambient);
        assembly->addChild(ornaments, 5 + stage);
    }
    return assembly;
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
    auto const profile = profileFor(rank);
    for (int index = 0; index < count; ++index) {
        auto const angle = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count)) +
            (pseudoUnit(index, 7) - 0.5f) * 0.42f;
        auto const ring = radius * (0.78f + pseudoUnit(index, 11) * 0.38f);
        auto const edge = ccp(std::cos(angle) * ring, std::sin(angle) * ring);
        auto const start = inward ? ccp(center.x + edge.x, center.y + edge.y) : center;
        CCPoint delta;
        if (inward) {
            delta = ccp(-edge.x, -edge.y);
        } else if (falling) {
            delta = ccp(std::cos(angle) * distance * 0.42f, -distance * (0.72f + pseudoUnit(index, 13) * 0.60f));
        } else {
            delta = ccp(std::cos(angle) * distance, std::sin(angle) * distance);
        }

        auto const side = 2.2f + pseudoUnit(index, 17) * 3.6f;
        auto* fragment = rectangle(
            index % 3 == 0 ? profile.secondary : profile.color,
            {side, side},
            start,
            index % 2 == 0 ? 45.0f : 0.0f,
            0
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
            CCFadeOut::create(0.16f),
            CCRemoveSelf::create(),
            nullptr
        ));
    }
}

void RankTransitionLayer::addFlash(ccColor3B color, GLubyte opacity, float delay) {
    auto* flash = CCLayerColor::create({color.r, color.g, color.b, 0});
    flash->setContentSize(getContentSize());
    flash->setPosition(CCPointZero);
    addChild(flash, 80);
    flash->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCFadeTo::create(0.08f, opacity),
        CCFadeOut::create(0.19f),
        CCRemoveSelf::create(),
        nullptr
    ));
}

void RankTransitionLayer::addLightSweep(ccColor3B color, float delay) {
    auto const size = getContentSize();
    auto* sweep = CCLayerColor::create({color.r, color.g, color.b, 0});
    sweep->setContentSize({13.0f, 145.0f});
    sweep->setAnchorPoint({0.5f, 0.5f});
    sweep->ignoreAnchorPointForPosition(false);
    sweep->setRotation(-22.0f);
    sweep->setPosition({size.width / 2.0f - 86.0f, size.height / 2.0f + 12.0f});
    m_animationRoot->addChild(sweep, 32);
    sweep->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCFadeTo::create(0.07f, 95),
        CCSpawn::create(
            CCMoveBy::create(0.42f, {172.0f, 0.0f}),
            CCFadeOut::create(0.42f),
            nullptr
        ),
        CCRemoveSelf::create(),
        nullptr
    ));
}

void RankTransitionLayer::runShake(float strength, float delay) {
    auto const origin = m_animationRoot->getPosition();
    m_animationRoot->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCMoveTo::create(0.04f, {origin.x + strength, origin.y}),
        CCMoveTo::create(0.05f, {origin.x - strength * 1.4f, origin.y + strength * 0.25f}),
        CCMoveTo::create(0.04f, {origin.x + strength * 0.55f, origin.y}),
        CCMoveTo::create(0.05f, origin),
        nullptr
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
    auto const target = profileFor(m_newRank);

    auto* unranked = makeAssembly("UNRANKED", 104.0f, false);
    unranked->setPosition(center);
    unranked->setScale(0.52f);
    unranked->setOpacity(0);
    m_animationRoot->addChild(unranked, 12);
    unranked->runAction(CCSequence::create(
        CCFadeIn::create(0.28f),
        CCEaseBackOut::create(CCScaleTo::create(0.52f, 1.0f)),
        CCSpawn::create(
            CCRotateBy::create(1.15f, 7.0f),
            CCScaleTo::create(1.15f, 0.94f),
            nullptr
        ),
        CCDelayTime::create(0.56f),
        CCSpawn::create(
            CCFadeOut::create(0.38f),
            CCScaleTo::create(0.38f, 1.18f),
            CCRotateBy::create(0.38f, -13.0f),
            nullptr
        ),
        nullptr
    ));

    addParticles("UNRANKED", 13, 34.0f, 52.0f, 1.70f, false, false);
    addParticles(m_newRank, target.particleCount, 91.0f, 91.0f, 1.34f, true, false);

    for (int stage = 1; stage <= target.stage; ++stage) {
        auto* additions = makeOrnamentGroup(stage, false);
        additions->setPosition(center);
        additions->setScale(1.42f);
        additions->setRotation(stage % 2 == 0 ? -8.0f : 8.0f);
        additions->setOpacity(0);
        m_animationRoot->addChild(additions, 18 + stage);
        auto const delay = 2.12f + static_cast<float>(stage - 1) * 0.34f;
        additions->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCFadeTo::create(0.14f, 220),
            CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(0.34f, 1.0f)),
                CCEaseSineOut::create(CCRotateTo::create(0.34f, 0.0f)),
                nullptr
            ),
            nullptr
        ));
        if (stage == 3 || stage == 5) runShake(stage == 5 ? 3.4f : 2.2f, delay + 0.28f);
    }

    auto const completeAt = 3.00f + static_cast<float>(target.stage) * 0.34f;
    auto* finalAssembly = makeAssembly(m_newRank, target.stage == 5 ? 122.0f : 112.0f, true);
    finalAssembly->setPosition(center);
    finalAssembly->setScale(0.62f);
    finalAssembly->setOpacity(0);
    m_animationRoot->addChild(finalAssembly, 40);
    finalAssembly->runAction(CCSequence::create(
        CCDelayTime::create(completeAt),
        CCFadeIn::create(0.10f),
        CCEaseBackOut::create(CCScaleTo::create(0.48f, target.stage == 5 ? 1.16f : 1.10f)),
        CCScaleTo::create(0.18f, 1.0f),
        nullptr
    ));

    if (target.stage >= 4) addLightSweep(target.color, completeAt + 0.24f);
    addFlash(target.color, target.flashOpacity, completeAt + 0.34f);
    addParticles(m_newRank, target.particleCount + 3, 8.0f, 87.0f, completeAt + 0.42f, false, false);
    scheduleFinalReveal(target.stage == 5 ? 7.65f : 6.15f + target.stage * 0.18f);
}

void RankTransitionLayer::playPromotion() {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 15.0f);
    auto const target = profileFor(m_newRank);
    auto const gold = m_newRank == "GOLD";

    auto* oldAssembly = makeAssembly(m_oldRank, 108.0f, true);
    oldAssembly->setPosition(center);
    oldAssembly->setScale(0.76f);
    oldAssembly->setOpacity(0);
    m_animationRoot->addChild(oldAssembly, 15);
    oldAssembly->runAction(CCSequence::create(
        CCFadeIn::create(0.22f),
        CCEaseBackOut::create(CCScaleTo::create(0.42f, 1.0f)),
        CCSpawn::create(
            CCEaseSineInOut::create(CCRotateBy::create(1.25f, 8.0f)),
            CCEaseSineInOut::create(CCMoveBy::create(1.25f, {0.0f, 8.0f})),
            nullptr
        ),
        CCDelayTime::create(0.48f),
        CCSpawn::create(
            CCFadeOut::create(0.30f),
            CCScaleTo::create(0.30f, 0.88f),
            nullptr
        ),
        nullptr
    ));

    addParticles(m_oldRank, target.particleCount - 3, 92.0f, 92.0f, 0.92f, true, false);

    auto* newParts = makeOrnamentGroup(target.stage, false);
    newParts->setPosition({center.x, center.y + (target.stage == 5 ? 42.0f : 0.0f)});
    newParts->setScale(target.stage == 4 ? 0.36f : 1.38f);
    newParts->setRotation(target.stage == 2 ? -8.0f : (target.stage == 5 ? -6.0f : 7.0f));
    newParts->setOpacity(0);
    m_animationRoot->addChild(newParts, 30);
    newParts->runAction(CCSequence::create(
        CCDelayTime::create(1.86f),
        CCFadeTo::create(0.18f, 228),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.56f, center)),
            CCEaseBackOut::create(CCScaleTo::create(0.56f, 1.0f)),
            CCEaseSineOut::create(CCRotateTo::create(0.56f, 0.0f)),
            nullptr
        ),
        nullptr
    ));

    auto* finalAssembly = makeAssembly(m_newRank, gold ? 124.0f : 114.0f, true);
    finalAssembly->setPosition({center.x, center.y - 16.0f});
    finalAssembly->setScale(0.68f);
    finalAssembly->setOpacity(0);
    m_animationRoot->addChild(finalAssembly, 40);
    finalAssembly->runAction(CCSequence::create(
        CCDelayTime::create(2.54f),
        CCFadeIn::create(0.10f),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.50f, {center.x, center.y + (gold ? 10.0f : 7.0f)})),
            CCEaseBackOut::create(CCScaleTo::create(0.50f, gold ? 1.15f : 1.11f)),
            nullptr
        ),
        CCSpawn::create(
            CCEaseSineOut::create(CCMoveTo::create(0.18f, center)),
            CCScaleTo::create(0.18f, 1.0f),
            nullptr
        ),
        nullptr
    ));

    if (target.stage >= 4) addLightSweep(target.color, 2.72f);
    addFlash(target.color, target.flashOpacity, gold ? 3.08f : 2.96f);
    addParticles(m_newRank, target.particleCount + (gold ? 5 : 0), 8.0f, gold ? 98.0f : 78.0f, 3.05f, false, false);
    if (target.stage == 3 || target.stage == 5) runShake(target.shake, 2.36f);
    scheduleFinalReveal(gold ? 6.85f : 5.65f + target.stage * 0.10f);
}

void RankTransitionLayer::playDemotion() {
    auto const size = getContentSize();
    auto const center = ccp(size.width / 2.0f, size.height / 2.0f + 15.0f);
    auto const oldProfile = profileFor(m_oldRank);
    auto const nextProfile = profileFor(m_newRank);

    auto* oldAssembly = makeAssembly(m_oldRank, m_oldRank == "GOLD" ? 122.0f : 112.0f, false);
    oldAssembly->setPosition(center);
    oldAssembly->setOpacity(0);
    m_animationRoot->addChild(oldAssembly, 18);
    oldAssembly->runAction(CCSequence::create(
        CCFadeIn::create(0.24f),
        CCDelayTime::create(0.42f),
        CCFadeTo::create(0.08f, 110),
        CCFadeTo::create(0.08f, 255),
        CCFadeTo::create(0.07f, 145),
        CCFadeTo::create(0.09f, 255),
        CCDelayTime::create(0.30f),
        CCSpawn::create(
            CCEaseSineIn::create(CCMoveBy::create(0.58f, {0.0f, -27.0f})),
            CCScaleTo::create(0.58f, 0.82f),
            CCRotateBy::create(0.58f, m_oldRank == "GOLD" ? 12.0f : 7.0f),
            CCFadeOut::create(0.58f),
            nullptr
        ),
        nullptr
    ));

    auto* detached = makeOrnamentGroup(oldProfile.stage, false);
    detached->setPosition(center);
    detached->setOpacity(0);
    m_animationRoot->addChild(detached, 36);
    detached->runAction(CCSequence::create(
        CCDelayTime::create(0.92f),
        CCFadeIn::create(0.09f),
        CCSpawn::create(
            CCEaseSineIn::create(CCMoveBy::create(0.88f, {m_oldRank == "GOLD" ? 12.0f : -7.0f, -96.0f})),
            CCRotateBy::create(0.88f, m_oldRank == "GOLD" ? 31.0f : -24.0f),
            CCScaleTo::create(0.88f, 0.64f),
            CCFadeOut::create(0.88f),
            nullptr
        ),
        nullptr
    ));

    addParticles(m_oldRank, oldProfile.particleCount, 18.0f, 82.0f, 0.96f, false, true);
    runShake(1.8f, 0.74f);
    addFlash(oldProfile.color, 58, 1.62f);

    auto* lowerAssembly = makeAssembly(m_newRank, 108.0f, true);
    lowerAssembly->setPosition({center.x, center.y + 12.0f});
    lowerAssembly->setScale(0.84f);
    lowerAssembly->setOpacity(0);
    m_animationRoot->addChild(lowerAssembly, 22);
    lowerAssembly->runAction(CCSequence::create(
        CCDelayTime::create(1.75f),
        CCFadeIn::create(0.16f),
        CCEaseBounceOut::create(CCMoveTo::create(0.50f, center)),
        CCScaleTo::create(0.18f, 1.0f),
        nullptr
    ));
    addParticles(m_newRank, 7, 8.0f, 34.0f, 2.16f, false, true);
    scheduleFinalReveal(m_oldRank == "GOLD" ? 5.35f : 4.75f);
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
    auto const target = profileFor(m_newRank);
    auto* finalAssembly = makeAssembly(m_newRank, m_newRank == "GOLD" ? 132.0f : 120.0f, true);
    finalAssembly->setPosition(center);
    finalAssembly->setScale(skipped ? 1.0f : 0.82f);
    m_animationRoot->addChild(finalAssembly, 20);
    if (!skipped) {
        finalAssembly->runAction(CCEaseBackOut::create(CCScaleTo::create(0.30f, 1.0f)));
        addFlash(target.color, static_cast<GLubyte>(std::min(105, static_cast<int>(target.flashOpacity))), 0.02f);
        addParticles(m_newRank, 10 + target.stage, 8.0f, 62.0f, 0.04f, false, false);
    }

    auto* eventLabel = label(
        m_eventText,
        0.47f,
        {size.width / 2.0f, size.height / 2.0f - 62.0f},
        target.color,
        FontWeight::Black
    );
    eventLabel->setOpacity(0);
    m_animationRoot->addChild(eventLabel, 30);

    auto* tierLabel = label(
        m_newRank,
        m_newRank == "GOLD" ? 0.78f : 0.68f,
        {size.width / 2.0f, size.height / 2.0f - 92.0f},
        m_newRank == "GOLD" ? Gold : Text,
        FontWeight::Black
    );
    tierLabel->setOpacity(0);
    m_animationRoot->addChild(tierLabel, 31);
    eventLabel->runAction(CCFadeIn::create(0.18f));
    tierLabel->runAction(CCSequence::create(
        CCDelayTime::create(0.08f),
        CCFadeIn::create(0.20f),
        nullptr
    ));

    if (m_continueItem) {
        m_continueItem->setVisible(true);
        m_continueItem->setEnabled(true);
        m_continueItem->setScale(0.78f);
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
