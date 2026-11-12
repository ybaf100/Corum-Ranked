#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <initializer_list>
#include <limits>

namespace corum::ranked::ui {

// Clearing m_pSelectedItem alone leaves CCMenu in TrackingTouch. Cancel both
// the native state and this menu's claimed touches before replacing its items
// or suspending it, so a late finger-up cannot activate a new/freed item.
//
// Inside touch dispatch (normally a button callback running from this very
// menu's ccTouchEnded), cocos2d 2.2 removes the delivered touch from the
// claim set right after the callback returns, and CCSet::removeObject
// releases even when the object is no longer in the set. Dropping that claim
// here as well released the CCTouch twice: the freed touch corrupted the heap
// and a later touch crashed inside CCTouchDispatcher::touches. While the
// dispatcher is locked, each claim's reference is handed to that pending
// release instead; a claim held by a different finger then leaks one small
// CCTouch rather than freeing memory that is still in use.
inline void cancelMenuTouch(cocos2d::CCMenu* menu) {
    if (!menu) return;
    if (menu->m_eState == cocos2d::kCCMenuStateTrackingTouch && menu->m_pSelectedItem) {
        menu->m_pSelectedItem->unselected();
    }
    menu->m_pSelectedItem = nullptr;
    menu->m_eState = cocos2d::kCCMenuStateWaiting;
    auto* dispatcher = cocos2d::CCDirector::sharedDirector()->getTouchDispatcher();
    auto* handler = dispatcher->findHandler(menu);
    auto* targeted = geode::cast::typeinfo_cast<cocos2d::CCTargetedTouchHandler*>(handler);
    auto* touches = targeted ? targeted->getClaimedTouches() : nullptr;
    if (!touches) return;
    if (dispatcher->m_bLocked) {
        for (auto it = touches->begin(); it != touches->end(); ++it) {
            if (*it) (*it)->retain();
        }
    }
    touches->removeAllObjects();
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

// Modal force-priority changes can put a new round's native delegates below
// -500. The scene-owned observer must precede BOTH of its native delegates;
// changing only the Pause menu leaves the observer behind swallowing gameplay.
// Never reorder unrelated/modal handlers or alter the force-priority registry.
inline void alignPauseObserverPriority(cocos2d::CCLayer* observer,
    cocos2d::CCLayer* input, cocos2d::CCMenu* menu) {
    auto* dispatcher = cocos2d::CCDirector::sharedDirector()->getTouchDispatcher();
    auto* own = observer ? dispatcher->findHandler(observer) : nullptr;
    if (!own) return;
    auto priority = own->m_nPriority;
    for (cocos2d::CCLayer* native : {input, static_cast<cocos2d::CCLayer*>(menu)}) {
        auto* handler = native ? dispatcher->findHandler(native) : nullptr;
        if (handler && handler->m_nPriority > std::numeric_limits<int>::min())
            priority = std::min(priority, handler->m_nPriority - 1);
    }
    if (priority != own->m_nPriority) dispatcher->setPriority(priority, observer);
}

} // namespace corum::ranked::ui
