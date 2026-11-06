#include "HudEditorPopup.hpp"
#include "HudPlacementGroup.hpp"
#include "PlayerIcon.hpp"
#include "RankedHudLabel.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "../domain/CompetitiveHudLayout.hpp"
#include "../domain/HudPresentation.hpp"
#include "../domain/SurvivalHudPresentation.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <tuple>
#include <utility>

using namespace geode::prelude;

namespace corum::ranked::ui {

// Routes the editor's drags ahead of FLAlertLayer, but only for touches that
// land on a HUD element or the toolbar chrome. Toolbar buttons fall through to
// the popup's own modal menu.
class HudEditorInput final : public CCLayer {
    WeakRef<HudEditorPopup> m_owner;
    int m_touch = -1;
public:
    static HudEditorInput* create(HudEditorPopup* owner) {
        auto* input = new HudEditorInput;
        if (!input->init()) { delete input; return nullptr; }
        input->m_owner = WeakRef(owner);
        input->setTouchEnabled(true);
        input->autorelease();
        return input;
    }
    void registerWithTouchDispatcher() override {
        CCDirector::sharedDirector()->getTouchDispatcher()->addPrioTargetedDelegate(this, -65000, true);
    }
    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (m_touch != -1 || !isVisible()) return false;
        auto owner = m_owner.lock();
        if (!owner || !owner->beginTouch(touch->getLocation())) return false;
        m_touch = touch->getID();
        return true;
    }
    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (touch->getID() != m_touch) return;
        if (auto owner = m_owner.lock()) owner->moveTouch(touch->getLocation());
    }
    void ccTouchEnded(CCTouch* touch, CCEvent*) override {
        if (touch->getID() != m_touch) return;
        m_touch = -1;
        if (auto owner = m_owner.lock()) owner->endTouch();
    }
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override { ccTouchEnded(touch, event); }
    void onExit() override {
        if (m_touch != -1) if (auto owner = m_owner.lock()) owner->endTouch();
        m_touch = -1;
        CCLayer::onExit();
    }
};

namespace {
constexpr ccColor3B kPendingCheck = {112, 128, 151};

std::string elementName(HudMode mode, std::string_view id) {
    if (auto const* spec = hudElement(mode, id)) return i18n::text(std::string(spec->labelKey), std::string(spec->fallback));
    return std::string(id);
}

void includeBox(CCNode* node, CCNode* space, CCNode* root, CCRect& box, bool& any) {
    if (!node || !node->isVisible()) return;
    auto const size = node->getContentSize();
    if (node != root && size.width > 0.0f && size.height > 0.0f) {
        for (auto corner : {CCPoint{0, 0}, CCPoint{size.width, 0}, CCPoint{0, size.height}, CCPoint{size.width, size.height}}) {
            auto const point = space->convertToNodeSpace(node->convertToWorldSpace(corner));
            if (!any) { box = CCRect{point.x, point.y, 0, 0}; any = true; continue; }
            auto const minX = std::min(box.getMinX(), point.x), minY = std::min(box.getMinY(), point.y);
            auto const maxX = std::max(box.getMaxX(), point.x), maxY = std::max(box.getMaxY(), point.y);
            box = CCRect{minX, minY, maxX - minX, maxY - minY};
        }
    }
    for (auto* child : node->getChildrenExt()) includeBox(child, space, root, box, any);
}

void drawBox(CCDrawNode* draw, CCRect const& box, ccColor4F fill, float border, ccColor4F stroke) {
    CCPoint points[] = {
        {box.getMinX(), box.getMinY()}, {box.getMaxX(), box.getMinY()},
        {box.getMaxX(), box.getMaxY()}, {box.getMinX(), box.getMaxY()},
    };
    draw->drawPolygon(points, 4, fill, border, stroke);
}

// CCDrawNode blends premultiplied (ONE, ONE_MINUS_SRC_ALPHA), so a straight
// {1,1,1,0.035} line drew almost fully white. The editor's colours are
// straight alpha: draw them with the matching blend function.
CCDrawNode* straightDraw() {
    auto* draw = CCDrawNode::create();
    draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    return draw;
}

// Selection: four corner brackets instead of a filled box over the label.
void drawCorners(CCDrawNode* draw, CCRect const& box, ccColor4F color) {
    auto const arm = std::min(7.0f, std::max(3.0f, std::min(box.size.width, box.size.height) * 0.45f));
    auto const x0 = box.getMinX() - 2.0f, x1 = box.getMaxX() + 2.0f, y0 = box.getMinY() - 2.0f, y1 = box.getMaxY() + 2.0f;
    for (auto [x, y, sx, sy] : {std::tuple{x0, y0, 1.0f, 1.0f}, std::tuple{x1, y0, -1.0f, 1.0f},
                                std::tuple{x0, y1, 1.0f, -1.0f}, std::tuple{x1, y1, -1.0f, -1.0f}}) {
        draw->drawSegment({x, y}, {x + sx * arm, y}, 0.75f, color);
        draw->drawSegment({x, y}, {x, y + sy * arm}, 0.75f, color);
    }
}

CCNodeRGBA* fadingNode() {
    auto* node = CCNodeRGBA::create();
    node->setCascadeOpacityEnabled(true);
    node->setCascadeColorEnabled(false);
    return node;
}
} // namespace

HudEditorPopup* HudEditorPopup::create(HudMode mode) {
    auto* popup = new HudEditorPopup;
    if (popup->init(mode)) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool HudEditorPopup::init(HudMode mode) {
    m_size = CCDirector::sharedDirector()->getWinSize();
    if (!Popup::init(m_size.width, m_size.height)) return false;
    m_noElasticity = true;
    m_mode = mode;
    m_saved = loadHudLayout();
    m_working = m_saved.placements(mode);
    setID("ranked-hud-editor"_spr);
    m_bgSprite->setVisible(false);
    m_closeBtn->setVisible(false);
    m_mainLayer->setLayout(nullptr);
    m_canvas = CCNode::create();
    for (auto* node : {m_canvas, static_cast<CCNode*>(m_buttonMenu)}) {
        node->ignoreAnchorPointForPosition(false);
        node->setAnchorPoint({0.5f, 0.5f});
        node->setContentSize(m_size);
        node->setPosition({m_size.width / 2.0f, m_size.height / 2.0f});
        node->setScale(1.0f);
    }
    m_mainLayer->addChild(m_canvas, 1);
    m_buttonMenu->setZOrder(30);
    buildBackground();
    if (mode == HudMode::Solo) buildSoloPreview(); else buildSurvivalPreview();
    m_outlines = straightDraw();
    m_canvas->addChild(m_outlines, 40);
    m_tag = label("", 0.22f, {}, Text, FontWeight::ExtraBold);
    m_canvas->addChild(m_tag, 41);
    m_selected = std::string(hudElements(mode).front().id);
    buildToolbar();
    if (auto* input = HudEditorInput::create(this)) m_mainLayer->addChild(input, 90);
    refresh();
    return true;
}

void HudEditorPopup::onExit() {
    m_drag = Drag::None;
    setDragging(false);
    Popup::onExit();
}

void HudEditorPopup::keyBackClicked() {
    onCancel(nullptr);
}

CCNodeRGBA* HudEditorPopup::group(std::string_view id) {
    if (auto found = m_groups.find(id); found != m_groups.end()) return found->second;
    auto* node = fadingNode();
    node->setContentSize(m_size);
    node->setID(std::string("hud-editor-group"_spr) + "-" + std::string(id));
    m_canvas->addChild(node, id == "cutline" ? 12 : 10);
    m_groups.emplace(std::string(id), node);
    return node;
}

void HudEditorPopup::buildBackground() {
    auto const w = m_size.width, h = m_size.height;
    auto* sky = CCLayerColor::create({18, 38, 86, 255});
    sky->setContentSize(m_size);
    m_canvas->addChild(sky, -10);
    if (auto* gradient = CCSprite::create("GJ_gradientBG.png")) {
        auto const content = gradient->getContentSize();
        gradient->setPosition({w / 2.0f, h / 2.0f});
        if (content.width > 0.0f) gradient->setScaleX(w / content.width);
        if (content.height > 0.0f) gradient->setScaleY(h / content.height);
        gradient->setColor({40, 92, 190});
        gradient->setOpacity(150);
        m_canvas->addChild(gradient, -9);
    }
    // A plain ground band stands in for the level; the HUD is always drawn
    // over gameplay, so the preview only needs comparable contrast.
    auto* ground = CCLayerColor::create({10, 22, 54, 235});
    ground->setContentSize({w, 64.0f});
    m_canvas->addChild(ground, -8);
    // Grid and centre guides appear only while an element is dragged.
    m_gridLines = straightDraw();
    for (float x = 16.0f; x < w; x += 16.0f) m_gridLines->drawSegment({x, 0}, {x, h}, 0.25f, {1, 1, 1, 0.06f});
    for (float y = 16.0f; y < h; y += 16.0f) m_gridLines->drawSegment({0, y}, {w, y}, 0.25f, {1, 1, 1, 0.06f});
    m_gridLines->drawSegment({w / 2.0f, 0}, {w / 2.0f, h}, 0.35f, {0.25f, 0.88f, 1, 0.35f});
    m_gridLines->drawSegment({0, h / 2.0f}, {w, h / 2.0f}, 0.35f, {0.25f, 0.88f, 1, 0.35f});
    m_gridLines->setVisible(false);
    m_canvas->addChild(m_gridLines, -7);
    auto* grid = straightDraw();
    grid->drawSegment({0, 64.0f}, {w, 64.0f}, 0.5f, {0.25f, 0.88f, 1, 0.18f});
    // Native progress bar and pause button: shown for reference, not editable,
    // and kept dim so they never read as part of the editable HUD.
    drawBox(grid, {w / 2.0f - 100.0f, h - 12.0f, 200.0f, 6.0f}, {1, 1, 1, 0.08f}, 0.5f, {1, 1, 1, 0.22f});
    drawBox(grid, {w / 2.0f - 100.0f, h - 12.0f, 112.0f, 6.0f}, {0.35f, 0.93f, 0.59f, 0.32f}, 0.0f, {0, 0, 0, 0});
    m_canvas->addChild(grid, -7);
    if (auto* pause = CCSprite::createWithSpriteFrameName("GJ_pauseBtn_001.png")) {
        pause->setScale(0.6f);
        pause->setOpacity(110);
        pause->setPosition({w - 18.0f, h - 18.0f});
        m_canvas->addChild(pause, -6);
    }
}

// Mirrors PlayLayer::addRankedHud (fonts, scales, anchors and coordinates),
// one group per customizable label.
void HudEditorPopup::buildSoloPreview() {
    auto const layout = layoutHud(m_size.width, m_size.height);
    auto const top = layout.topY, left = layout.topLeftX + 8.0f, right = layout.topRightX - 8.0f;
    auto const w = m_size.width;
    auto add = [](CCNode* parent, std::string const& text, FontWeight weight, float scale, CCPoint anchor, CCPoint at, ccColor3B color) {
        return rankedHudLabel(parent, text, weight, scale, anchor, at, color);
    };
    add(group("fps"), "FPS : 60", FontWeight::SemiBold, 0.51f, {0, 1}, {7.0f, top - 0.5f}, Text);

    add(group("own-name"), i18n::text("you", "YOU"), FontWeight::SemiBold, 0.51f, {0, 1}, {left, top - 4.0f}, Cyan).fit(96.0f, 0.51f, 0.21f);
    add(group("own-caption"), i18n::text("score", "SCORE"), FontWeight::SemiBold, 0.51f, {0, 1}, {left, top - 22.0f}, Text);
    add(group("own-score"), "182.0", FontWeight::Black, 0.714f, {0, 1}, {left + 82.0f, top - 20.0f}, Text).fit(76.0f, 0.714f, 0.26f);
    // Death Match attempt counters exactly as GameplayUx draws them.
    add(group("own-attempt"), std::string(i18n::literal("inline.attempts-left")) + "2", FontWeight::SemiBold, 0.34f, {0, 1}, {left, top - 53.0f}, Text);
    add(group("opponent-name"), i18n::text("opponent", "OPPONENT"), FontWeight::SemiBold, 0.51f, {1, 1}, {right, top - 4.0f}, Red).fit(96.0f, 0.51f, 0.21f);
    add(group("opponent-caption"), i18n::text("score", "SCORE"), FontWeight::SemiBold, 0.51f, {1, 1}, {right - 88.0f, top - 22.0f}, Text);
    add(group("opponent-score"), "140.0", FontWeight::Black, 0.714f, {1, 1}, {right, top - 20.0f}, Text).fit(76.0f, 0.714f, 0.26f);
    add(group("opponent-attempt"), std::string(i18n::literal("inline.opp-attempts-left")) + "1", FontWeight::SemiBold, 0.34f, {1, 1}, {right, top - 53.0f}, Gold);
    for (int index = 0; index < 3; ++index) {
        for (auto [parent, x, done] : {std::tuple{static_cast<CCNode*>(group("own-checks")), left + 8.0f + 17.0f * index, index < 2},
                                       std::tuple{static_cast<CCNode*>(group("opponent-checks")), right - 42.0f + 17.0f * index, index < 1}}) {
            if (auto* check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png")) {
                check->setScale(0.30f);
                check->setPosition({x, top - 36.0f});
                check->setColor(done ? Green : kPendingCheck);
                check->setOpacity(220);
                parent->addChild(check, 3);
            }
        }
    }
    add(group("round"), fmt::format("{} 2", i18n::text("round", "ROUND")), FontWeight::SemiBold, 0.527f, {0.5f, 1}, {w / 2.0f, top - 1.0f}, Gold);
    // The live score difference rides in the timer group next to the clock.
    auto clock = add(group("timer"), "02:39", FontWeight::Black, CompetitiveClockScale, {0.5f, 1}, {w / 2.0f, top - 18.0f}, Text);
    auto delta = add(group("timer"), "+42", FontWeight::ExtraBold, CompetitiveClockScale, {0.5f, 1}, {w / 2.0f + 34.0f, top - 18.0f}, Green);
    auto const clockPair = competitiveClockPair(w / 2.0f,
        clock.foreground->getContentSize().width * clock.foreground->getScale(),
        delta.foreground->getContentSize().width * delta.foreground->getScale());
    clock.position({clockPair.clockX, top - 18.0f});
    delta.position({clockPair.deltaX, top - 18.0f});
    add(group("state"), "MATCH POINT", FontWeight::SemiBold, 0.442f, {0.5f, 1}, {w / 2.0f, top - 47.0f}, Gold);

    auto caption = add(group("qualifying-caption"), "Qualifying", FontWeight::SemiBold, 0.51f, {0, 0},
        {layout.bottomLeftX + 4.0f, layout.bottomY + 4.0f}, Text);
    add(group("qualifying-value"), "20%", FontWeight::Black, 0.714f, {0, 0},
        {caption.foreground->getPositionX() + caption.foreground->getScaledContentSize().width + 10.0f, layout.bottomY + 1.0f}, Text);

    auto* progress = fadingNode();
    progress->setPosition({layout.bottomRightX - 42.0f, layout.bottomY + 14.0f});
    if (auto* icon = PlayerIcon::createLocal()) {
        icon->setPosition({-31.0f, 0.0f});
        icon->setScale(0.50f);
        progress->addChild(icon, 3);
    }
    add(progress, "47%", FontWeight::Black, 0.44f, {0.5f, 0.5f}, {0, 0}, Text);
    group("progress")->addChild(progress, 5);

    auto* feed = group("feed");
    // The death log at its maximum of three rows (oldest at the bottom).
    std::array<std::pair<std::string, ccColor3B>, 3> rows {{
        {fmt::format("{} 38%", i18n::text("opponent", "OPPONENT")), Green},
        {fmt::format("{} 21%", i18n::text("you", "YOU")), Red},
        {fmt::format("{} 64%", i18n::text("opponent", "OPPONENT")), Green},
    }};
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto* row = fadingNode();
        row->setPosition({layout.bottomLeftX + 4.0f, layout.bottomY + 31.0f + static_cast<float>(index) * 16.0f});
        add(row, rows[index].first, FontWeight::Black, 0.65f, {0, 0}, {0, 0}, rows[index].second)
            .fit(std::min(220.0f, w * 0.46f), 0.65f, 0.40f);
        feed->addChild(row, 5);
    }
}

// Mirrors SurvivalHud::init/present with a representative 9-player round.
void HudEditorPopup::buildSurvivalPreview() {
    auto const w = m_size.width, h = m_size.height;
    auto const topY = layoutHud(w, h).topY;
    auto add = [](CCNode* parent, std::string const& text, FontWeight weight, float scale, CCPoint anchor, CCPoint at, ccColor3B color) {
        return rankedHudLabel(parent, text, weight, scale, anchor, at, color);
    };
    auto fps = add(group("fps"), "FPS : 60", FontWeight::SemiBold, 0.51f, {0, 1}, {7.0f, topY - 0.5f}, Text);
    auto const fpsBox = fps.foreground->boundingBox();
    add(group("aux"), "01:12", FontWeight::SemiBold, 0.38f, {0, 1}, {fpsBox.getMinX(), fpsBox.getMinY() - 4.0f}, TextMuted);

    auto tint = [](int rank, bool passing) { auto c = survivalRankColor(rank, passing); return ccc3(c.r, c.g, c.b); };
    auto* standings = group("standings");
    constexpr int rows = 7, cutoffAfter = 5, ownRank = 9;
    auto const gap = std::clamp((h * 0.60f) / static_cast<float>(rows), 12.0f, 20.0f);
    auto const scale = std::min(0.36f, gap / 50.0f), width = std::min(168.0f, w * 0.36f);
    auto const top = std::min(topY - 28.0f, h);
    float textWidth = 0.0f;
    for (int index = 0; index < rows; ++index) {
        auto const own = index + 1 == rows;
        auto const rank = own ? ownRank : index + 1;
        auto const passing = rank <= cutoffAfter;
        auto* row = fadingNode();
        row->setPosition({8.0f, top - static_cast<float>(index) * gap - (own ? 7.0f : 0.0f)});
        auto const color = tint(rank, passing);
        auto rankLabel = add(row, survivalOrdinal(rank), FontWeight::Black, scale, {0, 1}, {0, -1}, color);
        auto name = rankedHudPlayerName(row, own ? i18n::text("you", "YOU") : fmt::format("PLAYER {}", rank), FontWeight::Black, scale, {0, 1}, {0, -1}, color);
        auto score = add(row, fmt::format("{}%", 72 - rank * 5), FontWeight::Black, scale, {0, 1}, {0, -1}, color);
        rankLabel.fit(31.0f, scale, scale * 0.8f);
        score.fit(42.0f, scale, scale * 0.8f);
        auto const rankWidth = rankLabel.foreground->getContentSize().width * rankLabel.foreground->getScale();
        auto const scoreWidth = score.foreground->getContentSize().width * score.foreground->getScale();
        name.fit(std::max(32.0f, width - rankWidth - scoreWidth - 8.0f), scale, scale * 0.7f);
        auto const nameWidth = name.foreground->getContentSize().width * name.foreground->getScale();
        textWidth = std::max(textWidth, rankWidth + nameWidth + scoreWidth + 8.0f);
        name.position({rankWidth + 4.0f, -1.0f});
        score.position({rankWidth + 4.0f + nameWidth + 4.0f, -1.0f});
        standings->addChild(row, 5);
    }
    auto* cutoff = straightDraw();
    auto const y = top - static_cast<float>(cutoffAfter) * gap + gap * 0.15f;
    cutoff->drawSegment({8.0f, y}, {8.0f + textWidth + 3.0f, y}, 0.65f, {1, 0.32f, 0.41f, 0.95f});
    standings->addChild(cutoff, 6);

    add(group("round"), fmt::format("{} 3", i18n::text("round", "ROUND")), FontWeight::SemiBold, 0.527f, {0.5f, 1}, {w / 2.0f, topY - 5.0f}, Gold);
    auto clock = add(group("timer"), "02:39", FontWeight::Black, CompetitiveClockScale, {0.5f, 1}, {w / 2.0f, topY - 18.0f}, Text);
    auto delta = add(group("difference"), "-4", FontWeight::ExtraBold, CompetitiveClockScale, {0.5f, 1}, {w / 2.0f + 34.0f, topY - 18.0f}, tint(ownRank, false));
    clock.fit(w * 0.32f, CompetitiveClockScale, CompetitiveClockScale * 0.7f);
    delta.fit(w * 0.25f, CompetitiveClockScale, CompetitiveClockScale * 0.7f);
    auto const pair = competitiveClockPair(w / 2.0f,
        clock.foreground->getContentSize().width * clock.foreground->getScale(),
        delta.foreground->getContentSize().width * delta.foreground->getScale());
    clock.position({pair.clockX, topY - 18.0f});
    delta.position({pair.deltaX, topY - 18.0f});
    add(group("rank"), survivalOrdinal(ownRank), FontWeight::Black, 1.68f, {0, 0}, {12.0f, 12.0f}, tint(ownRank, false));
    add(group("waiting"), i18n::text("survival-attempts-finished", "ATTEMPTS COMPLETE · WAITING FOR RESULTS"),
        FontWeight::SemiBold, 0.30f, {0.5f, 0}, {w / 2.0f, 14.0f}, TextMuted);

    // Cutoff (red) and leader (gold) markers on the reference progress bar.
    auto* markers = group("cutline");
    for (auto [x, color] : {std::pair{w / 2.0f - 100.0f + 200.0f * 0.62f, ccColor3B{255, 82, 105}},
                            std::pair{w / 2.0f - 100.0f + 200.0f * 0.81f, ccColor3B{255, 212, 79}}}) {
        auto* marker = CCLayerColor::create({color.r, color.g, color.b, 255});
        marker->setContentSize({2.0f, 12.0f});
        marker->setPosition({x - 1.0f, h - 15.0f});
        markers->addChild(marker, 2);
    }
}

// An unavailable button gets a dark veil over its whole face (CCLayerColor
// chrome does not reliably follow cascaded opacity, so dimming alone left the
// HIDE button looking unchanged on the timer).
static void markUnavailable(CCMenuItemSpriteExtra* item, bool unavailable) {
    if (!item) return;
    constexpr int tag = 9301;
    auto* veil = static_cast<CCLayerColor*>(item->getChildByTag(tag));
    if (!veil && unavailable) {
        veil = CCLayerColor::create({4, 8, 18, 190});
        veil->setContentSize(item->getContentSize());
        veil->setTag(tag);
        item->addChild(veil, 100);
    }
    if (veil) veil->setVisible(unavailable);
}

CCMenuItemSpriteExtra* HudEditorPopup::toolbarButton(std::string const& text, CCSize size, CCPoint offset,
    SEL_MenuHandler selector, ccColor3B accent, CCLabelBMFont** caption, int tag) {
    auto* face = surface(size, {}, accent, SurfaceRaised, 250, true);
    auto* title = menuLabel(text, 0.26f, {size.width / 2.0f, size.height / 2.0f}, Text, FontWeight::ExtraBold);
    fitFullMenuLabel(title, size.width - 8.0f, size.height - 6.0f, 0.26f);
    face->addChild(title, 5);
    if (caption) *caption = title;
    auto* item = CCMenuItemSpriteExtra::create(face, this, selector);
    item->setTag(tag);
    m_buttonMenu->addChild(item);
    m_toolbarItems.push_back({item, offset});
    return item;
}

// 1 px nudges. GJ_arrow_01 points left; rotation is clockwise.
CCMenuItemSpriteExtra* HudEditorPopup::arrowButton(float rotation, CCPoint offset, int tag) {
    CCSize const size {22.0f, 22.0f};
    auto* face = surface(size, {}, Blue, SurfaceRaised, 250, true);
    if (auto* arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        auto const content = arrow->getContentSize();
        auto const fit = std::max(content.width, content.height);
        arrow->setScale(fit > 0.0f ? 14.0f / fit : 0.3f);
        arrow->setRotation(rotation);
        arrow->setPosition({size.width / 2.0f, size.height / 2.0f});
        face->addChild(arrow, 5);
    }
    auto* item = CCMenuItemSpriteExtra::create(face, this, menu_selector(HudEditorPopup::onNudge));
    item->setTag(tag);
    m_buttonMenu->addChild(item);
    m_toolbarItems.push_back({item, offset});
    m_nudgeButtons.push_back(item);
    return item;
}

CCLabelBMFont* HudEditorPopup::toolbarCaption(std::string const& text, CCPoint offset, float width, ccColor3B color, float scale) {
    auto const half = CCPoint{m_toolbarSize.width / 2.0f, m_toolbarSize.height / 2.0f};
    auto* caption = menuLabel(text, scale, half + offset, color, FontWeight::ExtraBold);
    fitMenuLabelWidth(caption, width, scale, scale * 0.6f);
    m_toolbar->addChild(caption, 5);
    return caption;
}

// Bottom dock (top when the selected element sits low), two rows like the
// approved design: element / size / transparency / 1 px moves, then
// hide / grid / presets / reset ... list / cancel / save.
void HudEditorPopup::buildToolbar() {
    auto const dockW = std::min(m_size.width - 16.0f, std::max(420.0f, m_size.width * 0.80f));
    m_toolbarSize = CCSize{dockW, 58.0f};
    m_toolbar = surface(m_toolbarSize, {}, Cyan, Surface, 242, true);
    m_toolbar->setID("hud-editor-toolbar"_spr);
    m_canvas->addChild(m_toolbar, 50);
    auto const L = -dockW / 2.0f, R = dockW / 2.0f;
    constexpr float top = 13.0f, bottom = -13.0f;
    CCSize const square {20.0f, 20.0f};

    toolbarButton("<", square, {L + 16.0f, top}, menu_selector(HudEditorPopup::onPrevious), Blue);
    m_elementName = toolbarCaption("", {L + 62.0f, top + 4.0f}, 64.0f, Text, 0.24f);
    m_indexLabel = toolbarCaption("", {L + 62.0f, top - 6.0f}, 64.0f, TextMuted, 0.15f);
    toolbarButton(">", square, {L + 108.0f, top}, menu_selector(HudEditorPopup::onNext), Blue);

    toolbarCaption(i18n::text("hud-size", "SIZE"), {L + 136.0f, top}, 26.0f, TextMuted, 0.18f);
    m_smallerButton = toolbarButton("-", square, {L + 160.0f, top}, menu_selector(HudEditorPopup::onSmaller), Blue);
    m_scaleLabel = toolbarCaption("", {L + 186.0f, top}, 30.0f, Gold, 0.24f);
    m_largerButton = toolbarButton("+", square, {L + 212.0f, top}, menu_selector(HudEditorPopup::onLarger), Blue);

    toolbarCaption(i18n::text("hud-transparency", "TRANSPARENCY"), {L + 244.0f, top}, 34.0f, TextMuted, 0.18f);
    m_opaqueButton = toolbarButton("-", square, {L + 274.0f, top}, menu_selector(HudEditorPopup::onLessTransparent), Blue);
    m_transparencyLabel = toolbarCaption("", {L + 300.0f, top}, 30.0f, Gold, 0.24f);
    m_transparentButton = toolbarButton("+", square, {L + 326.0f, top}, menu_selector(HudEditorPopup::onMoreTransparent), Blue);

    arrowButton(0.0f, {L + 356.0f, top}, 0);
    arrowButton(90.0f, {L + 378.0f, top}, 1);
    arrowButton(-90.0f, {L + 400.0f, top}, 2);
    arrowButton(180.0f, {L + 422.0f, top}, 3);
    m_presetLabel = toolbarCaption("", {R - 26.0f, top}, 44.0f, Gold, 0.18f);

    m_visibleButton = toolbarButton(i18n::text("hud-hide", "HIDE"), {50, 20}, {L + 31.0f, bottom},
        menu_selector(HudEditorPopup::onToggleVisible), Red, &m_visibleCaption);
    m_gridButton = toolbarButton(i18n::text("hud-grid-on", "GRID 8PX"), {54, 20}, {L + 88.0f, bottom},
        menu_selector(HudEditorPopup::onGrid), Blue, &m_gridCaption);
    auto const presets = HudPresets;
    for (std::size_t index = 0; index < presets.size(); ++index) {
        toolbarButton(i18n::text(std::string(hudPresetKey(m_mode, presets[index])), std::string(hudPresetFallback(m_mode, presets[index]))),
            {46, 20}, {L + 146.0f + 47.0f * static_cast<float>(index), bottom}, menu_selector(HudEditorPopup::onPreset), Cyan,
            nullptr, static_cast<int>(index));
    }
    toolbarButton(i18n::text("hud-reset", "RESET"), {46, 20}, {L + 146.0f + 47.0f * 3 + 6.0f, bottom}, menu_selector(HudEditorPopup::onReset), Orange);
    toolbarButton(i18n::text("hud-list", "LIST"), {42, 20}, {R - 126.0f, bottom}, menu_selector(HudEditorPopup::onList), Gold);
    toolbarButton(i18n::text("cancel", "CANCEL"), {42, 20}, {R - 80.0f, bottom}, menu_selector(HudEditorPopup::onCancel), Blue);
    toolbarButton(i18n::text("hud-save", "SAVE"), {50, 20}, {R - 30.0f, bottom}, menu_selector(HudEditorPopup::onSave), Green);
    // Collapse tab sits on the dock's top edge.
    toolbarButton(i18n::text("hud-collapse", "HIDE TOOLS"), {36, 14}, {R - 24.0f, m_toolbarSize.height / 2.0f + 8.0f},
        menu_selector(HudEditorPopup::onCollapse), Blue);

    // Collapsed tools leave one button at the bottom centre.
    {
        CCSize const size {84.0f, 22.0f};
        auto* face = surface(size, {}, Cyan, Surface, 245, true);
        auto* title = menuLabel(i18n::text("hud-tools-open", "TOOLS"), 0.24f, {size.width / 2.0f, size.height / 2.0f}, Text, FontWeight::ExtraBold);
        fitFullMenuLabel(title, size.width - 8.0f, size.height - 6.0f, 0.24f);
        face->addChild(title, 5);
        m_expandButton = CCMenuItemSpriteExtra::create(face, this, menu_selector(HudEditorPopup::onExpand));
        m_expandButton->setPosition({m_size.width / 2.0f, 6.0f + size.height / 2.0f});
        m_expandButton->setVisible(false);
        m_buttonMenu->addChild(m_expandButton);
    }
    placeToolbar({m_size.width / 2.0f, 6.0f + m_toolbarSize.height / 2.0f});
    buildList();
}

// Two columns of element rows over the canvas: tap a row to select the
// element, tap its right-hand button to show/hide it.
void HudEditorPopup::buildList() {
    auto const elements = hudElements(m_mode);
    auto const perColumn = (elements.size() + 1) / 2;
    CCSize const panel {324.0f, 40.0f + 22.0f * static_cast<float>(perColumn)};
    m_list = surface(panel, {m_size.width / 2.0f, m_size.height / 2.0f}, Gold, Surface, 250, true);
    m_list->setID("hud-editor-list"_spr);
    m_list->setVisible(false);
    m_canvas->addChild(m_list, 60);
    auto* title = menuLabel(i18n::text("hud-list-title", "ELEMENTS"), 0.28f, {panel.width / 2.0f - 30.0f, panel.height - 14.0f},
        Gold, FontWeight::Black);
    fitMenuLabelWidth(title, 200.0f, 0.28f, 0.16f);
    m_list->addChild(title, 5);
    auto add = [&](CCNode* face, SEL_MenuHandler selector, int tag, CCPoint at) {
        auto* item = CCMenuItemSpriteExtra::create(face, this, selector);
        item->setTag(tag);
        item->setPosition(at);
        item->setVisible(false);
        item->setEnabled(false);
        m_buttonMenu->addChild(item);
        m_listItems.push_back(item);
        return item;
    };
    auto const origin = CCPoint{m_size.width / 2.0f - panel.width / 2.0f, m_size.height / 2.0f - panel.height / 2.0f};
    auto* closeFace = surface({58, 18}, {}, Blue, SurfaceRaised, 250, true);
    auto* closeText = menuLabel(i18n::text("close", "CLOSE"), 0.22f, {29, 9}, Text, FontWeight::ExtraBold);
    fitFullMenuLabel(closeText, 50, 14, 0.22f);
    closeFace->addChild(closeText, 5);
    add(closeFace, menu_selector(HudEditorPopup::onList), 0, origin + CCPoint{panel.width - 36.0f, panel.height - 14.0f});
    for (std::size_t index = 0; index < elements.size(); ++index) {
        auto const column = index / perColumn, row = index % perColumn;
        auto const x = origin.x + 8.0f + static_cast<float>(column) * 158.0f;
        auto const y = origin.y + panel.height - 36.0f - static_cast<float>(row) * 22.0f;
        ListRow entry;
        entry.id = std::string(elements[index].id);
        auto* face = surface({112, 19}, {}, Blue, SurfaceRaised, 250, false);
        entry.name = menuLabel("", 0.2f, {5, 9.5f}, Text, FontWeight::ExtraBold);
        entry.name->setAnchorPoint({0.0f, 0.5f});
        face->addChild(entry.name, 5);
        entry.state = menuLabel("", 0.16f, {108, 9.5f}, TextMuted, FontWeight::SemiBold);
        entry.state->setAnchorPoint({1.0f, 0.5f});
        face->addChild(entry.state, 5);
        entry.select = add(face, menu_selector(HudEditorPopup::onListSelect), static_cast<int>(index), {x + 56.0f, y});
        auto* toggleFace = surface({38, 19}, {}, Red, SurfaceRaised, 250, false);
        entry.toggleCaption = menuLabel("", 0.18f, {19, 9.5f}, Text, FontWeight::ExtraBold);
        toggleFace->addChild(entry.toggleCaption, 5);
        entry.toggle = add(toggleFace, menu_selector(HudEditorPopup::onListToggle), static_cast<int>(index), {x + 135.0f, y});
        m_listRows.push_back(entry);
    }
}

void HudEditorPopup::setListOpen(bool open) {
    m_listOpen = open;
    m_drag = Drag::None;
    if (m_list) m_list->setVisible(open);
    for (auto* item : m_listItems) { item->setVisible(open); item->setEnabled(open); }
    // The toolbar stays put but is inert under the list.
    for (auto const& entry : m_toolbarItems) entry.item->setVisible(!open);
    if (m_toolbar) m_toolbar->setVisible(!open);
    refresh();
}

void HudEditorPopup::refreshList() {
    for (auto const& row : m_listRows) {
        auto const* spec = hudElement(m_mode, row.id);
        auto const t = transform(row.id);
        auto const selected = row.id == m_selected;
        row.name->setString(elementName(m_mode, row.id).c_str());
        row.name->setColor(selected ? Cyan : (t.visible ? Text : TextMuted));
        fitMenuLabelWidth(row.name, 62.0f, 0.2f, 0.11f);
        std::string state;
        if (t.scale != 1.0f) state = fmt::format("{}%", static_cast<int>(std::lround(t.scale * 100.0f)));
        if (t.opacity < 0.999f) {
            if (!state.empty()) state += " ";
            state += fmt::format("{}{}%", i18n::text("hud-transparency-short", "T"), static_cast<int>(std::lround((1.0f - t.opacity) * 100.0f)));
        }
        row.state->setString(state.c_str());
        fitMenuLabelWidth(row.state, 42.0f, 0.16f, 0.1f);
        row.toggleCaption->setString((t.visible ? i18n::text("hud-show", "SHOW") : i18n::text("hud-hidden-tag", "HIDDEN")).c_str());
        row.toggleCaption->setColor(t.visible ? Green : Red);
        fitMenuLabelWidth(row.toggleCaption, 32.0f, 0.18f, 0.1f);
        auto const hideable = spec && spec->hideable;
        if (m_listOpen) row.toggle->setEnabled(hideable);
        row.toggle->setCascadeOpacityEnabled(true);
        row.toggle->setOpacity(hideable ? 255 : 70);
        propagateHudOpacity(row.toggle, hideable ? 255 : 70);
        markUnavailable(row.toggle, !hideable);
        if (!hideable) {
            row.toggleCaption->setString(i18n::text("hud-always-shown", "LOCKED").c_str());
            row.toggleCaption->setColor(TextMuted);
            fitMenuLabelWidth(row.toggleCaption, 32.0f, 0.18f, 0.1f);
        }
    }
}

void HudEditorPopup::onList(CCObject*) {
    setListOpen(!m_listOpen);
}

void HudEditorPopup::onListSelect(CCObject* sender) {
    auto const index = sender ? sender->getTag() : -1;
    if (index < 0 || index >= static_cast<int>(m_listRows.size())) return;
    m_selected = m_listRows[static_cast<std::size_t>(index)].id;
    setListOpen(false);
}

void HudEditorPopup::onListToggle(CCObject* sender) {
    auto const index = sender ? sender->getTag() : -1;
    if (index < 0 || index >= static_cast<int>(m_listRows.size())) return;
    auto const& id = m_listRows[static_cast<std::size_t>(index)].id;
    auto const* spec = hudElement(m_mode, id);
    if (!spec || !spec->hideable) return;
    auto value = placement(id);
    value.visible = !value.visible;
    storePlacement(id, value);
    refresh();
}

void HudEditorPopup::placeToolbar(CCPoint center) {
    auto const halfW = m_toolbarSize.width / 2.0f, halfH = m_toolbarSize.height / 2.0f;
    center.x = m_size.width <= m_toolbarSize.width ? m_size.width / 2.0f : std::clamp(center.x, halfW, m_size.width - halfW);
    center.y = m_size.height <= m_toolbarSize.height ? m_size.height / 2.0f : std::clamp(center.y, halfH, m_size.height - halfH);
    m_toolbar->setPosition(center);
    for (auto const& entry : m_toolbarItems) entry.item->setPosition(center + entry.offset);
}

HudPlacement HudEditorPopup::placement(std::string const& id) const {
    auto const found = m_working.find(id);
    return found == m_working.end() ? HudPlacement{} : found->second;
}

void HudEditorPopup::storePlacement(std::string const& id, HudPlacement const& value) {
    if (value == HudPlacement{}) m_working.erase(id);
    else m_working[id] = value;
}

HudTransform HudEditorPopup::transform(std::string const& id) const {
    auto const value = placement(id);
    return resolveHudTransform(m_mode, id, &value, m_size.width, m_size.height);
}

CCRect HudEditorPopup::elementBox(std::string const& id) const {
    auto const found = m_groups.find(id);
    CCRect box;
    bool any = false;
    if (found != m_groups.end()) includeBox(found->second, m_canvas, found->second, box, any);
    if (!any) {
        auto const t = transform(id);
        return CCRect{t.position.x - 6.0f, t.position.y - 6.0f, 12.0f, 12.0f};
    }
    return box;
}

// Moves the selected element by a window delta, keeping it fully on screen.
void HudEditorPopup::moveSelected(float dx, float dy) {
    auto const* spec = hudElement(m_mode, m_selected);
    if (!spec || !spec->movable) return;
    auto const box = elementBox(m_selected);
    auto clampShift = [](float minimum, float extent, float limit, float move) {
        if (extent > limit) return 0.0f;
        return std::clamp(minimum + move, 0.0f, limit - extent) - minimum;
    };
    auto const sx = clampShift(box.getMinX(), box.size.width, m_size.width, dx);
    auto const sy = clampShift(box.getMinY(), box.size.height, m_size.height, dy);
    auto const current = placement(m_selected);
    auto const t = transform(m_selected);
    storePlacement(m_selected, hudPlacementAt({t.position.x + sx, t.position.y + sy},
        {box.getMidX() + sx, box.getMidY() + sy}, m_size.width, m_size.height, current.scale, current.visible, current.opacity));
    refresh();
}

// Keeps a scaled element fully on screen by moving it (never by resizing).
void HudEditorPopup::keepInside(std::string const& id) {
    auto const* spec = hudElement(m_mode, id);
    if (!spec || !spec->movable) return;
    auto const box = elementBox(id);
    float dx = 0.0f, dy = 0.0f;
    if (box.size.width <= m_size.width) {
        if (box.getMinX() < 0.0f) dx = -box.getMinX();
        else if (box.getMaxX() > m_size.width) dx = m_size.width - box.getMaxX();
    }
    if (box.size.height <= m_size.height) {
        if (box.getMinY() < 0.0f) dy = -box.getMinY();
        else if (box.getMaxY() > m_size.height) dy = m_size.height - box.getMaxY();
    }
    if (std::abs(dx) < 0.01f && std::abs(dy) < 0.01f) return;
    auto const current = placement(id);
    auto const t = transform(id);
    storePlacement(id, hudPlacementAt({t.position.x + dx, t.position.y + dy},
        {box.getMidX() + dx, box.getMidY() + dy}, m_size.width, m_size.height, current.scale, current.visible, current.opacity));
    if (auto found = m_groups.find(id); found != m_groups.end()) applyHudPlacement(found->second, transform(id));
}

void HudEditorPopup::refresh() {
    for (auto const& [id, node] : m_groups) {
        // Hidden elements stay visible (dimmed) in the editor so they can be
        // selected and shown again; visible ones preview their real opacity.
        auto t = transform(id);
        if (!t.visible) t.opacity = std::min(t.opacity, 0.25f);
        t.visible = true;
        applyHudPlacement(node, t);
    }
    auto const selected = transform(m_selected);
    auto const selectedBox = elementBox(m_selected);
    // Dock at the bottom unless the selected element sits under it.
    if (m_toolbar && m_drag != Drag::Element) {
        auto const bottomY = 6.0f + m_toolbarSize.height / 2.0f, topY = m_size.height - 6.0f - m_toolbarSize.height / 2.0f;
        auto const low = selectedBox.getMinY() < 6.0f + m_toolbarSize.height + 16.0f;
        auto const wanted = low ? topY : bottomY;
        if (std::abs(m_toolbar->getPosition().y - wanted) > 0.5f) placeToolbar({m_size.width / 2.0f, wanted});
    }
    bool overlap = false;
    m_outlines->clear();
    for (auto const& spec : hudElements(m_mode)) {
        auto const id = std::string(spec.id);
        auto const t = transform(id);
        auto const box = elementBox(id);
        if (id != m_selected && t.visible && selected.visible && !hudMutuallyExclusive(m_mode, id, m_selected)) {
            auto const left = std::max(box.getMinX(), selectedBox.getMinX()), right = std::min(box.getMaxX(), selectedBox.getMaxX());
            auto const bottom = std::max(box.getMinY(), selectedBox.getMinY()), top = std::min(box.getMaxY(), selectedBox.getMaxY());
            if (right - left > 2.0f && top - bottom > 2.0f) overlap = true;
        }
    }
    for (auto const& spec : hudElements(m_mode)) {
        auto const id = std::string(spec.id);
        auto const hidden = !transform(id).visible;
        auto found = m_hiddenTags.find(id);
        if (hidden && found == m_hiddenTags.end()) {
            auto* tag = label(i18n::text("hud-hidden-tag", "HIDDEN"), 0.2f, {}, Red, FontWeight::ExtraBold);
            m_canvas->addChild(tag, 42);
            found = m_hiddenTags.emplace(id, tag).first;
        }
        if (found == m_hiddenTags.end()) continue;
        found->second->setVisible(hidden);
        if (hidden) { auto const box = elementBox(id); found->second->setPosition({box.getMidX(), box.getMidY()}); }
    }
    // No borders on unselected elements: the preview reads as the real HUD.
    auto const stroke = overlap ? ccColor4F{1, 0.36f, 0.43f, 1} : ccColor4F{0.25f, 0.88f, 1, 1};
    drawCorners(m_outlines, selectedBox, stroke);

    auto const* spec = hudElement(m_mode, m_selected);
    auto const name = elementName(m_mode, m_selected);
    auto const percent = static_cast<int>(std::lround(selected.scale * 100.0f));
    auto const transparency = static_cast<int>(std::lround((1.0f - selected.opacity) * 100.0f));
    auto text = fmt::format("{} · {}%", name, percent);
    if (transparency > 0) text += fmt::format(" · {} {}%", i18n::text("hud-transparency", "TRANSPARENCY"), transparency);
    if (!selected.visible) text += " · " + i18n::text("hud-hidden-tag", "HIDDEN");
    if (overlap) text += " · " + i18n::text("hud-overlap", "OVERLAP");
    m_tag->setString(text.c_str());
    m_tag->setColor(overlap ? Red : Cyan);
    auto const tagHeight = m_tag->getScaledContentSize().height;
    auto const above = selectedBox.getMaxY() + 3.0f + tagHeight <= m_size.height;
    m_tag->setAnchorPoint({0.0f, above ? 0.0f : 1.0f});
    auto const tagX = std::clamp(selectedBox.getMinX(), 2.0f, std::max(2.0f, m_size.width - m_tag->getScaledContentSize().width - 2.0f));
    m_tag->setPosition({tagX, above ? selectedBox.getMaxY() + 3.0f : selectedBox.getMinY() - 3.0f});

    auto const elements = hudElements(m_mode);
    auto const index = static_cast<std::size_t>(std::find_if(elements.begin(), elements.end(),
        [&](auto const& candidate) { return candidate.id == m_selected; }) - elements.begin());
    m_elementName->setString(name.c_str());
    fitMenuLabelWidth(m_elementName, 78.0f, 0.24f, 0.13f);
    m_indexLabel->setString(fmt::format("{} / {}", index + 1, elements.size()).c_str());
    m_scaleLabel->setString(fmt::format("{}%", percent).c_str());
    m_transparencyLabel->setString(fmt::format("{}%", transparency).c_str());
    auto const hideable = spec && spec->hideable, movable = spec && spec->movable;
    m_visibleCaption->setString((selected.visible ? i18n::text("hud-hide", "HIDE") : i18n::text("hud-show", "SHOW")).c_str());
    fitMenuLabelWidth(m_visibleCaption, 76.0f, 0.26f, 0.14f);
    m_gridCaption->setString((m_grid > 1.0f ? i18n::text("hud-grid-on", "GRID 8PX") : i18n::text("hud-grid-off", "GRID OFF")).c_str());
    fitMenuLabelWidth(m_gridCaption, 112.0f, 0.26f, 0.14f);
    // Unavailable actions (hide on the timer, moving a fixed element, limits)
    // are greyed through the whole button, chrome included.
    auto enable = [this](CCMenuItemSpriteExtra* item, bool enabled) {
        item->setEnabled(enabled);
        item->setCascadeOpacityEnabled(true);
        auto const opacity = static_cast<GLubyte>(enabled ? (m_drag == Drag::Element ? 70 : 255) : 70);
        item->setOpacity(opacity);
        propagateHudOpacity(item, opacity);
        for (auto* face : item->getChildrenExt()) for (auto* child : face->getChildrenExt())
            if (auto* text = typeinfo_cast<CCLabelBMFont*>(child)) text->setColor(enabled ? Text : TextMuted);
    };
    enable(m_visibleButton, hideable);
    markUnavailable(m_visibleButton, !hideable);
    if (!hideable) {
        m_visibleCaption->setString(i18n::text("hud-always-shown", "LOCKED").c_str());
        fitMenuLabelWidth(m_visibleCaption, 76.0f, 0.26f, 0.14f);
    }
    enable(m_smallerButton, movable && selected.scale > HudMinimumScale + 0.001f);
    enable(m_largerButton, movable && selected.scale < HudMaximumScale - 0.001f);
    enable(m_transparentButton, selected.opacity > HudMinimumOpacity + 0.001f);
    enable(m_opaqueButton, selected.opacity < 0.999f);
    for (auto* nudge : m_nudgeButtons) enable(nudge, movable);
    std::string preset = i18n::text("hud-preset-custom", "Custom");
    for (auto candidate : HudPresets)
        if (hudPresetPlacements(m_mode, candidate) == m_working)
            preset = i18n::text(std::string(hudPresetKey(m_mode, candidate)), std::string(hudPresetFallback(m_mode, candidate)));
    m_presetLabel->setString(preset.c_str());
    fitMenuLabelWidth(m_presetLabel, 80.0f, 0.20f, 0.12f);
    refreshList();
}

bool HudEditorPopup::beginTouch(CCPoint location) {
    if (m_closed || !m_canvas) return false;
    auto const menuPoint = m_buttonMenu->convertToNodeSpace(location);
    for (auto const& entry : m_toolbarItems)
        if (entry.item->isVisible() && entry.item->boundingBox().containsPoint(menuPoint)) return false;
    for (auto* item : m_listItems)
        if (item->isVisible() && item->boundingBox().containsPoint(menuPoint)) return false;
    if (m_expandButton && m_expandButton->isVisible() && m_expandButton->boundingBox().containsPoint(menuPoint)) return false;
    auto const point = m_canvas->convertToNodeSpace(location);
    // While the list is open the canvas is not editable; swallow other taps.
    if (m_listOpen) { m_drag = Drag::None; return true; }
    // The dock is fixed: taps on its chrome do nothing.
    if (m_toolbar && m_toolbar->isVisible() && m_toolbar->boundingBox().containsPoint(point)) {
        m_drag = Drag::None;
        return true;
    }
    std::string hit;
    auto bestArea = std::numeric_limits<float>::max();
    for (auto const& spec : hudElements(m_mode)) {
        auto const id = std::string(spec.id);
        auto box = elementBox(id);
        // Small labels get a finger-sized hit area.
        auto const growX = std::max(0.0f, (22.0f - box.size.width) / 2.0f), growY = std::max(0.0f, (22.0f - box.size.height) / 2.0f);
        box = CCRect{box.getMinX() - growX, box.getMinY() - growY, box.size.width + growX * 2.0f, box.size.height + growY * 2.0f};
        if (!box.containsPoint(point)) continue;
        auto const area = box.size.width * box.size.height;
        if (area < bestArea) { bestArea = area; hit = id; }
    }
    if (hit.empty()) return false;
    m_selected = hit;
    auto const* spec = hudElement(m_mode, hit);
    if (spec && spec->movable) {
        m_drag = Drag::Element;
        m_dragStart = point;
        auto const t = transform(hit);
        m_dragOrigin = CCPoint{t.position.x, t.position.y};
        m_dragBox = elementBox(hit);
        m_dragScale = t.scale;
        setDragging(true);
    } else {
        m_drag = Drag::None;
    }
    refresh();
    return true;
}

void HudEditorPopup::moveTouch(CCPoint location) {
    if (m_closed || m_drag == Drag::None) return;
    auto const point = m_canvas->convertToNodeSpace(location);
    auto const delta = point - m_dragStart;
    if (m_drag == Drag::Toolbar) { placeToolbar(m_dragOrigin + delta); return; }
    // Snap the element's lower-left corner to the grid (8 px, or 1 px with
    // the grid off), then keep the whole element inside the window.
    auto const grid = m_grid;
    auto shiftAxis = [grid](float minimum, float extent, float limit, float move) {
        auto target = snapHud(minimum + move, grid);
        if (extent <= limit) target = std::clamp(target, 0.0f, limit - extent);
        else target = minimum;
        return target - minimum;
    };
    auto const shiftX = shiftAxis(m_dragBox.getMinX(), m_dragBox.size.width, m_size.width, delta.x);
    auto const shiftY = shiftAxis(m_dragBox.getMinY(), m_dragBox.size.height, m_size.height, delta.y);
    auto const current = placement(m_selected);
    storePlacement(m_selected, hudPlacementAt({m_dragOrigin.x + shiftX, m_dragOrigin.y + shiftY},
        {m_dragBox.getMidX() + shiftX, m_dragBox.getMidY() + shiftY}, m_size.width, m_size.height, m_dragScale,
        current.visible, current.opacity));
    refresh();
}

void HudEditorPopup::endTouch() {
    m_drag = Drag::None;
    setDragging(false);
}

void HudEditorPopup::setDragging(bool dragging) {
    if (m_gridLines) m_gridLines->setVisible(dragging);
    auto const opacity = static_cast<GLubyte>(dragging ? 70 : 255);
    if (m_toolbar) propagateHudOpacity(m_toolbar, opacity);
    for (auto const& entry : m_toolbarItems) {
        entry.item->setCascadeOpacityEnabled(true);
        if (entry.item->isEnabled()) entry.item->setOpacity(opacity);
    }
}

void HudEditorPopup::setToolsCollapsed(bool collapsed) {
    m_toolsCollapsed = collapsed;
    if (m_toolbar) m_toolbar->setVisible(!collapsed);
    for (auto const& entry : m_toolbarItems) entry.item->setVisible(!collapsed);
    if (m_expandButton) m_expandButton->setVisible(collapsed);
    if (collapsed && m_listOpen) setListOpen(false);
}

void HudEditorPopup::onCollapse(CCObject*) { setToolsCollapsed(true); }
void HudEditorPopup::onExpand(CCObject*) { setToolsCollapsed(false); }

void HudEditorPopup::onPrevious(CCObject*) {
    auto const elements = hudElements(m_mode);
    auto found = std::find_if(elements.begin(), elements.end(), [&](auto const& spec) { return spec.id == m_selected; });
    auto const index = found == elements.end() ? 0 : static_cast<std::size_t>(found - elements.begin());
    m_selected = std::string(elements[(index + elements.size() - 1) % elements.size()].id);
    refresh();
}

void HudEditorPopup::onNext(CCObject*) {
    auto const elements = hudElements(m_mode);
    auto found = std::find_if(elements.begin(), elements.end(), [&](auto const& spec) { return spec.id == m_selected; });
    auto const index = found == elements.end() ? 0 : static_cast<std::size_t>(found - elements.begin());
    m_selected = std::string(elements[(index + 1) % elements.size()].id);
    refresh();
}

void HudEditorPopup::onToggleVisible(CCObject*) {
    auto const* spec = hudElement(m_mode, m_selected);
    if (!spec || !spec->hideable) return;
    auto value = placement(m_selected);
    value.visible = !value.visible;
    storePlacement(m_selected, value);
    refresh();
}

void HudEditorPopup::onSmaller(CCObject*) {
    auto value = placement(m_selected);
    value.scale = clampHudScale(value.scale - HudScaleStep);
    storePlacement(m_selected, value);
    refresh();
    keepInside(m_selected);
    refresh();
}

void HudEditorPopup::onLarger(CCObject*) {
    auto value = placement(m_selected);
    value.scale = clampHudScale(value.scale + HudScaleStep);
    storePlacement(m_selected, value);
    refresh();
    keepInside(m_selected);
    refresh();
}

void HudEditorPopup::onMoreTransparent(CCObject*) {
    auto value = placement(m_selected);
    value.opacity = clampHudOpacity(value.opacity - HudOpacityStep);
    storePlacement(m_selected, value);
    refresh();
}

void HudEditorPopup::onLessTransparent(CCObject*) {
    auto value = placement(m_selected);
    value.opacity = clampHudOpacity(value.opacity + HudOpacityStep);
    storePlacement(m_selected, value);
    refresh();
}

// Tags: 0 left, 1 up, 2 down, 3 right.
void HudEditorPopup::onNudge(CCObject* sender) {
    auto const tag = sender ? sender->getTag() : -1;
    if (tag < 0 || tag > 3) return;
    static constexpr std::array<std::pair<float, float>, 4> directions {{{-1, 0}, {0, 1}, {0, -1}, {1, 0}}};
    auto const [x, y] = directions[static_cast<std::size_t>(tag)];
    moveSelected(x * HudFineStep, y * HudFineStep);
}

void HudEditorPopup::onGrid(CCObject*) {
    m_grid = m_grid > HudFineStep ? HudFineStep : HudSnapGrid;
    refresh();
}

void HudEditorPopup::onPreset(CCObject* sender) {
    auto const index = sender ? sender->getTag() : 0;
    if (index < 0 || index >= static_cast<int>(HudPresets.size())) return;
    m_working = hudPresetPlacements(m_mode, HudPresets[static_cast<std::size_t>(index)]);
    refresh();
}

void HudEditorPopup::onReset(CCObject*) {
    m_working.clear();
    refresh();
}

void HudEditorPopup::onCancel(CCObject*) {
    if (m_closed) return;
    m_closed = true;
    m_drag = Drag::None;
    onClose(nullptr);
}

void HudEditorPopup::onSave(CCObject*) {
    if (m_closed) return;
    m_saved.placements(m_mode) = m_working;
    saveHudLayout(m_saved);
    log::info("[HudEditor] saved mode={} elements={}", m_mode == HudMode::Solo ? "solo" : "survival", m_working.size());
    m_closed = true;
    m_drag = Drag::None;
    onClose(nullptr);
}

} // namespace corum::ranked::ui
