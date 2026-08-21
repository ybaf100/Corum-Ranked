#include "RankedPopup.hpp"
#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

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

} // namespace

class $modify(CorumRankedMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* icon = makeCrossedSwordsIcon();
        auto* sprite = CircleButtonSprite::create(
            icon,
            CircleBaseColor::Blue,
            CircleBaseSize::Small
        );
        sprite->setScale(0.82f);

        auto* button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(CorumRankedMenuLayer::onRanked)
        );
        button->setID("corum-ranked-button"_spr);

        auto* menu = CCMenu::create();
        menu->setID("corum-ranked-menu"_spr);
        // Keep Ranked away from Geometry Dash's crowded bottom action row.
        // The user-facing entry now lives as a compact upper-right shortcut.
        menu->setPosition({size.width - 34.0f, size.height - 36.0f});
        menu->addChild(button);
        addChild(menu, 20);

        auto* caption = CCLabelBMFont::create("RANKED", "bigFont.fnt");
        caption->setScale(0.22f);
        caption->setPosition({size.width - 34.0f, size.height - 62.0f});
        caption->setOpacity(225);
        caption->setID("corum-ranked-caption"_spr);
        addChild(caption, 20);

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
    }

    void onRanked(CCObject*) {
        corum::ranked::showRankedPopup();
    }
};
