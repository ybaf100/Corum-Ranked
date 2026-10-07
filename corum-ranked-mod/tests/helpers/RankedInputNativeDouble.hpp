#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <set>
#include <vector>

// Cocos 2.x public touch semantics: lower priorities run first, equal-priority
// delegates register newest-first, and End/Cancel only reaches a claimed touch.
// This is a host fixture, not a claim about the iPad's exact handler priorities.
namespace cocos2d {
struct CCTouch { int id; };
struct CCEvent {};
struct CCObject { virtual ~CCObject() = default; };
struct CCNode : CCObject {
    CCNode* parent = nullptr;
    CCNode* getParent() { return parent; }
};
struct CCTouchDelegate {
    virtual ~CCTouchDelegate() = default;
    virtual bool ccTouchBegan(CCTouch*, CCEvent*) { return false; }
    virtual void ccTouchEnded(CCTouch*, CCEvent*) {}
};
struct CCSet {
    std::set<int> ids;
    void removeAllObjects() { ids.clear(); }
};
struct CCTouchHandler : CCObject {
    CCTouchDelegate* delegate = nullptr;
    int m_nPriority = 0;
};
struct CCTargetedTouchHandler : CCTouchHandler {
    CCSet claimed;
    bool swallow = true;
    CCSet* getClaimedTouches() { return &claimed; }
};
struct CCTouchDispatcher {
    std::vector<std::unique_ptr<CCTargetedTouchHandler>> handlers;
    int adds = 0, removes = 0, priorityWrites = 0;
    CCTouchHandler* findHandler(CCTouchDelegate* delegate) {
        for (auto& h : handlers) if (h->delegate == delegate) return h.get();
        return nullptr;
    }
    void removeDelegate(CCTouchDelegate* delegate) {
        ++removes;
        std::erase_if(handlers, [delegate](auto const& h) { return h->delegate == delegate; });
    }
    void addTargetedDelegate(CCTouchDelegate* delegate, int priority, bool swallow) {
        assert(!findHandler(delegate));
        auto h = std::make_unique<CCTargetedTouchHandler>();
        h->delegate = delegate; h->m_nPriority = priority; h->swallow = swallow;
        auto it = std::find_if(handlers.begin(), handlers.end(), [priority](auto const& p) {
            return p->m_nPriority >= priority;
        });
        handlers.insert(it, std::move(h)); ++adds;
    }
    void setPriority(int priority, CCTouchDelegate* delegate) {
        auto* h = findHandler(delegate); assert(h); h->m_nPriority = priority;
        ++priorityWrites;
        std::stable_sort(handlers.begin(), handlers.end(), [](auto const& a, auto const& b) {
            return a->m_nPriority < b->m_nPriority;
        });
    }
    void began(int id) {
        CCTouch touch{id};
        for (auto& h : handlers) {
            if (h->delegate->ccTouchBegan(&touch, nullptr)) {
                h->claimed.ids.insert(id); if (h->swallow) break;
            }
        }
    }
    void ended(int id) {
        CCTouch touch{id};
        for (auto& h : handlers) if (h->claimed.ids.contains(id)) {
            h->delegate->ccTouchEnded(&touch, nullptr);
            h->claimed.ids.erase(id);
            if (h->swallow) break;
        }
    }
};
struct CCDirector {
    CCTouchDispatcher dispatcher;
    static CCDirector* sharedDirector() { static CCDirector director; return &director; }
    CCTouchDispatcher* getTouchDispatcher() { return &dispatcher; }
};
struct CCLayer : CCNode, CCTouchDelegate {
    bool touch = true, running = false;
    int touchWrites = 0;
    virtual void registerWithTouchDispatcher() {
        CCDirector::sharedDirector()->dispatcher.addTargetedDelegate(this, -128, true);
    }
    bool isTouchEnabled() const { return touch; }
    virtual void setTouchEnabled(bool value) {
        ++touchWrites;
        if (touch == value) return;
        touch = value;
        if (running) {
            if (touch) registerWithTouchDispatcher();
            else CCDirector::sharedDirector()->dispatcher.removeDelegate(this);
        }
    }
    virtual void onEnter() { if (touch) registerWithTouchDispatcher(); running = true; }
    virtual void onExit() {
        if (touch) CCDirector::sharedDirector()->dispatcher.removeDelegate(this);
        running = false;
    }
    ~CCLayer() override { if (running && touch) CCDirector::sharedDirector()->dispatcher.removeDelegate(this); }
};
struct CCMenuItem : CCNode {
    bool enabled = true, selected = false;
    int activations = 0, unselections = 0;
    bool isEnabled() const { return enabled; }
    void setEnabled(bool value) { enabled = value; }
    void unselected() { selected = false; ++unselections; }
    void activate() { assert(enabled); ++activations; }
};
enum tCCMenuState { kCCMenuStateWaiting, kCCMenuStateTrackingTouch };
struct CCMenu : CCLayer {
    tCCMenuState m_eState = kCCMenuStateWaiting;
    CCMenuItem* m_pSelectedItem = nullptr;
    CCMenuItem* item = nullptr;
    bool enabled = true;
    bool isEnabled() const { return enabled; }
    void setEnabled(bool value) { enabled = value; }
    bool ccTouchBegan(CCTouch*, CCEvent*) override {
        if (m_eState != kCCMenuStateWaiting || !enabled || !item || !item->enabled) return false;
        m_pSelectedItem = item; item->selected = true; m_eState = kCCMenuStateTrackingTouch; return true;
    }
    void ccTouchEnded(CCTouch*, CCEvent*) override {
        assert(m_eState == kCCMenuStateTrackingTouch);
        if (m_pSelectedItem) { m_pSelectedItem->unselected(); m_pSelectedItem->activate(); }
        m_eState = kCCMenuStateWaiting;
    }
    void onExit() override {
        if (m_eState == kCCMenuStateTrackingTouch) {
            if (m_pSelectedItem) m_pSelectedItem->unselected();
            m_pSelectedItem = nullptr; m_eState = kCCMenuStateWaiting;
        }
        CCLayer::onExit();
    }
};
}
namespace geode::cast {
template<class Target, class Source> Target typeinfo_cast(Source* source) { return dynamic_cast<Target>(source); }
}
