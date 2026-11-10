#pragma once

#include "ButtonFx.hpp"
#include "RankAssetPackManager.hpp"
#include "../domain/FontMetrics.hpp"
#include "../domain/RankTierPresentation.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace corum::ranked::ui {

using namespace geode::prelude;

// Broadcast palette: a near-black neutral ground, one lime signal colour and
// steel for everything secondary. Tier colours stay the only other hues.
inline constexpr ccColor3B Ink = {13, 14, 18};
inline constexpr ccColor3B Surface = {23, 25, 34};
inline constexpr ccColor3B SurfaceRaised = {32, 35, 46};
inline constexpr ccColor3B SurfaceSoft = {43, 47, 60};
inline constexpr ccColor3B Text = {244, 245, 247};
inline constexpr ccColor3B TextMuted = {169, 174, 189};
inline constexpr ccColor3B Accent = {200, 255, 46};
// Text and marks drawn on top of an Accent fill.
inline constexpr ccColor3B AccentInk = {13, 14, 18};
// Filled lime under an ink caption: deeper than Accent so the caption reads.
inline constexpr ccColor3B AccentFill = {166, 222, 36};
inline constexpr ccColor3B Steel = {122, 130, 152};
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

// CCLabelBMFont's own width wrapping does not reliably break this UTF-8 font
// (a long Korean warning stayed on one line far past its popup). Break lines
// explicitly at spaces, or between whole UTF-8 glyphs for an overlong word,
// then shrink until the complete block fits its reserved box.
inline void fitWrappedMenuLabel(CCLabelBMFont* text, std::string const& value, float width, float height, float scale) {
    if (!text || width <= 0 || height <= 0) return;
    text->setWidth(-1);
    text->setLineBreakWithoutSpace(false);
    text->setAlignment(kCCTextAlignmentCenter);
    auto measure = [text](std::string const& line) {
        text->setString(line.c_str());
        return text->getContentSize().width;
    };
    auto glyphs = [](std::string const& word) {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < word.size();) {
            auto const lead = static_cast<unsigned char>(word[i]);
            std::size_t n = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            n = std::min(n, word.size() - i);
            out.push_back(word.substr(i, n));
            i += n;
        }
        return out;
    };
    auto current = rankedMenuTextScale(scale);
    std::string joined = value;
    for (int pass = 0; pass < 16; ++pass) {
        setLabelScale(text, current);
        auto const applied = text->getScale();
        if (applied <= 0) return;
        auto const limit = width / applied;
        std::vector<std::string> lines;
        std::size_t paragraphStart = 0;
        while (paragraphStart <= value.size()) {
            auto paragraphEnd = value.find('\n', paragraphStart);
            if (paragraphEnd == std::string::npos) paragraphEnd = value.size();
            auto const paragraph = value.substr(paragraphStart, paragraphEnd - paragraphStart);
            std::string line;
            std::size_t wordStart = 0;
            while (wordStart <= paragraph.size()) {
                auto wordEnd = paragraph.find(' ', wordStart);
                if (wordEnd == std::string::npos) wordEnd = paragraph.size();
                auto const word = paragraph.substr(wordStart, wordEnd - wordStart);
                wordStart = wordEnd + 1;
                if (word.empty()) continue;
                auto const candidate = line.empty() ? word : line + " " + word;
                if (measure(candidate) <= limit) { line = candidate; continue; }
                if (!line.empty()) { lines.push_back(line); line.clear(); }
                if (measure(word) <= limit) { line = word; continue; }
                for (auto const& glyph : glyphs(word)) {
                    if (!line.empty() && measure(line + glyph) > limit) { lines.push_back(line); line.clear(); }
                    line += glyph;
                }
            }
            lines.push_back(line);
            paragraphStart = paragraphEnd + 1;
        }
        joined.clear();
        for (std::size_t i = 0; i < lines.size(); ++i) { if (i) joined += '\n'; joined += lines[i]; }
        text->setString(joined.c_str());
        auto const size = text->getContentSize();
        if (size.height * applied <= height && size.width * applied <= width + .5f) return;
        auto const ratio = std::min(height / std::max(1.f, size.height * applied), width / std::max(1.f, size.width * applied));
        current *= std::clamp(std::sqrt(ratio) * .95f, .5f, .95f);
    }
    // Last resort: never draw outside the box.
    auto const size = text->getContentSize();
    if (size.width > 0 && size.height > 0)
        text->setScale(std::min({text->getScale(), width / size.width, height / size.height}));
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

inline constexpr int SurfaceShadowTag = 8028;
inline constexpr int SurfaceFillTag = 8029;
inline constexpr int SurfaceBorderTag = 8030;
inline constexpr int SurfaceRuleTag = 8031;
inline constexpr int SurfaceTabTag = 8032;

// A frame is the neutral line colour pulled towards its accent: the signal
// colour stays on the rule and the tab, so a screen full of panels stays calm.
inline ccColor3B surfaceEdge(ccColor3B accent, bool strong) {
    auto const pull = strong ? 0.45f : 0.25f;
    auto const mix = [pull](GLubyte from, GLubyte to) {
        return static_cast<GLubyte>(std::lround(static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * pull));
    };
    return {mix(SurfaceSoft.r, accent.r), mix(SurfaceSoft.g, accent.g), mix(SurfaceSoft.b, accent.b)};
}

// Places a surface's chrome for `size`. surface() builds through this and the
// lobby card widens through it, so both always agree on the geometry.
inline void layoutSurface(CCNode* root, CCSize size) {
    if (!root) return;
    root->setContentSize(size);
    for (auto* child : CCArrayExt<CCNode*>(root->getChildren())) {
        auto* layer = typeinfo_cast<CCLayerColor*>(child);
        if (!layer) continue;
        switch (layer->getTag()) {
            case SurfaceShadowTag:
                layer->setContentSize(size);
                layer->setPosition({1.5f, -2.0f});
                break;
            case SurfaceBorderTag:
                layer->setContentSize(size);
                layer->setPosition(CCPointZero);
                break;
            case SurfaceFillTag:
                layer->setContentSize({std::max(2.0f, size.width - 2.0f), std::max(2.0f, size.height - 2.0f)});
                layer->setPosition({1.0f, 1.0f});
                break;
            case SurfaceRuleTag: {
                auto const rule = layer->getContentSize().height;
                layer->setContentSize({size.width, rule});
                layer->setPosition({0.0f, size.height - rule});
                break;
            }
            case SurfaceTabTag: {
                auto const tab = layer->getContentSize().height;
                layer->setContentSize({std::clamp(size.width * 0.2f, 8.0f, 36.0f), tab});
                layer->setPosition({1.3f, size.height - 2.0f - tab});
                break;
            }
            default: break;
        }
    }
}

// In-level UI (HUD, pause additions, replay and spectator overlays) keeps
// the pre-0.6 look: navy panels with a cyan frame. The broadcast restyle is
// for menus only.
inline constexpr ccColor3B HudInk = {5, 10, 22};
inline constexpr ccColor3B HudSurface = {10, 19, 36};
inline constexpr ccColor3B HudSurfaceRaised = {15, 28, 50};
inline constexpr ccColor3B HudSurfaceSoft = {21, 38, 65};
inline constexpr ccColor3B HudCyan = {64, 224, 255};
inline constexpr ccColor3B HudBlue = {77, 135, 255};

inline CCNode* hudSurface(
    CCSize size,
    CCPoint position,
    ccColor3B accent = HudCyan,
    ccColor3B fill = HudSurface,
    GLubyte opacity = 242,
    bool strong = false
) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});
    root->setPosition(position);
    auto* shadow = CCLayerColor::create({1, 4, 12, static_cast<GLubyte>(strong ? 82 : 54)});
    shadow->setContentSize({size.width + 2.0f, size.height + 3.0f});
    shadow->setPosition({-1.0f, -2.0f});
    root->addChild(shadow, -2);
    auto* border = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 158 : 88)});
    border->setContentSize(size);
    border->setTag(SurfaceBorderTag);root->addChild(border, 0);
    auto* inner = CCLayerColor::create({fill.r, fill.g, fill.b, opacity});
    inner->setContentSize({std::max(2.0f, size.width - 1.5f), std::max(2.0f, size.height - 1.5f)});
    inner->setPosition({0.75f, 0.75f});
    root->addChild(inner, 1);
    auto* highlight = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 235 : 190)});
    highlight->setContentSize({std::max(4.0f, size.width - 8.0f), strong ? 1.5f : 1.0f});
    highlight->setPosition({4.0f, size.height - 3.0f});
    highlight->setTag(SurfaceRuleTag);root->addChild(highlight, 3);
    if (strong) {
        auto* leftRail = CCLayerColor::create({accent.r, accent.g, accent.b, 138});
        leftRail->setContentSize({1.5f, std::max(4.0f, size.height - 10.0f)});
        leftRail->setPosition({3.0f, 5.0f});
        leftRail->setTag(SurfaceTabTag);root->addChild(leftRail, 3);
    }
    return root;
}

// The one panel every Corum screen is built from: a flat fill inside a thin
// tinted frame, ruled along the top. A strong surface carries a heavier rule
// with a slanted tab under its left end. No glow and no gradient.
inline CCNode* surface(
    CCSize size,
    CCPoint position,
    ccColor3B accent = Steel,
    ccColor3B fill = Surface,
    GLubyte opacity = 242,
    bool strong = false
) {
    auto* root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setPosition(position);

    auto* shadow = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(strong ? 78 : 50)});
    shadow->setTag(SurfaceShadowTag);
    root->addChild(shadow, -2);

    auto const edge = surfaceEdge(accent, strong);
    auto* border = CCLayerColor::create({edge.r, edge.g, edge.b, 255});
    border->setTag(SurfaceBorderTag);
    root->addChild(border, 0);

    auto* inner = CCLayerColor::create({fill.r, fill.g, fill.b, opacity});
    inner->setTag(SurfaceFillTag);
    root->addChild(inner, 1);

    auto* rule = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(strong ? 255 : 140)});
    rule->setContentSize({size.width, strong ? 2.0f : 1.0f});
    rule->setTag(SurfaceRuleTag);
    root->addChild(rule, 3);

    if (strong) {
        auto* tab = CCLayerColor::create({accent.r, accent.g, accent.b, 255});
        tab->setContentSize({8.0f, 3.0f});
        tab->setSkewX(40.0f);
        tab->setTag(SurfaceTabTag);
        root->addChild(tab, 3);
    }
    layoutSurface(root, size);

    // Any surface that ends up as a menu item's face answers the pointer.
    if (auto* fx = ButtonFx::create()) root->addChild(fx, 2);

    return root;
}

// Recolor only static border/highlight layers; keep fill, children and actions.
inline void setSurfaceAccent(CCNode* panel,ccColor3B color) {
    if(!panel)return;
    auto const strong=panel->getChildByTag(SurfaceTabTag)!=nullptr;
    if(auto* border=typeinfo_cast<CCLayerColor*>(panel->getChildByTag(SurfaceBorderTag)))border->setColor(surfaceEdge(color,strong));
    for(int tag:{SurfaceRuleTag,SurfaceTabTag})if(auto* layer=typeinfo_cast<CCLayerColor*>(panel->getChildByTag(tag)))layer->setColor(color);
}

// A slanted slab: the face of an action. Filled, it is the primary action in
// the signal colour (caption in AccentInk); outlined, it is a secondary one.
inline CCNode* slab(
    CCSize size,
    CCPoint position,
    ccColor3B color = AccentFill,
    bool outlined = false,
    ccColor3B fill = SurfaceRaised
) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});
    root->setPosition(position);

    auto* shadow = CCLayerColor::create({0, 0, 0, 96});
    shadow->setContentSize(size);
    shadow->setPosition({2.0f, -2.0f});
    shadow->setSkewX(SlabSkew);
    root->addChild(shadow, -2);

    auto* body = CCLayerColor::create({color.r, color.g, color.b, 255});
    body->setContentSize(size);
    body->setSkewX(SlabSkew);
    body->setTag(SurfaceBorderTag);
    root->addChild(body, 0);

    if (outlined) {
        auto* inner = CCLayerColor::create({fill.r, fill.g, fill.b, 255});
        inner->setContentSize({std::max(2.0f, size.width - 3.0f), std::max(2.0f, size.height - 3.0f)});
        inner->setPosition({1.5f, 1.5f});
        inner->setSkewX(SlabSkew);
        inner->setTag(SurfaceFillTag);
        root->addChild(inner, 1);
    }
    if (auto* fx = ButtonFx::create(ButtonFxKind::Standard, SlabSkew)) root->addChild(fx, 2);
    return root;
}

// Captions on a slab, and display titles, lean with it.
inline void leanLabel(CCNode* caption) {
    if (caption) caption->setSkewX(SlabSkew);
}

// A ">" drawn from two bars, so it fades and tints like any other layer.
inline CCNode* chevron(float arm, float thickness, ccColor3B color) {
    auto* root = CCNode::create();
    for (int i = 0; i < 2; ++i) {
        auto* bar = CCLayerColor::create({color.r, color.g, color.b, 255});
        bar->setContentSize({arm, thickness});
        bar->setPosition({-arm / 2.0f, (i ? -1.0f : 1.0f) * arm * 0.32f - thickness / 2.0f});
        bar->setRotation(i ? -40.0f : 40.0f);
        root->addChild(bar);
    }
    return root;
}

// An "x" drawn from two bars, for close buttons.
inline CCNode* cross(float arm, float thickness, ccColor3B color) {
    auto* root = CCNode::create();
    for (int i = 0; i < 2; ++i) {
        auto* bar = CCLayerColor::create({color.r, color.g, color.b, 255});
        bar->setContentSize({arm, thickness});
        bar->setPosition({-arm / 2.0f, -thickness / 2.0f});
        bar->setRotation(i ? -45.0f : 45.0f);
        root->addChild(bar);
    }
    return root;
}

// The mode start button: a large signal slab with a double chevron. It shines
// on its own while idle, grows under the pointer and bursts when pressed.
// A start button that cannot be used right now is a quiet steel slab.
inline CCNode* heroSlab(CCSize size, bool live = true) {
    auto const fill = live ? AccentFill : SurfaceSoft;
    auto const ink = live ? AccentInk : TextMuted;
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});

    CCLayerColor* pings[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        pings[i] = CCLayerColor::create({AccentFill.r, AccentFill.g, AccentFill.b, 0});
        pings[i]->setContentSize(size);
        pings[i]->setSkewX(SlabSkew);
        root->addChild(pings[i], -3 + i);
    }
    auto* body = CCNode::create();
    body->setContentSize(size);
    body->setAnchorPoint({0.5f, 0.5f});
    body->setPosition({size.width / 2.0f, size.height / 2.0f});
    root->addChild(body, 1);

    auto* shadow = CCLayerColor::create({0, 0, 0, 110});
    shadow->setContentSize(size);
    shadow->setPosition({3.0f, -3.0f});
    shadow->setSkewX(SlabSkew);
    body->addChild(shadow, -2);

    auto* face = CCLayerColor::create({fill.r, fill.g, fill.b, 255});
    face->setContentSize(size);
    face->setSkewX(SlabSkew);
    face->setTag(SurfaceBorderTag);
    body->addChild(face, 0);

    auto const arm = size.height * 0.34f;
    for (int i = 0; i < 2; ++i) {
        auto* mark = chevron(arm, std::max(3.0f, arm * 0.30f), ink);
        mark->setPosition({size.width / 2.0f + (i ? 1.0f : -1.0f) * arm * 0.42f + arm * 0.10f, size.height / 2.0f});
        body->addChild(mark, 5);
    }
    if (auto* fx = ButtonFx::create(ButtonFxKind::Hero, SlabSkew, body, pings[0], pings[1])) body->addChild(fx, 2);
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
    auto* base = CCLayerColor::create({Ink.r, Ink.g, Ink.b, 255});
    base->setContentSize(size);
    parent->addChild(base, -20);

    // Two slanted slabs give the flat ground a direction without a gradient.
    auto* wide = CCLayerColor::create({Surface.r, Surface.g, Surface.b, 150});
    wide->setContentSize({size.width * 0.34f, size.height * 1.5f});
    wide->setPosition({size.width * 0.58f, -size.height * 0.25f});
    wide->setSkewX(SlabSkew * 2.0f);
    parent->addChild(wide, -19);

    auto* thin = CCLayerColor::create({Accent.r, Accent.g, Accent.b, 14});
    thin->setContentSize({size.width * 0.05f, size.height * 1.5f});
    thin->setPosition({size.width * 0.50f, -size.height * 0.25f});
    thin->setSkewX(SlabSkew * 2.0f);
    parent->addChild(thin, -19);

    // Fine diagonal hatching across the whole ground, drawn once.
    auto* hatch = CCDrawNode::create();
    auto const lean = size.height * 0.2867f; // tan(16 degrees)
    for (float x = -lean; x < size.width; x += 14.0f)
        hatch->drawSegment({x, 0.0f}, {x + lean, size.height}, 0.3f, {1.0f, 1.0f, 1.0f, 0.03f});
    parent->addChild(hatch, -18);

    auto* horizon = CCLayerColor::create({SurfaceSoft.r, SurfaceSoft.g, SurfaceSoft.b, 255});
    horizon->setContentSize({size.width - 34.0f, 1.0f});
    horizon->setPosition({17.0f, size.height - 72.0f});
    parent->addChild(horizon, -17);
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

