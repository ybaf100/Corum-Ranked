#include "ApiClient.hpp"
#include "BatchSubmitPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace {

CCDrawNode* createPaperPlaneIcon() {
    auto icon = CCDrawNode::create();
    icon->setContentSize({30.0f, 26.0f});
    icon->setAnchorPoint({0.5f, 0.5f});

    auto const white = ccc4f(1.0f, 1.0f, 1.0f, 1.0f);
    auto const shade = ccc4f(0.72f, 0.94f, 1.0f, 1.0f);
    auto const outline = ccc4f(0.05f, 0.05f, 0.05f, 1.0f);
    CCPoint upperWing[] {
        {1.5f, 22.5f},
        {28.5f, 13.5f},
        {10.0f, 12.0f},
    };
    CCPoint lowerWing[] {
        {10.0f, 12.0f},
        {28.5f, 13.5f},
        {5.0f, 2.5f},
    };
    icon->drawPolygon(upperWing, 3, white, 1.4f, outline);
    icon->drawPolygon(lowerWing, 3, shade, 1.4f, outline);
    return icon;
}

} // namespace

class $modify(CorumMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        corum::ApiClient::initializeSession();
        addBatchSubmitButton();
        return true;
    }

    void addBatchSubmitButton() {
        if (!Mod::get()->getSettingValue<bool>("enable-record-submit")) return;

        auto buttonSprite = CircleButtonSprite::create(
            createPaperPlaneIcon(),
            CircleBaseColor::Green,
            CircleBaseSize::Small
        );
        buttonSprite->setScale(0.78f);

        auto button = CCMenuItemSpriteExtra::create(
            buttonSprite,
            this,
            menu_selector(CorumMenuLayer::onBatchSubmit)
        );
        button->setID("batch-record-submit-button"_spr);

        if (auto menu = typeinfo_cast<CCMenu*>(
            getChildByID("bottom-menu")
        )) {
            menu->addChild(button);
            menu->updateLayout();
            return;
        }

        auto fallbackMenu = CCMenu::create();
        fallbackMenu->setID("batch-record-submit-menu"_spr);
        fallbackMenu->setPosition({
            CCDirector::sharedDirector()->getWinSize().width - 30.0f,
            82.0f,
        });
        fallbackMenu->addChild(button);
        addChild(fallbackMenu, 20);
    }

    void onBatchSubmit(CCObject*) {
        if (corum::ApiClient::isOutdated()) {
            corum::ApiClient::showUpdateRequiredWarning();
            return;
        }
        corum::showBatchSubmitPopup();
    }

    void onCreator(CCObject*) {
        createQuickPopup(
            "C Integration Active",
            "<cy>Corum Integration</c> is currently <cg>enabled</c>.\n"
            "Corum information and the manual record button can appear on listed custom levels.\n"
            "Records are sent only when you press the button.",
            "Cancel",
            "Continue",
            [](FLAlertLayer*, bool continueToCustomLevels) {
                if (continueToCustomLevels) {
                    auto transition = CCTransitionFade::create(0.5f, CreatorLayer::scene());
                    CCDirector::sharedDirector()->replaceScene(transition);
                }
            },
            true,
            true
        );
    }
};
