#pragma once

#include "RankAssetPackManager.hpp"
#include "../domain/FontMetrics.hpp"
#include "../domain/RankTierPresentation.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

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

// Disabled custom buttons are composed of several independent color nodes;
// tinting only their menu item does not desaturate the border or the caption.
inline void grayButtonVisual(CCNode* node) {
    if (!node) return;
    auto gray = [](ccColor3B color) {
        auto const value = static_cast<GLubyte>((299 * color.r + 587 * color.g + 114 * color.b) / 1000);
        return ccc3(value, value, value);
    };
    if (auto* caption = typeinfo_cast<CCLabelBMFont*>(node)) {
        caption->setColor({176, 176, 176});
    } else if (auto* layer = typeinfo_cast<CCLayerColor*>(node)) {
        layer->setColor(gray(layer->getColor()));
    } else if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        sprite->setColor(gray(sprite->getColor()));
    }
    for (auto* child : node->getChildrenExt()) grayButtonVisual(child);
}

enum class FontWeight {
    Regular,
    Medium,
    SemiBold,
    ExtraBold,
    Black,
};

inline char const* font(FontWeight weight) {
    switch (weight) {
        case FontWeight::Medium: return "CorumSansMedium.fnt"_spr;
        case FontWeight::SemiBold: return "CorumSansSemiBold.fnt"_spr;
        case FontWeight::ExtraBold: return "CorumSansBold.fnt"_spr;
        case FontWeight::Black: return "CorumSansBlack.fnt"_spr;
        // Regular is reserved for Latin/symbol text; current UI uses the other
        // four weights for bilingual captions. See the font coverage regression.
        case FontWeight::Regular: return "CorumSans.fnt"_spr;
    }
    return "CorumSans.fnt"_spr;
}

inline char const* bodyFont() {
    return font(FontWeight::SemiBold);
}

inline char const* displayFont() {
    return font(FontWeight::Black);
}

inline char const* countdownFont() {
    return "CorumSansCountdown.fnt"_spr;
}

// Callers continue using the authored UI scales. Only Corum's larger bitmap
// fonts need normalization; native fallback fonts retain their own metrics.
inline float fontScale(CCLabelBMFont* caption, float logicalScale) {
    auto const* resource = caption ? caption->getFntFile() : nullptr;
    return corumFontScale(logicalScale, resource ? std::string_view(resource) : std::string_view{});
}

inline void setLabelScale(CCLabelBMFont* caption, float logicalScale) {
    if (caption) caption->setScale(fontScale(caption, logicalScale));
}

inline void fitLabelWidth(CCLabelBMFont* caption, float width, float maximumScale, float minimumScale) {
    if (caption) caption->limitLabelWidth(width,
        fontScale(caption, maximumScale), fontScale(caption, minimumScale));
}

inline ccColor3B tierColor(std::string tier) {
    tier=rankTierBase(tier);
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

// Player data keeps its original casing even if a generated font failed to
// load. Native gold/big fonts may substitute uppercase for missing lowercase.
inline CCLabelBMFont* playerNameLabel(std::string const& text,float scale,CCPoint position,
    ccColor3B color,FontWeight weight) {
    auto* result=CCLabelBMFont::create(text.c_str(),font(weight));
    if(!result)result=CCLabelBMFont::create(text.c_str(),"chatFont.fnt");
    setLabelScale(result,scale);result->setColor(color);result->setPosition(position);
    return result;
}
inline CCLabelBMFont* label(
    std::string const& text,
    float scale,
    CCPoint position,
    ccColor3B color,
    FontWeight weight,
    char const* resource = nullptr
) {
    auto* result = CCLabelBMFont::create(text.c_str(), resource ? resource : font(weight));
    // The generated resource font is the preferred path, but keep every screen
    // usable if a platform-specific font atlas ever fails to load.
    if (!result) {
        auto const emphasized =
            weight == FontWeight::SemiBold ||
            weight == FontWeight::ExtraBold ||
            weight == FontWeight::Black;
        result = CCLabelBMFont::create(text.c_str(), emphasized ? "goldFont.fnt" : "bigFont.fnt");
    }
    setLabelScale(result, scale);
    result->setColor(color);
    result->setPosition(position);
    return result;
}

inline CCLabelBMFont* label(
    std::string const& text,
    float scale,
    CCPoint position,
    ccColor3B color = Text,
    bool display = false
) {
    return label(
        text,
        scale,
        position,
        color,
        display ? FontWeight::Black : FontWeight::SemiBold
    );
}

// Cocos wraps decoded UTF-8 glyphs. Keep the original text intact, including
// long names without spaces, and fit the complete block into its reserved box.
inline void fitFullLabel(CCLabelBMFont* text, float width, float height, float scale) {
    if (!text || width <= 0 || height <= 0) return;
    text->setAlignment(kCCTextAlignmentCenter);
    text->setLineBreakWithoutSpace(true);
    for (int pass = 0; pass < 20; ++pass) {
        setLabelScale(text, scale);
        text->setWidth(width / text->getScale());
        auto const displayedHeight = text->getContentSize().height * text->getScale();
        if (displayedHeight <= height) return;
        scale *= std::max(0.25f, std::sqrt(height / displayedHeight) * 0.95f);
    }
}

inline void fitMenuLabelWidth(CCLabelBMFont* text, float width, float maximumScale, float minimumScale) {
    fitLabelWidth(text, width, rankedMenuTextScale(maximumScale), rankedMenuTextScale(minimumScale));
    // A readability floor must not place long translated text outside a button.
    if (text && width > 0 && text->getContentSize().width * text->getScale() > width)
        text->setScale(width / text->getContentSize().width);
}

inline void fitFullMenuLabel(CCLabelBMFont* text, float width, float height, float scale) {
    fitFullLabel(text, width, height, rankedMenuTextScale(scale));
}

// Names stay on one line. Clear a previous wrapping width before measuring,
// and allow any necessary shrink instead of pushing glyphs into the next card.
inline void fitSingleLineMenuLabel(CCLabelBMFont* text,float width,float height,float maximum) {
    if(!text || width<=0 || height<=0)return;
    text->setWidth(-1);text->setLineBreakWithoutSpace(false);
    setLabelScale(text,rankedMenuTextScale(maximum));
    auto const size=text->getContentSize();
    if(size.width>0 && size.height>0)
        text->setScale(std::min({text->getScale(),width/size.width,height/size.height}));
}

inline CCLabelBMFont* menuLabel(std::string const& text, float scale, CCPoint position,
    ccColor3B color, FontWeight weight, char const* resource = nullptr) {
    auto* result = label(text, rankedMenuTextScale(scale), position, color, weight, resource);
    // surface() draws its opaque fill at z=1 and chrome at z=3. Default addChild
    // must keep menu captions above them, including retained modal buttons.
    if (result) result->setZOrder(5);
    return result;
}

inline CCLabelBMFont* menuLabel(std::string const& text, float scale, CCPoint position,
    ccColor3B color = Text, bool display = false) {
    return menuLabel(text, scale, position, color,
        display ? FontWeight::Black : FontWeight::SemiBold);
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

    // Flat, low-noise chrome keeps the information hierarchy readable on a
    // phone. The previous double glow, side rail and four corner blocks made
    // every panel compete with the actual values displayed inside it.
    auto* shadow = CCLayerColor::create({1, 4, 12, static_cast<GLubyte>(strong ? 82 : 54)});
    shadow->setContentSize({size.width + 2.0f, size.height + 3.0f});
    shadow->setPosition({-1.0f, -2.0f});
    root->addChild(shadow, -2);

    auto* border = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 158 : 88)});
    border->setContentSize(size);
    border->setPosition(CCPointZero);
    root->addChild(border, 0);

    auto* inner = CCLayerColor::create({fill.r, fill.g, fill.b, opacity});
    inner->setContentSize({std::max(2.0f, size.width - 1.5f), std::max(2.0f, size.height - 1.5f)});
    inner->setPosition({0.75f, 0.75f});
    root->addChild(inner, 1);

    auto* highlight = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 235 : 190)});
    highlight->setContentSize({std::max(4.0f, size.width - 8.0f), strong ? 1.5f : 1.0f});
    highlight->setPosition({4.0f, size.height - 3.0f});
    root->addChild(highlight, 3);

    if (strong) {
        auto* leftRail = CCLayerColor::create({accent.r, accent.g, accent.b, 138});
        leftRail->setContentSize({1.5f, std::max(4.0f, size.height - 10.0f)});
        leftRail->setPosition({3.0f, 5.0f});
        root->addChild(leftRail, 3);
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
    fitLabelWidth(caption, size.width - 12.0f, textScale, std::max(0.10f, textScale * 0.65f));
    root->addChild(caption, 6);
    return root;
}

inline CCNode* menuPill(std::string const& text, CCSize size, CCPoint position,
    ccColor3B accent, float scale = 0.22f, ccColor3B color = Text, bool display = true) {
    return pill(text, size, position, accent, rankedMenuTextScale(scale), color, display);
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
    tier=rankTierBase(tier);
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
    auto const path = rankedVisualResourcePath(filename);
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

} // namespace corum::ranked::ui

