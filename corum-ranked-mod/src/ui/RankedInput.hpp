#pragma once

#include <Geode/Geode.hpp>
#include <limits>

namespace corum::ranked::ui {

// Clearing m_pSelectedItem alone leaves CCMenu in TrackingTouch. Cancel both
// the native state and this menu's claimed touches before replacing its items
// or suspending it, so a late finger-up cannot activate a new/freed item.
inline void cancelMenuTouch(cocos2d::CCMenu* menu) {
    if (!menu) return;
    if (menu->m_eState == cocos2d::kCCMenuStateTrackingTouch && menu->m_pSelectedItem) {
        menu->m_pSelectedItem->unselected();
    }
    menu->m_pSelectedItem = nullptr;
    menu->m_eState = cocos2d::kCCMenuStateWaiting;
    auto* handler = cocos2d::CCDirector::sharedDirector()->getTouchDispatcher()->findHandler(menu);
    if (auto* targeted = geode::cast::typeinfo_cast<cocos2d::CCTargetedTouchHandler*>(handler)) {
        if (auto* touches = targeted->getClaimedTouches()) touches->removeAllObjects();
    }
}

// A swallowing gameplay delegate must never precede its own Pause menu. Scope
// the adjustment to that child menu; preserve any already-higher priority and
// leave modal/other-mod delegates and the global force-priority registry alone.
inline void restorePauseMenuInput(cocos2d::CCMenu* menu, cocos2d::CCLayer* input) {
    if (!menu) return;
    cancelMenuTouch(menu);
    menu->setEnabled(true);
    menu->setTouchEnabled(true);
    auto* dispatcher = cocos2d::CCDirector::sharedDirector()->getTouchDispatcher();
    auto* inputHandler = input ? dispatcher->findHandler(input) : nullptr;
    auto* menuHandler = dispatcher->findHandler(menu);
    if (inputHandler && menuHandler && inputHandler->m_nPriority > std::numeric_limits<int>::min() &&
        menuHandler->m_nPriority >= inputHandler->m_nPriority) {
        dispatcher->setPriority(inputHandler->m_nPriority - 1, menu);
    }
}

} // namespace corum::ranked::ui
