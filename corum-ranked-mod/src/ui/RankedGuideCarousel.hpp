#pragma once

#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "ModeLobbyMotion.hpp"
#include "RankedPatchNotesCache.hpp"
#include <Geode/Geode.hpp>
#include <optional>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace corum::ranked::ui {

// Q11 / Q12: while the mod update or the Ranked resources download, the
// screen explains how Ranked works. Slides advance every five seconds (the
// arrows step by hand); the download progress lives in the dock below.
inline constexpr float kGuideSlideSeconds = 5.0f;
inline constexpr float kGuideOutSeconds = 0.25f, kGuideInSeconds = 0.30f, kGuideInDelay = 0.10f;
inline constexpr int kGuideSlideCount = 7;
// The update screen's first card: the installed and the new version with the
// changes between them (the newest five releases), held longer.
inline constexpr float kGuidePatchSlideSeconds = 12.0f;
inline constexpr std::size_t kGuidePatchReleases = 5;
struct GuidePatchCard { std::string from, to; };

// Shared across screen rebuilds so a refresh does not restart the tour.
struct GuideCarouselState { int index = 0; float elapsed = 0.0f; std::string patchTarget; };
inline GuideCarouselState& guideCarouselState() { static GuideCarouselState state; return state; }

class RankedGuideCarousel final : public cocos2d::CCNode {
    std::vector<cocos2d::CCNode*> m_slides;
    // One segment per slide: passed slides full, the current one fills over
    // its five seconds, the rest stay empty. Shows how many pages there are.
    std::vector<cocos2d::CCLayerColor*> m_tracks;
    std::vector<cocos2d::CCLayerColor*> m_fills;
    std::optional<GuidePatchCard> m_patch;
    cocos2d::CCNode* m_patchBody = nullptr;
    bool m_patchFilled = false;
    int m_count = kGuideSlideCount;
    float m_segmentWidth = 24.0f;
    cocos2d::CCLabelBMFont* m_counter = nullptr;
    cocos2d::CCSize m_size;
    int m_from = -1, m_direction = 1;
    float m_switch = 1.0f;

    static float css(float px) { return px / 64.0f; }
    static cocos2d::CCLayerColor* box(cocos2d::CCNode* parent, float x, float y, float w, float h, cocos2d::ccColor3B color, GLubyte alpha = 255, int z = 1) {
        auto* layer = cocos2d::CCLayerColor::create({color.r, color.g, color.b, alpha}, w, h);
        layer->setPosition({x, y}); parent->addChild(layer, z); return layer;
    }
    static cocos2d::CCLabelBMFont* text(cocos2d::CCNode* parent, std::string const& value, float cssPx, cocos2d::CCPoint at,
        cocos2d::ccColor3B color, FontWeight weight, cocos2d::CCPoint anchor = {0.0f, 0.5f}, float width = 0.0f) {
        auto* label = menuLabel(value, css(cssPx), at, color, weight);
        label->setAnchorPoint(anchor);
        if (width > 0.0f) fitMenuLabelWidth(label, width, css(cssPx), css(cssPx) * 0.55f);
        parent->addChild(label, 5); return label;
    }
    static void chip(cocos2d::CCNode* parent, std::string const& value, cocos2d::CCPoint leftCenter, cocos2d::ccColor3B fill, cocos2d::ccColor3B ink) {
        auto* label = menuLabel(value, css(12.0f), {}, ink, FontWeight::Black);
        auto const w = label->getScaledContentSize().width + 10.0f, h = 11.0f;
        auto* face = box(parent, leftCenter.x, leftCenter.y - h / 2.0f, w, h, fill, 255, 4); face->setSkewX(8.0f);
        label->setPosition({leftCenter.x + w / 2.0f, leftCenter.y}); parent->addChild(label, 5);
    }

    // Left column: the leaning title and the wrapped explanation.
    void writeCopy(cocos2d::CCNode* slide, char const* titleKey, char const* titleFallback, char const* bodyKey, char const* bodyFallback) {
        auto const h = m_size.height;
        auto* title = text(slide, i18n::text(titleKey, titleFallback), 30.0f, {16.0f, h - 46.0f}, Text, FontWeight::Black, {0.0f, 0.5f}, 268.0f);
        leanLabel(title);
        auto* body = menuLabel("", css(15.0f), {16.0f, h - 64.0f}, TextMuted, FontWeight::SemiBold);
        fitWrappedMenuLabel(body, i18n::text(bodyKey, bodyFallback), 268.0f, 74.0f, css(15.0f));
        body->setAlignment(cocos2d::kCCTextAlignmentLeft); body->setAnchorPoint({0.0f, 1.0f});
        slide->addChild(body, 5);
    }

    // Right column diagrams (x 306 .. width-16).
    void modes(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, cy = m_size.height / 2.0f + 6.0f;
        for (int i = 0; i < 2; ++i) {
            auto const y = cy + (i == 0 ? 6.0f : -50.0f);
            box(s, x, y, w, 44.0f, SurfaceRaised);
            box(s, x, y + 43.0f, w, 1.0f, i == 0 ? Accent : Gold, 255, 2);
            chip(s, i == 0 ? "01" : "02", {x + 8.0f, y + 30.0f}, AccentFill, AccentInk);
            auto* name = text(s, i == 0 ? "SURVIVAL" : "SOLO RANKED", 24.0f, {x + 36.0f, y + 30.0f}, Text, FontWeight::Black, {0.0f, 0.5f}, w - 44.0f);
            leanLabel(name);
            text(s, i == 0 ? "16 - 64 · ROUNDS · CUT" : "1 VS 1 · BO3 · BAN", 13.0f, {x + 8.0f, y + 11.0f}, TextMuted, FontWeight::SemiBold, {0.0f, 0.5f}, w - 16.0f);
        }
    }
    void soloFlow(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, cy = m_size.height / 2.0f + 4.0f;
        std::array<char const*, 5> const steps {"MATCH", "BAN", "ROUND", "DM", "RESULT"};
        auto const step = w / 5.0f;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            auto const left = x + static_cast<float>(i) * step;
            auto const dm = i == 3;
            box(s, left, cy - 14.0f, step - 6.0f, 28.0f, dm ? ccColor3B{42, 38, 20} : SurfaceRaised);
            box(s, left, cy + 13.0f, step - 6.0f, 1.0f, dm ? Gold : i == 4 ? Accent : Steel, 255, 2);
            text(s, steps[i], 13.0f, {left + (step - 6.0f) / 2.0f, cy}, dm ? Gold : Text, FontWeight::Black, {0.5f, 0.5f}, step - 10.0f);
        }
        // R1 / R2 / R3 chips under the round step.
        for (int r = 0; r < 3; ++r)
            chip(s, r == 0 ? "W" : r == 1 ? "L" : "W", {x + 2.0f * step + static_cast<float>(r) * 14.0f, cy - 30.0f}, r == 1 ? Red : AccentFill, AccentInk);
        text(s, "BO3", 13.0f, {x + 2.0f * step, cy - 46.0f}, TextMuted, FontWeight::ExtraBold);
    }
    void tiers(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, base = m_size.height / 2.0f - 50.0f;
        std::array<std::pair<char const*, cocos2d::ccColor3B>, 5> const steps {{
            {"RED", {244, 76, 91}}, {"AQUA", {55, 220, 234}}, {"BRONZE", {208, 137, 70}}, {"SILVER", {201, 215, 235}}, {"GOLD", Gold}}};
        auto const step = w / 5.0f;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            auto const h = 24.0f + static_cast<float>(i) * 18.0f;
            auto const left = x + static_cast<float>(i) * step;
            box(s, left, base, step - 4.0f, h, steps[i].second, 70);
            box(s, left, base + h - 2.0f, step - 4.0f, 2.0f, steps[i].second, 255, 2);
            text(s, steps[i].first, 12.0f, {left + (step - 4.0f) / 2.0f, base + h + 8.0f}, steps[i].second, FontWeight::Black, {0.5f, 0.5f}, step - 6.0f);
            text(s, "III II I", 10.0f, {left + (step - 4.0f) / 2.0f, base + 8.0f}, Text, FontWeight::ExtraBold, {0.5f, 0.5f}, step - 8.0f);
        }
        text(s, "+ LP", 20.0f, {x + w, base + 112.0f}, Accent, FontWeight::Black, {1.0f, 0.5f});
    }
    void promotion(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, cy = m_size.height / 2.0f + 22.0f;
        chip(s, i18n::text("promotion-short", "PROMOTION"), {x, cy + 20.0f}, Gold, AccentInk);
        text(s, "SILVER I  >  GOLD", 18.0f, {x, cy - 2.0f}, Text, FontWeight::Black, {0.0f, 0.5f}, w);
        for (int i = 0; i < 3; ++i)
            chip(s, i == 0 ? "W" : i == 1 ? "W" : "-", {x + static_cast<float>(i) * 18.0f, cy - 22.0f}, i < 2 ? AccentFill : SurfaceSoft, i < 2 ? AccentInk : TextMuted);
        box(s, x, cy - 74.0f, w, 34.0f, SurfaceRaised);
        box(s, x, cy - 41.0f, w, 1.0f, Red, 255, 2);
        chip(s, i18n::text("demotion-warning", "DEMOTION WARNING"), {x + 8.0f, cy - 57.0f}, Red, AccentInk);
    }
    void placement(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, cy = m_size.height / 2.0f + 4.0f;
        std::array<char const*, 5> const grades {"A", "S", "B", "A", "?"};
        auto const step = w / 5.0f;
        for (int i = 0; i < 5; ++i) {
            auto const left = x + static_cast<float>(i) * step;
            auto const done = i < 4;
            box(s, left, cy - 30.0f, step - 5.0f, 60.0f, SurfaceRaised);
            box(s, left, cy + 29.0f, step - 5.0f, 1.0f, done ? Gold : SurfaceSoft, 255, 2);
            text(s, "S" + std::to_string(i + 1), 12.0f, {left + 6.0f, cy + 18.0f}, TextMuted, FontWeight::ExtraBold);
            text(s, grades[static_cast<std::size_t>(i)], 34.0f, {left + (step - 5.0f) / 2.0f, cy - 6.0f}, done ? Gold : TextMuted, FontWeight::Black, {0.5f, 0.5f});
        }
        text(s, "MAP 1 + MAP 2", 13.0f, {x, cy - 46.0f}, TextMuted, FontWeight::ExtraBold);
    }
    void survival(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, base = m_size.height / 2.0f - 46.0f;
        std::array<int, 5> const field {16, 12, 8, 4, 1};
        auto const step = w / 5.0f;
        for (std::size_t i = 0; i < field.size(); ++i) {
            auto const h = 8.0f + 6.0f * static_cast<float>(field[i]);
            auto const left = x + static_cast<float>(i) * step;
            auto const last = i + 1 == field.size();
            box(s, left, base, step - 6.0f, h, last ? Gold : Accent, last ? 255 : 90);
            text(s, std::to_string(field[i]), 18.0f, {left + (step - 6.0f) / 2.0f, base + h + 10.0f}, last ? Gold : Text, FontWeight::Black, {0.5f, 0.5f});
            text(s, i == 0 ? "R1" : i + 1 == field.size() ? "WIN" : i + 2 == field.size() ? "FINAL" : "R" + std::to_string(i + 1), 11.0f,
                {left + (step - 6.0f) / 2.0f, base - 8.0f}, TextMuted, FontWeight::ExtraBold, {0.5f, 0.5f});
        }
    }
    void achievements(cocos2d::CCNode* s) {
        auto const x = 306.0f, w = m_size.width - 16.0f - x, cy = m_size.height / 2.0f + 10.0f;
        box(s, x, cy + 6.0f, w, 40.0f, SurfaceRaised);
        box(s, x, cy + 45.0f, w, 1.0f, ccColor3B{201, 162, 255}, 255, 2);
        text(s, "+20 AP", 22.0f, {x + 8.0f, cy + 26.0f}, Gold, FontWeight::Black);
        text(s, "PLAYER", 15.0f, {x + 90.0f, cy + 26.0f}, Text, FontWeight::ExtraBold);
        text(s, i18n::text("title-equipped-chip", "EQUIPPED"), 11.0f, {x + w - 8.0f, cy + 26.0f}, ccColor3B{201, 162, 255}, FontWeight::Black, {1.0f, 0.5f});
        std::array<cocos2d::ccColor3B, 5> const grades {Accent, ccColor3B{55, 220, 234}, ccColor3B{181, 124, 255}, Gold, Red};
        for (std::size_t i = 0; i < grades.size(); ++i) {
            auto const left = x + static_cast<float>(i) * 40.0f;
            box(s, left, cy - 44.0f, 34.0f, 34.0f, Surface);
            box(s, left, cy - 11.0f, 34.0f, 1.5f, grades[i], 255, 2);
            auto* gem = box(s, left + 10.0f, cy - 34.0f, 14.0f, 14.0f, grades[i], 255, 3);
            gem->ignoreAnchorPointForPosition(false); gem->setAnchorPoint({0.5f, 0.5f}); gem->setPosition({left + 17.0f, cy - 27.0f}); gem->setRotation(45.0f);
        }
    }

    // Card 1 on the update screen: "v1 > v2", then each release's changes
    // (newest first) in the UI language, drawn with the system font.
    void patchSlide(cocos2d::CCNode* s) {
        auto const& patch = *m_patch;
        auto const version = [](std::string const& v) { return v.empty() ? std::string("-") : v[0] == 'v' ? v : "v" + v; };
        auto* title = text(s, fmt::format("{}  >  {}", version(patch.from), version(patch.to)), 30.0f, {16.0f, m_size.height - 46.0f}, Text, FontWeight::Black,
            {0.0f, 0.5f}, m_size.width - 32.0f);
        leanLabel(title);
        m_patchBody = cocos2d::CCNode::create(); m_patchBody->setContentSize(m_size); s->addChild(m_patchBody, 5);
        fillPatchSlide();
    }
    void fillPatchSlide() {
        if (!m_patch || !m_patchBody || m_patchFilled) return;
        auto& cache = RankedPatchNotesCache::get();
        m_patchBody->removeAllChildrenWithCleanup(true);
        if (!cache.loaded()) {
            cache.fetch();
            text(m_patchBody, cache.failed() ? i18n::text("patch-notes-unavailable", "PATCH NOTES UNAVAILABLE") : i18n::text("patch-notes-loading", "LOADING PATCH NOTES..."),
                15.0f, {16.0f, m_size.height - 72.0f}, TextMuted, FontWeight::SemiBold);
            return;
        }
        m_patchFilled = true;
        auto const korean = i18n::isKorean();
        auto const releases = patchNotesBetween(cache.notes(), m_patch->from, m_patch->to, kGuidePatchReleases);
        auto const width = m_size.width - 40.0f, floor = 26.0f;
        float y = m_size.height - 66.0f;
        if (releases.empty()) { text(m_patchBody, i18n::text("patch-notes-none", "NO NOTES FOR THIS UPDATE"), 15.0f, {16.0f, y - 6.0f}, TextMuted, FontWeight::SemiBold); return; }
        for (auto const& release : releases) {
            if (y < floor + 12.0f) break;
            auto* head = text(m_patchBody, "v" + release.version, 12.0f, {16.0f, y - 5.0f}, Accent, FontWeight::Black);
            y -= head->getScaledContentSize().height + 3.0f;
            for (auto const& line : release.lines(korean)) {
                auto* body = cocos2d::CCLabelTTF::create(("* " + line).c_str(), patchNotesFont(), 8.0f, {width, 0}, cocos2d::kCCTextAlignmentLeft);
                if (!body) continue;
                auto const h = body->getContentSize().height;
                // Whatever does not fit ends the card ("..."): the newest come first.
                if (y - h < floor) { text(m_patchBody, "...", 13.0f, {24.0f, y - 4.0f}, TextMuted, FontWeight::Black); return; }
                body->setAnchorPoint({0.0f, 1.0f}); body->setPosition({24.0f, y}); body->setColor({214, 218, 228});
                m_patchBody->addChild(body, 5); y -= h + 2.0f;
            }
            y -= 4.0f;
        }
    }
    float slideSeconds(int index) const { return m_patch && index == 0 ? kGuidePatchSlideSeconds : kGuideSlideSeconds; }

    bool init(cocos2d::CCSize size, std::optional<GuidePatchCard> patch) {
        if (!CCNode::init()) return false;
        m_patch = std::move(patch); m_count = kGuideSlideCount + (m_patch ? 1 : 0);
        m_slides.assign(static_cast<std::size_t>(m_count), nullptr);
        m_tracks.assign(static_cast<std::size_t>(m_count), nullptr); m_fills.assign(static_cast<std::size_t>(m_count), nullptr);
        m_size = size; setContentSize(size);
        setID("ranked-guide-carousel"_spr);
        // Panel (.panel.rule) and the HOW RANKED WORKS line with the counter.
        box(this, 0.0f, 0.0f, size.width, size.height, Surface, 255, 0);
        box(this, 0.0f, size.height - 1.5f, size.width, 1.5f, Accent, 255, 1);
        text(this, i18n::text("guide-title", "HOW RANKED WORKS"), 13.0f, {16.0f, size.height - 16.0f}, TextMuted, FontWeight::ExtraBold);
        m_counter = text(this, "", 13.0f, {size.width - 16.0f, size.height - 16.0f}, Accent, FontWeight::Black, {1.0f, 0.5f});
        struct Spec { char const* titleKey; char const* titleFallback; char const* bodyKey; char const* bodyFallback; void (RankedGuideCarousel::*diagram)(cocos2d::CCNode*); };
        std::array<Spec, kGuideSlideCount> const specs {{
            {"guide-modes-title", "TWO MODES", "guide-modes-body", "Survival: many players race on the same map and the bottom of each round is eliminated. Solo Ranked: one on one, best of three.", &RankedGuideCarousel::modes},
            {"guide-solo-title", "A SOLO RANKED MATCH", "guide-solo-body", "After matching, both players ban maps. Each round, the better record on the same map wins. A drawn round goes to a Death Match.", &RankedGuideCarousel::soloFlow},
            {"guide-tiers-title", "TIERS AND LP", "guide-tiers-body", "Results move your LP up and down. Tiers go RED, AQUA, BRONZE, SILVER, GOLD, each split into III, II and I.", &RankedGuideCarousel::tiers},
            {"guide-promotion-title", "PROMOTION AND DEMOTION", "guide-promotion-body", "At the next tier's line a promotion series opens: win enough games to move up. Lose during a demotion warning and you can drop.", &RankedGuideCarousel::promotion},
            {"guide-placement-title", "PLACEMENT", "guide-placement-body", "Your first tier comes from placement stages of two maps each. A stage's grade sets the next stage's map pool.", &RankedGuideCarousel::placement},
            {"guide-survival-title", "SURVIVAL ROUNDS", "guide-survival-body", "Each round, players below the cut are eliminated until one remains. The round's first place finishes them with its equipped effect.", &RankedGuideCarousel::survival},
            {"guide-achievements-title", "ACHIEVEMENTS AND TITLES", "guide-achievements-body", "Achievements give AP, and an equipped title shows with your name. Survival round results become round badges.", &RankedGuideCarousel::achievements},
        }};
        std::size_t first = 0;
        if (m_patch) {
            auto* slide = cocos2d::CCNode::create(); slide->setContentSize(size); slide->setID("ranked-guide-patch-notes"_spr); addChild(slide, 2);
            patchSlide(slide); m_slides[0] = slide; first = 1;
        }
        for (std::size_t i = 0; i < specs.size(); ++i) {
            auto* slide = cocos2d::CCNode::create();
            slide->setContentSize(size); addChild(slide, 2);
            writeCopy(slide, specs[i].titleKey, specs[i].titleFallback, specs[i].bodyKey, specs[i].bodyFallback);
            (this->*specs[i].diagram)(slide);
            m_slides[first + i] = slide;
        }
        // Segmented progress bar under the text column; the arrows sit at the bottom right.
        constexpr float kGap = 4.0f;
        auto const available = std::max(70.0f, size.width - 16.0f - 84.0f);
        m_segmentWidth = std::min(28.0f, (available - kGap * static_cast<float>(m_count - 1)) / static_cast<float>(m_count));
        for (int i = 0; i < m_count; ++i) {
            auto const x = 16.0f + static_cast<float>(i) * (m_segmentWidth + kGap);
            m_tracks[static_cast<std::size_t>(i)] = box(this, x, 12.0f, m_segmentWidth, 3.0f, SurfaceSoft, 255, 3);
            m_fills[static_cast<std::size_t>(i)] = box(this, x, 12.0f, 0.0f, 3.0f, Accent, 255, 4);
        }
        auto* menu = cocos2d::CCMenu::create(); menu->setPosition(cocos2d::CCPointZero); addChild(menu, 10);
        for (int i = 0; i < 2; ++i) {
            auto* face = slab({24.0f, 16.0f}, {}, GhostEdge, true, Ink);
            if (auto* mark = chevron(4.0f, 2.0f, Text)) { mark->setPosition({12.0f, 8.0f}); if (i == 0) mark->setScaleX(-1.0f); face->addChild(mark, 5); }
            auto* item = CCMenuItemSpriteExtra::create(face, this, i == 0 ? menu_selector(RankedGuideCarousel::onPrevious) : menu_selector(RankedGuideCarousel::onNext));
            item->setPosition({size.width - 16.0f - 12.0f - (i == 0 ? 30.0f : 0.0f), 14.0f});
            item->setID(i == 0 ? "ranked-guide-previous"_spr : "ranked-guide-next"_spr);
            menu->addChild(item);
        }
        auto& state = guideCarouselState();
        // A new update starts on its patch notes card.
        if (m_patch && state.patchTarget != m_patch->to) { state.patchTarget = m_patch->to; state.index = 0; state.elapsed = 0.0f; }
        state.index = ((state.index % m_count) + m_count) % m_count;
        arrange();
        scheduleUpdate();
        return true;
    }

    void go(int step) {
        auto& state = guideCarouselState();
        m_from = state.index; m_direction = step >= 0 ? 1 : -1;
        state.index = ((state.index + step) % m_count + m_count) % m_count;
        state.elapsed = 0.0f; m_switch = 0.0f;
    }

    // Outgoing slide fades and drifts 16 points away; the next one comes in
    // from the other side a moment later.
    void arrange() {
        auto const& state = guideCarouselState();
        auto const out = std::clamp(m_switch / kGuideOutSeconds, 0.0f, 1.0f);
        auto const in = std::clamp((m_switch - kGuideInDelay) / kGuideInSeconds, 0.0f, 1.0f);
        auto const inEase = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);
        for (int i = 0; i < m_count; ++i) {
            auto* slide = m_slides[static_cast<std::size_t>(i)];
            float opacity = 0.0f, offset = 0.0f;
            if (i == state.index) { opacity = m_from < 0 ? 1.0f : inEase; offset = m_from < 0 ? 0.0f : 16.0f * (1.0f - inEase) * static_cast<float>(m_direction); }
            else if (i == m_from) { opacity = 1.0f - out; offset = -16.0f * out * static_cast<float>(m_direction); }
            slide->setVisible(opacity > 0.01f);
            slide->setPositionX(offset);
            propagateModeOpacity(slide, static_cast<GLubyte>(std::lround(opacity * 255.0f)));
            auto const fill = i < state.index ? 1.0f : i > state.index ? 0.0f
                : std::clamp(state.elapsed / slideSeconds(i), 0.0f, 1.0f);
            m_fills[static_cast<std::size_t>(i)]->setContentSize({m_segmentWidth * fill, 3.0f});
        }
        m_counter->setString(fmt::format("{:02} / {:02}", state.index + 1, m_count).c_str());
        if (m_from >= 0 && m_switch >= kGuideInDelay + kGuideInSeconds) m_from = -1;
    }

public:
    static RankedGuideCarousel* create(cocos2d::CCSize size, std::optional<GuidePatchCard> patch = std::nullopt) {
        auto* node = new RankedGuideCarousel;
        if (node->init(size, std::move(patch))) { node->autorelease(); return node; }
        delete node; return nullptr;
    }
    void onPrevious(cocos2d::CCObject*) { go(-1); }
    void onNext(cocos2d::CCObject*) { go(1); }
    void update(float dt) override {
        auto& state = guideCarouselState();
        dt = std::min(dt, 0.1f);
        state.elapsed += dt; m_switch += dt;
        if (!m_patchFilled && RankedPatchNotesCache::get().loaded()) fillPatchSlide();
        if (state.elapsed >= slideSeconds(state.index)) go(1);
        arrange();
    }
};

} // namespace corum::ranked::ui
