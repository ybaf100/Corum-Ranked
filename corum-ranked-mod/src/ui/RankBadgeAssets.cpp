#include "RankBadgeAssets.hpp"

#include "RankedVisuals.hpp"
#include "../domain/TransitionTrace.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <utility>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

struct PartSpec {
    char const* filename;
    char const* role;
    int z;
    bool base;
    bool fragment;
};

std::vector<PartSpec> partsFor(std::string const& rank) {
    if (rank == "UNRANKED") return {
        {"rank-unranked-body.png", "body", 10, true, false},
        {"rank-unranked-core.png", "core", 20, true, false},
        {"rank-unranked-fragment-tl.png", "fragment-tl", 30, false, true},
        {"rank-unranked-fragment-tr.png", "fragment-tr", 30, false, true},
        {"rank-unranked-fragment-bl.png", "fragment-bl", 30, false, true},
        {"rank-unranked-fragment-br.png", "fragment-br", 30, false, true},
    };
    if (rank == "RED") return {
        {"rank-red-body.png", "body", 10, true, false},
        {"rank-red-spikes.png", "spikes", 15, false, false},
        {"rank-red-core.png", "core", 20, true, false},
    };
    if (rank == "AQUA") return {
        {"rank-aqua-orbit-back.png", "orbit-back", 5, false, false},
        {"rank-aqua-body.png", "body", 10, true, false},
        {"rank-aqua-core.png", "core", 20, true, false},
        {"rank-aqua-orbit-front.png", "orbit-front", 30, false, false},
    };
    if (rank == "BRONZE") return {
        {"rank-bronze-body.png", "body", 10, true, false},
        {"rank-bronze-frame-top.png", "frame-top", 15, false, false},
        {"rank-bronze-frame-bottom.png", "frame-bottom", 15, false, false},
        {"rank-bronze-frame-left.png", "frame-left", 15, false, false},
        {"rank-bronze-frame-right.png", "frame-right", 15, false, false},
        {"rank-bronze-core.png", "core", 20, true, false},
    };
    if (rank == "SILVER") return {
        {"rank-silver-wing-left.png", "wing-left", 5, false, false},
        {"rank-silver-wing-right.png", "wing-right", 5, false, false},
        {"rank-silver-body.png", "body", 10, true, false},
        {"rank-silver-core.png", "core", 20, true, false},
        {"rank-silver-crystal-top.png", "crystal-top", 25, false, false},
    };
    if (rank == "GOLD") return {
        {"rank-gold-radiant-ornament.png", "radiant", 2, false, false},
        {"rank-gold-side-left.png", "side-left", 5, false, false},
        {"rank-gold-side-right.png", "side-right", 5, false, false},
        {"rank-gold-body.png", "body", 10, true, false},
        {"rank-gold-core.png", "core", 20, true, false},
        {"rank-gold-lower-crest.png", "lower-crest", 25, false, false},
        {"rank-gold-crown.png", "crown", 30, false, false},
    };
    return {};
}

CCLayerColor* centeredRoot() {
    auto* root = CCLayerColor::create({0, 0, 0, 0});
    root->setContentSize(CCSizeZero);
    root->setCascadeColorEnabled(true);
    root->setCascadeOpacityEnabled(true);
    return root;
}

void makeAdditive(CCSprite* sprite) {
    if (sprite) sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
}

CCSprite* ambientSprite(
    CCNode* parent,
    std::string const& filename,
    CCSize maxSize,
    int z,
    GLubyte opacity,
    ccColor3B const* tint = nullptr
) {
    auto* sprite = rankResourceSprite(filename);
    if (!sprite) return nullptr;
    fitRankSprite(sprite, maxSize);
    sprite->setPosition(CCPointZero);
    sprite->setOpacity(opacity);
    if (tint) sprite->setColor(*tint);
    makeAdditive(sprite);
    parent->addChild(sprite, z);
    return sprite;
}

void pulseOpacity(CCSprite* sprite, GLubyte low, GLubyte high, float duration) {
    if (!sprite) return;
    sprite->runAction(CCRepeatForever::create(CCSequence::create(
        CCEaseSineInOut::create(CCFadeTo::create(duration, high)),
        CCEaseSineInOut::create(CCFadeTo::create(duration, low)),
        nullptr
    )));
}

} // namespace

std::string normalizeRankName(std::string rank) {
    std::transform(rank.begin(), rank.end(), rank.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (rank == "NONE") return "UNRANKED";
    return rank;
}

int rankStageFor(std::string const& input) {
    auto const rank = normalizeRankName(input);
    if (rank == "RED") return 1;
    if (rank == "AQUA") return 2;
    if (rank == "BRONZE") return 3;
    if (rank == "SILVER") return 4;
    if (rank == "GOLD") return 5;
    return 0;
}

bool isKnownRankName(std::string const& input) {
    auto const rank = normalizeRankName(input);
    return rank == "UNRANKED" || rankStageFor(rank) > 0;
}

RankTierProfile rankTierProfile(std::string const& input) {
    auto const rank = normalizeRankName(input);
    if (rank == "RED") return {{244, 76, 91}, {255, 128, 91}, 1, 13, 94, 1.4f};
    if (rank == "AQUA") return {{55, 220, 234}, {91, 154, 255}, 2, 15, 104, 1.6f};
    if (rank == "BRONZE") return {{208, 137, 70}, {255, 174, 78}, 3, 17, 110, 2.5f};
    if (rank == "SILVER") return {{201, 215, 235}, {111, 195, 255}, 4, 19, 122, 2.0f};
    if (rank == "GOLD") return {{255, 207, 74}, {255, 242, 145}, 5, 24, 142, 3.6f};
    return {{145, 160, 184}, {202, 211, 224}, 0, 9, 72, 0.8f};
}

CCSprite* rankResourceSprite(std::string const& filename) {
    auto const path = rankedVisualResourcePath(filename);
    if (path.empty() || !std::filesystem::exists(path)) return nullptr;
    auto const started = std::chrono::steady_clock::now();
    auto* sprite = CCSprite::create(path.string().c_str());
    auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started
    ).count();
    TransitionTrace::get().mark(fmt::format("texture-decode:{}:{}us", filename, elapsedUs));
    return sprite;
}

void fitRankSprite(CCSprite* sprite, CCSize maxSize) {
    if (!sprite) return;
    auto const size = sprite->getContentSize();
    if (size.width <= 0.0f || size.height <= 0.0f) return;
    sprite->setScale(std::min(maxSize.width / size.width, maxSize.height / size.height));
}

void coverRankSprite(CCSprite* sprite, CCSize size) {
    if (!sprite) return;
    auto const content = sprite->getContentSize();
    if (content.width <= 0.0f || content.height <= 0.0f) return;
    auto const scale = std::max(size.width / content.width, size.height / content.height);
    sprite->setScale(scale);
}

RankBadgeAssembly createRankBadgeAssembly(std::string const& input, float badgeSize) {
    RankBadgeAssembly assembly;
    assembly.root = centeredRoot();
    auto const rank = normalizeRankName(input);

    for (auto const& spec : partsFor(rank)) {
        auto* sprite = rankResourceSprite(spec.filename);
        if (!sprite) continue;
        fitRankSprite(sprite, {badgeSize, badgeSize});
        sprite->setPosition(CCPointZero);
        assembly.root->addChild(sprite, spec.z);
        RankBadgePart part{spec.role, sprite, sprite->getScale()};
        if (spec.fragment) assembly.fragments.push_back(part);
        else if (spec.base) assembly.baseParts.push_back(part);
        else assembly.ornaments.push_back(part);
    }

    // Fail visibly but safely if a package ever omits structural layers.
    if (assembly.baseParts.empty()) {
        auto* fallback = trophy(rank, {badgeSize, badgeSize});
        fallback->setPosition(CCPointZero);
        assembly.root->addChild(fallback, 10);
        assembly.baseParts.push_back({"fallback", fallback, fallback->getScale()});
    }
    return assembly;
}

bool hasPersistentRankAmbient(std::string const& input) {
    auto const rank = normalizeRankName(input);
    return rank == "SILVER" || rank == "GOLD";
}

CCNode* createRankAmbientFx(
    std::string const& input,
    CCSize maxSize,
    RankBadgeDisplay display
) {
    auto* ambient = centeredRoot();
    auto const rank = normalizeRankName(input);
    if (!hasPersistentRankAmbient(rank)) return ambient;

    auto const longest = std::max(maxSize.width, maxSize.height);
    auto const compact = display == RankBadgeDisplay::Compact;
    auto const hero = display == RankBadgeDisplay::Hero;
    if (rank == "SILVER") {
        auto* halo = ambientSprite(
            ambient,
            "fx-silver-halo.png",
            {longest * (compact ? 1.28f : 1.65f), longest * (compact ? 1.28f : 1.65f)},
            1,
            compact ? 46 : 108
        );
        auto* orbit = ambientSprite(
            ambient,
            "fx-silver-orbit.png",
            {longest * (compact ? 1.18f : 1.46f), longest * (compact ? 1.18f : 1.46f)},
            4,
            compact ? 55 : 122
        );
        auto* sheen = ambientSprite(
            ambient,
            "fx-silver-crystal-sheen.png",
            {longest * (compact ? 1.16f : 1.36f), longest * (compact ? 1.16f : 1.36f)},
            6,
            compact ? 42 : 68
        );
        pulseOpacity(halo, compact ? 34 : 70, compact ? 62 : 132, 1.20f);
        if (orbit) {
            orbit->runAction(CCRepeatForever::create(
                CCRotateBy::create(compact ? 8.5f : 6.8f, 360.0f)
            ));
        }
        if (sheen) {
            sheen->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(1.25f, compact ? 78 : 118),
                CCFadeTo::create(1.55f, compact ? 24 : 35),
                nullptr
            )));
        }
    } else {
        auto* outer = ambientSprite(
            ambient,
            "fx-gold-halo-outer.png",
            {longest * (compact ? 1.36f : 1.82f), longest * (compact ? 1.36f : 1.82f)},
            1,
            compact ? 52 : 118
        );
        auto* rays = ambientSprite(
            ambient,
            "fx-gold-radial-rays.png",
            {longest * (compact ? 1.30f : 1.72f), longest * (compact ? 1.30f : 1.72f)},
            2,
            compact ? 44 : (hero ? 126 : 92)
        );
        auto* inner = ambientSprite(
            ambient,
            "fx-gold-halo-inner.png",
            {longest * (compact ? 1.20f : 1.48f), longest * (compact ? 1.20f : 1.48f)},
            4,
            compact ? 66 : 145
        );
        auto* aura = ambientSprite(
            ambient,
            "fx-gold-crown-aura.png",
            {longest * (compact ? 1.10f : 1.38f), longest * (compact ? 1.10f : 1.38f)},
            6,
            compact ? 52 : 132
        );
        pulseOpacity(outer, compact ? 36 : 76, compact ? 70 : 142, 1.15f);
        pulseOpacity(inner, compact ? 48 : 94, compact ? 82 : 168, 0.92f);
        pulseOpacity(aura, compact ? 36 : 74, compact ? 68 : 154, 0.78f);
        if (rays) {
            rays->runAction(CCRepeatForever::create(
                CCRotateBy::create(compact ? 17.0f : 14.0f, 360.0f)
            ));
        }
    }
    return ambient;
}

CCNode* createAnimatedRankBadge(
    std::string const& input,
    CCSize maxSize,
    RankBadgeDisplay display
) {
    auto const rank = normalizeRankName(input);
    auto const profile = rankTierProfile(rank);
    auto* root = centeredRoot();
    auto* ambient = centeredRoot();
    auto* badgeMotion = centeredRoot();
    root->addChild(ambient, 0);
    root->addChild(badgeMotion, 20);

    auto const longest = std::max(maxSize.width, maxSize.height);
    auto const compact = display == RankBadgeDisplay::Compact;
    auto const hero = display == RankBadgeDisplay::Hero;
    auto const genericExtent = longest * (compact ? 1.20f : 1.52f);

    auto* genericGlow = ambientSprite(
        ambient,
        "fx-rank-radial-glow-white.png",
        {genericExtent, genericExtent},
        0,
        compact ? 28 : static_cast<GLubyte>(rank == "GOLD" ? 96 : 62),
        &profile.color
    );
    pulseOpacity(genericGlow, compact ? 20 : 42, compact ? 42 : 86, compact ? 1.25f : 1.05f);

    if (rank == "AQUA" && !compact) {
        auto* orbit = ambientSprite(ambient, "fx-rank-broken-ring-white.png", {longest * 1.32f, longest * 1.32f}, 3, 72, &profile.color);
        if (orbit) orbit->runAction(CCRepeatForever::create(CCRotateBy::create(6.2f, 360.0f)));
    }

    auto* tierAmbient = createRankAmbientFx(rank, maxSize, display);
    ambient->addChild(tierAmbient, 10);

    auto* finalBadge = trophy(rank, maxSize);
    finalBadge->setPosition(CCPointZero);
    badgeMotion->addChild(finalBadge, 10);

    if (!compact) {
        badgeMotion->runAction(CCRepeatForever::create(CCSequence::create(
            CCSpawn::create(
                CCEaseSineInOut::create(CCMoveBy::create(1.35f, {0.0f, hero ? 2.2f : 1.4f})),
                CCEaseSineInOut::create(CCRotateBy::create(1.35f, 0.65f)),
                nullptr
            ),
            CCSpawn::create(
                CCEaseSineInOut::create(CCMoveBy::create(1.35f, {0.0f, hero ? -2.2f : -1.4f})),
                CCEaseSineInOut::create(CCRotateBy::create(1.35f, -0.65f)),
                nullptr
            ),
            nullptr
        )));
    }
    return root;
}

} // namespace corum::ranked::ui
