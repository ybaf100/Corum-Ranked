#include "RankedPopup.hpp"
#include "RankedRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {

CCNode* makeSword(float rotation) {
    auto* sword = CCNode::create();
    sword->setContentSize({34.0f, 34.0f});
    sword->setAnchorPoint({0.5f, 0.5f});
    sword->setRotation(rotation);

    auto* blade = CCLayerColor::create(ccc4(235, 247, 255, 255), 4.0f, 23.0f);
    blade->ignoreAnchorPointForPosition(false);
    blade->setAnchorPoint({0.5f, 0.0f});
    blade->setPosition({17.0f, 9.0f});
    sword->addChild(blade, 2);

    auto* guard = CCLayerColor::create(ccc4(255, 213, 68, 255), 13.0f, 3.5f);
    guard->ignoreAnchorPointForPosition(false);
    guard->setAnchorPoint({0.5f, 0.5f});
    guard->setPosition({17.0f, 9.0f});
    sword->addChild(guard, 3);

    auto* grip = CCLayerColor::create(ccc4(105, 70, 45, 255), 3.5f, 8.0f);
    grip->ignoreAnchorPointForPosition(false);
    grip->setAnchorPoint({0.5f, 1.0f});
    grip->setPosition({17.0f, 8.0f});
    sword->addChild(grip, 3);

    return sword;
}

CCNode* makeCrossedSwordsIcon() {
    auto* root = CCNode::create();
    root->setContentSize({42.0f, 42.0f});
    root->setAnchorPoint({0.5f, 0.5f});

    auto* left = makeSword(-42.0f);
    left->setPosition({21.0f, 21.0f});
    root->addChild(left, 2);

    auto* right = makeSword(42.0f);
    right->setPosition({21.0f, 21.0f});
    root->addChild(right, 3);

    return root;
}

// A read-only timer must not cover native or third-party menu buttons.
bool buttonOverlaps(CCNode* node, CCNode* ignore, CCRect const& area) {
    if (!node || node == ignore || !node->isVisible()) return false;
    if (auto* item = typeinfo_cast<CCMenuItem*>(node); item && item->isEnabled()) {
        auto const size = item->getContentSize();
        if (size.width > 0 && size.height > 0) {
            auto const a = item->convertToWorldSpace({0, 0});
            auto const b = item->convertToWorldSpace({size.width, size.height});
            CCRect const rect{std::min(a.x, b.x), std::min(a.y, b.y),
                std::abs(a.x - b.x), std::abs(a.y - b.y)};
            if (rect.intersectsRect(area)) return true;
        }
    }
    if (auto* children = node->getChildren()) {
        for (unsigned i = 0; i < children->count(); ++i) {
            if (auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
                child && buttonOverlaps(child, ignore, area)) return true;
        }
    }
    return false;
}

} // namespace

class $modify(CorumRankedMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* visual = CCNode::create();
        visual->setContentSize({52.0f, 52.0f});
        visual->setAnchorPoint({0.5f, 0.5f});
        auto* plate = corum::ranked::ui::surface(
            {52.0f, 52.0f},
            {26.0f, 26.0f},
            corum::ranked::ui::Cyan,
            corum::ranked::ui::Surface,
            236,
            true
        );
        visual->addChild(plate, 0);
        auto* icon = makeCrossedSwordsIcon();
        icon->setPosition({26.0f, 26.0f});
        icon->setScale(0.88f);
        visual->addChild(icon, 3);

        auto* button = CCMenuItemSpriteExtra::create(
            visual,
            this,
            menu_selector(CorumRankedMenuLayer::onRanked)
        );
        button->setID("corum-ranked-button"_spr);

        auto* menu = CCMenu::create();
        menu->setID("corum-ranked-menu"_spr);
        // Keep Ranked away from Geometry Dash's crowded bottom action row.
        // The user-facing entry now lives as a compact upper-right shortcut.
        menu->setPosition({size.width - 38.0f, size.height - 40.0f});
        menu->addChild(button);
        addChild(menu, 20);

        auto* caption = corum::ranked::ui::label(
            "RANKED",
            0.17f,
            {size.width - 38.0f, size.height - 69.0f},
            corum::ranked::ui::Cyan,
            true
        );
        caption->setOpacity(225);
        caption->setID("corum-ranked-caption"_spr);
        addChild(caption, 20);

        // This small clock belongs to MenuLayer, not PlayLayer: Free Play can
        // display the ordinary GD menu while the Survival round keeps running.
        auto* survivalClock = CCNode::create();
        survivalClock->setID("survival-freeplay-menu-clock"_spr);
        survivalClock->setContentSize({124.0f, 28.0f});
        survivalClock->setVisible(false);
        auto* clockFace = corum::ranked::ui::surface(
            {124.0f, 28.0f}, {62.0f, 14.0f},
            corum::ranked::ui::Cyan, corum::ranked::ui::Surface, 236, true);
        survivalClock->addChild(clockFace, 0);
        auto* clockText = corum::ranked::ui::label(
            "ROUND 1  03:00", 0.23f, {62.0f, 14.0f},
            corum::ranked::ui::Cyan, true);
        clockText->setID("survival-freeplay-menu-clock-text"_spr);
        survivalClock->addChild(clockText, 5);
        addChild(survivalClock, 21);

        schedule(schedule_selector(CorumRankedMenuLayer::rankedTick), 0.25f);
        if (corum::ranked::RankedRuntime::get().view().stage == corum::ranked::RuntimeStage::Matched) {
            scheduleOnce(schedule_selector(CorumRankedMenuLayer::reopenRanked), 0.35f);
        }
        return true;
    }

    void reopenRanked(float) {
        auto const& view = corum::ranked::RankedRuntime::get().view();
        if (view.stage == corum::ranked::RuntimeStage::Matched) corum::ranked::showRankedPopup();
    }

    void rankedTick(float) {
        corum::ranked::RankedRuntime::get().tick();

        auto* root = getChildByID("survival-freeplay-menu-clock"_spr);
        if (!root) return;
        auto& survival = corum::ranked::SurvivalRuntime::get();
        auto const& view = survival.view();
        auto const remaining = survival.roundRemainingMs();

        // Free Play is an ordinary GD menu scene, but the server clock is still
        // authoritative. Never synthesize another countdown or pause the round.
        if (!survival.active() || !survival.freePlaying() ||
            view.state != "PLAYING" || !remaining) {
            root->setVisible(false);
            return;
        }

        auto* text = typeinfo_cast<CCLabelBMFont*>(
            root->getChildByID("survival-freeplay-menu-clock-text"_spr));
        if (!text) {
            root->setVisible(false);
            return;
        }
        auto const seconds = (std::max<std::int64_t>(0, *remaining) + 999) / 1000;
        auto const caption = fmt::format("R{}  {:02}:{:02}",
            view.roundNumber, seconds / 60, seconds % 60);
        if (caption != text->getString()) {
            text->setString(caption.c_str());
            corum::ranked::ui::fitFullMenuLabel(text, 116.0f, 24.0f, .23f);
        }

        auto const size = getContentSize();
        auto const width = root->getContentSize().width;
        auto const height = root->getContentSize().height;
        // Search the upper-left safe area rather than drawing over an existing
        // GD, Geode or other mod's clickable menu control.
        bool placed = false;
        for (float y = size.height - height - 10.0f;
             y >= std::max(size.height * .58f, 0.0f); y -= 10.0f) {
            constexpr float x = 9.0f;
            auto const a = convertToWorldSpace({x - 3, y - 3});
            auto const b = convertToWorldSpace({x + width + 3, y + height + 3});
            CCRect const area{std::min(a.x, b.x), std::min(a.y, b.y),
                std::abs(a.x - b.x), std::abs(a.y - b.y)};
            if (buttonOverlaps(this, root, area)) continue;
            root->setPosition({x, y});
            placed = true;
            break;
        }
        // Prefer no overlay to concealing any menu button if a modified menu
        // has no free upper-left space.
        root->setVisible(placed);
    }

    void onRanked(CCObject*) {
        corum::ranked::showRankedPopup();
    }
};
