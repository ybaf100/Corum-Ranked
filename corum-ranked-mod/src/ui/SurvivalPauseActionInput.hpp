#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <functional>
#include <vector>

namespace corum::ranked::ui {
// A scene-owned route for the two Survival actions. Native Pause and its
// menus can re-register swallowing delegates after customSetup, especially
// on iOS. Claim only an enabled action's real, transformed rectangle.
class SurvivalPauseActionInput final : public cocos2d::CCLayer {
    struct Action {geode::WeakRef<CCMenuItemSpriteExtra> item;std::function<void()> invoke;};
    geode::WeakRef<cocos2d::CCNode> m_pause,m_scene;
    geode::WeakRef<PlayLayer> m_play;
    std::vector<Action> m_actions;
    std::function<bool()> m_allowed;
    int m_touch=-1,m_pressed=-1;
    unsigned m_epoch=0,m_diagnostics=0;
    bool m_pending=false,m_logged=false,m_registrationQueued=false,m_repairedRegistration=false;
    unsigned m_syncWarnings=0;
    bool valid()const;
    bool hit(std::size_t,cocos2d::CCTouch*)const;
    void cancelTouch();
public:
    static SurvivalPauseActionInput* create(cocos2d::CCNode*,PlayLayer*,std::function<bool()>);
    void bind(CCMenuItemSpriteExtra*,std::function<void()>);
    void synchronize();
    void suspend();
    void registerWithTouchDispatcher() override;
    void onEnter() override;
    void onExit() override;
    void update(float) override;
    bool ccTouchBegan(cocos2d::CCTouch*,cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch*,cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*,cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch*,cocos2d::CCEvent*) override;
};
}
