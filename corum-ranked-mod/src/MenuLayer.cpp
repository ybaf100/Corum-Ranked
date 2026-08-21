#include "RankedPopup.hpp"
#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

class $modify(CorumRankedMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto label = CCLabelBMFont::create("R", "goldFont.fnt");
        label->setScale(0.72f);
        auto sprite = CircleButtonSprite::create(
            label,
            CircleBaseColor::Blue,
            CircleBaseSize::Small
        );
        sprite->setScale(0.78f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(CorumRankedMenuLayer::onRanked)
        );
        button->setID("corum-ranked-button"_spr);

        auto* menu = CCMenu::create();
        menu->setID("corum-ranked-menu"_spr);
        menu->setPosition({30.0f, 82.0f});
        menu->addChild(button);
        addChild(menu, 20);
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
