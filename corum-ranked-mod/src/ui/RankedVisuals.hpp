#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>

namespace corum::ranked::ui {

using namespace geode::prelude;

inline constexpr ccColor3B Ink = {5, 10, 22};
inline constexpr ccColor3B Surface = {10, 19, 36};
inline constexpr ccColor3B SurfaceRaised = {15, 28, 50};
inline constexpr ccColor3B SurfaceSoft = {21, 38, 65};
inline constexpr ccColor3B Text = {242, 247, 255};
inline constexpr ccColor3B TextMuted = {164, 181, 205};
inline constexpr ccColor3B Cyan = {64, 224, 255};
inline constexpr ccColor3B Blue = {77, 135, 255};
inline constexpr ccColor3B Green = {90, 238, 151};
inline constexpr ccColor3B Red = {255, 91, 111};
inline constexpr ccColor3B Gold = {255, 207, 74};
inline constexpr ccColor3B Orange = {255, 145, 68};

inline char const* bodyFont() {
    return "CorumSans.fnt"_spr;
}

inline char const* displayFont() {
    return "CorumSansBold.fnt"_spr;
}

inline ccColor3B tierColor(std::string tier) {
    std::transform(tier.begin(), tier.end(), tier.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (tier == "RED") return {244, 76, 91};
    if (tier == "AQUA") return {55, 220, 234};
    if (tier == "BRONZE") return {208, 137, 70};
    if (tier == "SILVER") return {201, 215, 235};
    if (tier == "GOLD") return Gold;
    return {145, 160, 184};
}

struct Frame {
    float left = 18.0f;
    float right = 550.0f;
    float bottom = 14.0f;
    float top = 306.0f;
    float scale = 1.0f;
    float width = 532.0f;
    float height = 292.0f;
};

inline Frame contentFrame(CCSize const& size) {
    auto const scale = std::clamp(std::min(size.width / 568.0f, size.height / 320.0f), 0.78f, 1.25f);
    auto const horizontal = std::max(14.0f, 18.0f * scale);
    auto const vertical = std::max(10.0f, 14.0f * scale);
    return {
        .left = horizontal,
        .right = size.width - horizontal,
        .bottom = vertical,
        .top = size.height - vertical,
        .scale = scale,
        .width = size.width - horizontal * 2.0f,
        .height = size.height - vertical * 2.0f,
    };
}

inline CCLabelBMFont* label(
    std::string const& text,
    float scale,
    CCPoint position,
    ccColor3B color = Text,
    bool display = false
) {
    auto* result = CCLabelBMFont::create(text.c_str(), display ? displayFont() : bodyFont());
    // The generated resource font is the preferred path, but keep every screen
    // usable if a platform-specific font atlas ever fails to load.
    if (!result) {
        result = CCLabelBMFont::create(text.c_str(), display ? "goldFont.fnt" : "bigFont.fnt");
    }
    result->setScale(scale);
    result->setColor(color);
    result->setPosition(position);
    return result;
}

inline CCNode* surface(
    CCSize size,
    CCPoint position,
    ccColor3B accent = Cyan,
    ccColor3B fill = Surface,
    GLubyte opacity = 242,
    bool strong = false
) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});
    root->setPosition(position);

    auto* shadow = CCLayerColor::create({1, 4, 12, 105});
    shadow->setContentSize({size.width + 6.0f, size.height + 7.0f});
    shadow->setPosition({-3.0f, -6.0f});
    root->addChild(shadow, -2);

    auto* glow = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 42 : 22)});
    glow->setContentSize({size.width + 4.0f, size.height + 4.0f});
    glow->setPosition({-2.0f, -2.0f});
    root->addChild(glow, -1);

    auto* border = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 180 : 105)});
    border->setContentSize(size);
    border->setPosition(CCPointZero);
    root->addChild(border, 0);

    auto* inner = CCLayerColor::create({fill.r, fill.g, fill.b, opacity});
    inner->setContentSize({std::max(2.0f, size.width - 2.0f), std::max(2.0f, size.height - 2.0f)});
    inner->setPosition({1.0f, 1.0f});
    root->addChild(inner, 1);

    auto* highlight = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 235 : 190)});
    highlight->setContentSize({std::max(4.0f, size.width - 18.0f), strong ? 2.0f : 1.0f});
    highlight->setPosition({9.0f, size.height - 4.0f});
    root->addChild(highlight, 3);

    auto* leftRail = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 175 : 90)});
    leftRail->setContentSize({2.0f, std::max(4.0f, size.height - 18.0f)});
    leftRail->setPosition({4.0f, 9.0f});
    root->addChild(leftRail, 3);

    for (auto const& point : {
        CCPoint{3.0f, 3.0f},
        CCPoint{size.width - 7.0f, 3.0f},
        CCPoint{3.0f, size.height - 7.0f},
        CCPoint{size.width - 7.0f, size.height - 7.0f},
    }) {
        auto* corner = CCLayerColor::create({accent.r, accent.g, accent.b, 185});
        corner->setContentSize({4.0f, 4.0f});
        corner->setPosition(point);
        root->addChild(corner, 4);
    }

    return root;
}

inline CCNode* pill(
    std::string const& text,
    CCSize size,
    CCPoint position,
    ccColor3B accent,
    float textScale = 0.22f,
    ccColor3B textColor = Text,
    bool display = true
) {
    auto* root = surface(size, position, accent, SurfaceRaised, 246, false);
    auto* caption = label(text, textScale, {size.width / 2.0f, size.height / 2.0f}, textColor, display);
    caption->limitLabelWidth(size.width - 12.0f, textScale, std::max(0.10f, textScale * 0.65f));
    root->addChild(caption, 6);
    return root;
}

inline void addBackdrop(CCNode* parent, CCSize const& size) {
    auto* base = CCLayerColor::create({4, 9, 22, 255});
    base->setContentSize(size);
    parent->addChild(base, -20);

    if (auto* gradient = CCSprite::create("GJ_gradientBG.png")) {
        gradient->setAnchorPoint({0.5f, 0.5f});
        gradient->setPosition({size.width / 2.0f, size.height / 2.0f});
        auto const content = gradient->getContentSize();
        if (content.width > 0.0f) gradient->setScaleX(size.width / content.width);
        if (content.height > 0.0f) gradient->setScaleY(size.height / content.height);
        gradient->setColor({9, 57, 125});
        gradient->setOpacity(155);
        parent->addChild(gradient, -19);
    }

    auto* topGlow = CCLayerColor::create({28, 111, 210, 42});
    topGlow->setContentSize({size.width, size.height * 0.42f});
    topGlow->setPosition({0.0f, size.height * 0.58f});
    parent->addChild(topGlow, -18);

    auto* horizon = CCLayerColor::create({Cyan.r, Cyan.g, Cyan.b, 45});
    horizon->setContentSize({size.width - 34.0f, 1.0f});
    horizon->setPosition({17.0f, size.height - 72.0f});
    parent->addChild(horizon, -17);

    auto* vignette = CCLayerColor::create({0, 2, 10, 78});
    vignette->setContentSize(size);
    parent->addChild(vignette, -16);
}

inline std::string trophyFilename(std::string tier) {
    std::transform(tier.begin(), tier.end(), tier.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (tier != "red" && tier != "aqua" && tier != "bronze" && tier != "silver" && tier != "gold") {
        tier = "unranked";
    }
    return "corum-trophy-" + tier + ".png";
}

inline CCSprite* trophy(std::string const& tier, CCSize maxSize) {
    auto const filename = trophyFilename(tier);
    auto const path = Mod::get()->getResourcesDir() / filename;
    CCSprite* result = nullptr;
    if (std::filesystem::exists(path)) result = CCSprite::create(path.string().c_str());
    if (!result) {
        result = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        result->setColor(tierColor(tier));
    }
    auto const size = result->getContentSize();
    if (size.width > 0.0f && size.height > 0.0f) {
        result->setScale(std::min(maxSize.width / size.width, maxSize.height / size.height));
    }
    return result;
}

inline void applyFireTexture(CCParticleSystemQuad* particle, bool perfect) {
    if (!particle) return;
    auto const filename = perfect ? "corum-fire-perfect.png" : "corum-fire-promotion.png";
    auto const path = Mod::get()->getResourcesDir() / filename;
    if (!std::filesystem::exists(path)) return;
    auto* texture = CCTextureCache::sharedTextureCache()->addImage(path.string().c_str(), false);
    if (texture) particle->setTexture(texture);
}

} // namespace corum::ranked::ui
