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
#include <limits>
#include <tuple>

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
    m_outlines = CCDrawNode::create();
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
    auto* grid = CCDrawNode::create();
    for (float x = 16.0f; x < w; x += 16.0f) grid->drawSegment({x, 0}, {x, h}, 0.25f, {1, 1, 1, 0.035f});
    for (float y = 16.0f; y < h; y += 16.0f) grid->drawSegment({0, y}, {w, y}, 0.25f, {1, 1, 1, 0.035f});
    grid->drawSegment({w / 2.0f, 0}, {w / 2.0f, h}, 0.35f, {0.25f, 0.88f, 1, 0.18f});
    grid->drawSegment({0, h / 2.0f}, {w, h / 2.0f}, 0.35f, {0.25f, 0.88f, 1, 0.18f});
    grid->drawSegment({0, 64.0f}, {w, 64.0f}, 0.6f, {0.25f, 0.88f, 1, 0.35f});
    // Native progress bar and pause button: shown for reference, not editable.
    drawBox(grid, {w / 2.0f - 100.0f, h - 12.0f, 200.0f, 6.0f}, {1, 1, 1, 0.12f}, 0.5f, {1, 1, 1, 0.45f});
    drawBox(grid, {w / 2.0f - 100.0f, h - 12.0f, 112.0f, 6.0f}, {0.35f, 0.93f, 0.59f, 0.55f}, 0.0f, {0, 0, 0, 0});
    m_canvas->addChild(grid, -7);
    if (auto* pause = CCSprite::createWithSpriteFrameName("GJ_pauseBtn_001.png")) {
        pause->setScale(0.6f);
        pause->setOpacity(110);
        pause->setPosition({w - 18.0f, h - 18.0f});
        m_canvas->addChild(pause, -6);
    }
}

// Mirrors PlayLayer::addRankedHud (fonts, scales, anchors and coordinates).
void HudEditorPopup::buildSoloPreview() {
    auto const layout = layoutHud(m_size.width, m_size.height);
    auto const top = layout.topY, left = layout.topLeftX + 8.0f, right = layout.topRightX - 8.0f;
    auto const w = m_size.width;
    auto add = [](CCNode* parent, std::string const& text, FontWeight weight, float scale, CCPoint anchor, CCPoint at, ccColor3B color) {
        return rankedHudLabel(parent, text, weight, scale, anchor, at, color);
    };
    add(group("fps"), "FPS : 60", FontWeight::SemiBold, 0.51f, {0, 1}, {7.0f, top - 0.5f}, Text);

    auto* own = group("own");
    add(own, i18n::text("you", "YOU"), FontWeight::SemiBold, 0.51f, {0, 1}, {left, top - 4.0f}, Cyan).fit(96.0f, 0.51f, 0.21f);
    add(own, i18n::text("score", "SCORE"), FontWeight::SemiBold, 0.51f, {0, 1}, {left, top - 22.0f}, Text);
    add(own, "182.0", FontWeight::Black, 0.714f, {0, 1}, {left + 82.0f, top - 20.0f}, Text).fit(76.0f, 0.714f, 0.26f);
    auto* opponent = group("opponent");
    add(opponent, i18n::text("opponent", "OPPONENT"), FontWeight::SemiBold, 0.51f, {1, 1}, {right, top - 4.0f}, Red).fit(96.0f, 0.51f, 0.21f);
    add(opponent, i18n::text("score", "SCORE"), FontWeight::SemiBold, 0.51f, {1, 1}, {right - 88.0f, top - 22.0f}, Text);
    add(opponent, "140.0", FontWeight::Black, 0.714f, {1, 1}, {right, top - 20.0f}, Text).fit(76.0f, 0.714f, 0.26f);
    for (int index = 0; index < 3; ++index) {
        for (auto [parent, x, done] : {std::tuple{static_cast<CCNode*>(own), left + 8.0f + 17.0f * index, index < 2},
                                       std::tuple{static_cast<CCNode*>(opponent), right - 42.0f + 17.0f * index, index < 1}}) {
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
    add(group("timer"), "02:39", FontWeight::Black, CompetitiveClockScale, {0.5f, 1}, {w / 2.0f, top - 18.0f}, Text);
    add(group("state"), "MATCH POINT", FontWeight::SemiBold, 0.442f, {0.5f, 1}, {w / 2.0f, top - 47.0f}, Gold);

    auto* qualifying = group("qualifying");
    auto caption = add(qualifying, "Qualifying", FontWeight::SemiBold, 0.51f, {0, 0}, {layout.bottomLeftX + 4.0f, layout.bottomY + 4.0f}, Text);
    add(qualifying, "20%", FontWeight::Black, 0.714f, {0, 0},
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
    std::array<std::pair<std::string, ccColor3B>, 2> rows {{
        {fmt::format("{} 38%", i18n::text("opponent", "OPPONENT")), Green},
        {fmt::format("{} 21%", i18n::text("you", "YOU")), Red},
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
    auto* cutoff = CCDrawNode::create();
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

void HudEditorPopup::buildToolbar() {
    m_toolbarSize = CCSize{304.0f, 118.0f};
    m_toolbar = surface(m_toolbarSize, {}, Cyan, Surface, 246, true);
    m_toolbar->setID("hud-editor-toolbar"_spr);
    m_canvas->addChild(m_toolbar, 50);
    auto const half = CCPoint{m_toolbarSize.width / 2.0f, m_toolbarSize.height / 2.0f};
    auto* title = menuLabel(m_mode == HudMode::Solo
            ? i18n::text("hud-editor-title-solo", "SOLO HUD EDITOR")
            : i18n::text("hud-editor-title-survival", "SURVIVAL HUD EDITOR"),
        0.30f, {half.x - 40.0f, half.y + 46.0f}, Cyan, FontWeight::Black);
    fitMenuLabelWidth(title, 200.0f, 0.30f, 0.18f);
    m_toolbar->addChild(title, 5);
    m_presetLabel = menuLabel("", 0.20f, {half.x + 108.0f, half.y + 46.0f}, Gold, FontWeight::ExtraBold);
    m_toolbar->addChild(m_presetLabel, 5);
    auto* hint = menuLabel(i18n::text("hud-editor-hint", "Drag to move · tap to select · applies from the next match"),
        0.17f, {half.x, half.y + 31.0f}, TextMuted, FontWeight::SemiBold);
    fitMenuLabelWidth(hint, 288.0f, 0.17f, 0.11f);
    m_toolbar->addChild(hint, 5);

    toolbarButton("<", {22, 22}, {-136.0f, 10.0f}, menu_selector(HudEditorPopup::onPrevious), Blue);
    m_elementName = menuLabel("", 0.24f, {half.x - 88.0f, half.y + 10.0f}, Text, FontWeight::ExtraBold);
    m_toolbar->addChild(m_elementName, 5);
    toolbarButton(">", {22, 22}, {-40.0f, 10.0f}, menu_selector(HudEditorPopup::onNext), Blue);
    m_smallerButton = toolbarButton("-", {22, 22}, {-6.0f, 10.0f}, menu_selector(HudEditorPopup::onSmaller), Blue);
    m_scaleLabel = menuLabel("", 0.24f, {half.x + 22.0f, half.y + 10.0f}, Gold, FontWeight::Black);
    m_toolbar->addChild(m_scaleLabel, 5);
    m_largerButton = toolbarButton("+", {22, 22}, {50.0f, 10.0f}, menu_selector(HudEditorPopup::onLarger), Blue);
    m_visibleButton = toolbarButton(i18n::text("hud-hide", "HIDE"), {76, 22}, {108.0f, 10.0f},
        menu_selector(HudEditorPopup::onToggleVisible), Red, &m_visibleCaption);

    auto const presets = HudPresets;
    for (std::size_t index = 0; index < presets.size(); ++index) {
        toolbarButton(i18n::text(std::string(hudPresetKey(m_mode, presets[index])), std::string(hudPresetFallback(m_mode, presets[index]))),
            {68, 22}, {-111.0f + 72.0f * static_cast<float>(index), -18.0f}, menu_selector(HudEditorPopup::onPreset), Cyan,
            nullptr, static_cast<int>(index));
    }
    toolbarButton(i18n::text("hud-reset", "RESET"), {68, 22}, {111.0f, -18.0f}, menu_selector(HudEditorPopup::onReset), Orange);
    toolbarButton(i18n::text("cancel", "CANCEL"), {120, 24}, {-66.0f, -44.0f}, menu_selector(HudEditorPopup::onCancel), Blue);
    toolbarButton(i18n::text("hud-save", "SAVE"), {120, 24}, {66.0f, -44.0f}, menu_selector(HudEditorPopup::onSave), Green);

    auto const x = m_mode == HudMode::Solo ? m_size.width / 2.0f : m_size.width / 2.0f + 44.0f;
    placeToolbar({x, m_size.height / 2.0f - 6.0f});
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
        return {t.position.x - 6.0f, t.position.y - 6.0f, 12.0f, 12.0f};
    }
    return box;
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
        {box.getMidX() + dx, box.getMidY() + dy}, m_size.width, m_size.height, current.scale, current.visible));
    if (auto found = m_groups.find(id); found != m_groups.end()) applyHudPlacement(found->second, transform(id));
}

void HudEditorPopup::refresh() {
    for (auto const& [id, node] : m_groups) {
        auto const t = transform(id);
        applyHudPlacement(node, t);
        // Hidden elements stay visible (dimmed) in the editor so they can be
        // selected and shown again.
        node->setVisible(true);
        node->setOpacity(t.visible ? 255 : 60);
    }
    auto const selected = transform(m_selected);
    auto const selectedBox = elementBox(m_selected);
    bool overlap = false;
    m_outlines->clear();
    for (auto const& spec : hudElements(m_mode)) {
        auto const id = std::string(spec.id);
        auto const t = transform(id);
        auto const box = elementBox(id);
        if (id != m_selected && t.visible && selected.visible) {
            auto const left = std::max(box.getMinX(), selectedBox.getMinX()), right = std::min(box.getMaxX(), selectedBox.getMaxX());
            auto const bottom = std::max(box.getMinY(), selectedBox.getMinY()), top = std::min(box.getMaxY(), selectedBox.getMaxY());
            if (right - left > 2.0f && top - bottom > 2.0f) overlap = true;
        }
        if (id == m_selected) continue;
        drawBox(m_outlines, box, {0, 0, 0, 0}, 0.5f, t.visible ? ccColor4F{1, 1, 1, 0.28f} : ccColor4F{1, 0.36f, 0.43f, 0.45f});
    }
    auto const stroke = overlap ? ccColor4F{1, 0.36f, 0.43f, 1} : ccColor4F{0.25f, 0.88f, 1, 1};
    drawBox(m_outlines, selectedBox, {stroke.r, stroke.g, stroke.b, 0.10f}, 0.9f, stroke);

    auto const* spec = hudElement(m_mode, m_selected);
    auto const name = elementName(m_mode, m_selected);
    auto text = fmt::format("{} · {}%", name, static_cast<int>(std::lround(selected.scale * 100.0f)));
    if (!selected.visible) text += " · " + i18n::text("hud-hidden-tag", "HIDDEN");
    if (overlap) text += " · " + i18n::text("hud-overlap", "OVERLAP");
    m_tag->setString(text.c_str());
    m_tag->setColor(overlap ? Red : Cyan);
    auto const tagHeight = m_tag->getScaledContentSize().height;
    auto const above = selectedBox.getMaxY() + 3.0f + tagHeight <= m_size.height;
    m_tag->setAnchorPoint({0.0f, above ? 0.0f : 1.0f});
    auto const tagX = std::clamp(selectedBox.getMinX(), 2.0f, std::max(2.0f, m_size.width - m_tag->getScaledContentSize().width - 2.0f));
    m_tag->setPosition({tagX, above ? selectedBox.getMaxY() + 3.0f : selectedBox.getMinY() - 3.0f});

    m_elementName->setString(name.c_str());
    fitMenuLabelWidth(m_elementName, 70.0f, 0.24f, 0.14f);
    m_scaleLabel->setString(fmt::format("{}%", static_cast<int>(std::lround(selected.scale * 100.0f))).c_str());
    auto const hideable = spec && spec->hideable, movable = spec && spec->movable;
    m_visibleCaption->setString((selected.visible ? i18n::text("hud-hide", "HIDE") : i18n::text("hud-show", "SHOW")).c_str());
    fitMenuLabelWidth(m_visibleCaption, 68.0f, 0.26f, 0.14f);
    for (auto [item, enabled] : {std::pair{m_visibleButton, hideable},
                                 std::pair{m_smallerButton, movable && selected.scale > HudMinimumScale + 0.001f},
                                 std::pair{m_largerButton, movable && selected.scale < HudMaximumScale - 0.001f}}) {
        item->setEnabled(enabled);
        item->setCascadeOpacityEnabled(true);
        item->setOpacity(enabled ? 255 : 90);
    }
    std::string preset = i18n::text("hud-preset-custom", "Custom");
    for (auto candidate : HudPresets)
        if (hudPresetPlacements(m_mode, candidate) == m_working)
            preset = i18n::text(std::string(hudPresetKey(m_mode, candidate)), std::string(hudPresetFallback(m_mode, candidate)));
    m_presetLabel->setString(preset.c_str());
    fitMenuLabelWidth(m_presetLabel, 80.0f, 0.20f, 0.12f);
}

bool HudEditorPopup::beginTouch(CCPoint location) {
    if (m_closed || !m_canvas) return false;
    auto const menuPoint = m_buttonMenu->convertToNodeSpace(location);
    for (auto const& entry : m_toolbarItems)
        if (entry.item->isVisible() && entry.item->boundingBox().containsPoint(menuPoint)) return false;
    auto const point = m_canvas->convertToNodeSpace(location);
    if (m_toolbar && m_toolbar->boundingBox().containsPoint(point)) {
        m_drag = Drag::Toolbar;
        m_dragStart = point;
        m_dragOrigin = m_toolbar->getPosition();
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
    // Snap the element's lower-left corner to the 8 px grid, then keep the
    // whole element inside the window.
    auto shiftAxis = [](float minimum, float extent, float limit, float move) {
        auto target = snapHud(minimum + move);
        if (extent <= limit) target = std::clamp(target, 0.0f, limit - extent);
        else target = minimum;
        return target - minimum;
    };
    auto const shiftX = shiftAxis(m_dragBox.getMinX(), m_dragBox.size.width, m_size.width, delta.x);
    auto const shiftY = shiftAxis(m_dragBox.getMinY(), m_dragBox.size.height, m_size.height, delta.y);
    auto const current = placement(m_selected);
    storePlacement(m_selected, hudPlacementAt({m_dragOrigin.x + shiftX, m_dragOrigin.y + shiftY},
        {m_dragBox.getMidX() + shiftX, m_dragBox.getMidY() + shiftY}, m_size.width, m_size.height, m_dragScale, current.visible));
    refresh();
}

void HudEditorPopup::endTouch() {
    m_drag = Drag::None;
}

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
