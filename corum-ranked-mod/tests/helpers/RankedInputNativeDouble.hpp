#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Cocos 2.x public touch semantics: lower priorities run first, equal-priority
// delegates register newest-first, and End/Cancel only reaches a claimed touch.
// This is a host fixture, not a claim about the iPad's exact handler priorities.
namespace cocos2d {
struct CCPoint { float x=0,y=0; };
struct CCSize { float width=40,height=40; };
struct CCRect {float x,y,w,h; CCRect(float a,float b,float c,float d):x(a),y(b),w(c),h(d){}
    bool containsPoint(CCPoint p) const {return p.x>=x && p.x<=x+w && p.y>=y && p.y<=y+h;}};
// cocos2d 2.2 reference counting: the view owns an active touch, every claim
// retains it, and a double release asserts instead of freeing it twice.
struct CCTouch { int id; CCPoint point{110,110}; int refs=1; int getID() const {return id;}
    CCPoint getLocation() const {return point;}
    void retain() { ++refs; }
    void release() { assert(refs > 0 && "CCTouch released twice"); --refs; } };
struct CCEvent {};
struct CCObject { virtual ~CCObject() = default; };
struct CCNode : CCObject {
    std::shared_ptr<int> life=std::make_shared<int>(0);
    CCNode* parent = nullptr;
    std::string id;
    std::vector<CCNode*> children;
    bool visible = true;
    CCPoint origin,anchor;
    CCSize size;
    float scale=1;
    virtual bool isRunning() const {return true;}
    void setID(std::string value){id=std::move(value);}
    void autorelease(){}
    auto const& getChildrenExt()const{return children;}
    void addChild(CCNode* child){children.push_back(child);child->parent=this;}
    bool isVisible() const {return visible;}
    void setVisible(bool v) {visible=v;}
    CCSize getContentSize() const {return size;}
    void setPosition(CCPoint p) {origin=p;}
    void setAnchorPoint(CCPoint p){anchor=p;}
    CCNode* getChildByID(std::string const& value) const {
        for(auto* child:children)if(child->id==value)return child;
        return nullptr;
    }
    CCPoint convertToNodeSpace(CCPoint p) const {
        if(parent)p=parent->convertToNodeSpace(p);
        return {(p.x-origin.x)/scale+anchor.x*size.width,(p.y-origin.y)/scale+anchor.y*size.height};
    }
    CCNode* getParent() { return parent; }
};
struct CCTouchDelegate {
    virtual ~CCTouchDelegate() = default;
    virtual bool ccTouchBegan(CCTouch*, CCEvent*) { return false; }
    virtual void ccTouchEnded(CCTouch*, CCEvent*) {}
    virtual void ccTouchMoved(CCTouch*, CCEvent*) {}
    virtual void ccTouchCancelled(CCTouch*, CCEvent*) {}
};
// cocos2d-x 2.2.3 CCSet: addObject always retains, and removeObject releases
// even when the object is not in the set.
struct CCSet {
    std::set<CCTouch*> objects;
    using iterator = std::set<CCTouch*>::iterator;
    iterator begin() { return objects.begin(); }
    iterator end() { return objects.end(); }
    int count() const { return static_cast<int>(objects.size()); }
    bool containsObject(CCTouch* touch) const { return objects.contains(touch); }
    void addObject(CCTouch* touch) { touch->retain(); objects.insert(touch); }
    void removeObject(CCTouch* touch) { objects.erase(touch); touch->release(); }
    void removeAllObjects() { auto old = std::exchange(objects, {}); for (auto* touch : old) touch->release(); }
};
struct CCTouchHandler : CCObject {
    CCTouchDelegate* delegate = nullptr;
    int m_nPriority = 0;
};
struct CCTargetedTouchHandler : CCTouchHandler {
    CCSet claimed;
    ~CCTargetedTouchHandler() override { claimed.removeAllObjects(); }
    bool swallow = true;
    CCSet* getClaimedTouches() { return &claimed; }
};
struct CCTouchDispatcher {
    // Declared before handlers: handlers release their claims first.
    std::map<int, std::unique_ptr<CCTouch>> live;
    std::vector<std::unique_ptr<CCTargetedTouchHandler>> handlers;
    int adds = 0, removes = 0, priorityWrites = 0, leakedTouches = 0;
    bool m_bLocked = false;
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
    // Geode force-priority-aware delegate uses the same ordered dispatcher
    // semantics in this non-modal native fixture.
    void addPrioTargetedDelegate(CCTouchDelegate* delegate,int priority,bool swallow){addTargetedDelegate(delegate,priority,swallow);}
    void setPriority(int priority, CCTouchDelegate* delegate) {
        auto* h = findHandler(delegate); assert(h); h->m_nPriority = priority;
        ++priorityWrites;
        std::stable_sort(handlers.begin(), handlers.end(), [](auto const& a, auto const& b) {
            return a->m_nPriority < b->m_nPriority;
        });
    }
    void began(int id, CCPoint point = {110,110}) {
        auto& slot = live[id];
        if (slot) retire(std::move(slot));
        slot = std::make_unique<CCTouch>(CCTouch{id,point});
        auto* touch = slot.get();
        m_bLocked = true;
        for (auto& h : handlers) {
            if (h->delegate->ccTouchBegan(touch, nullptr)) {
                h->claimed.addObject(touch); if (h->swallow) break;
            }
        }
        m_bLocked = false;
    }
    // CCEGLViewProtocol::getSetOfTouchesEndOrCancel + CCTouchDispatcher::touches:
    // the event set retains the touch, the view drops its own reference, each
    // claiming handler is called and then removeObject()s it, then the event
    // set releases it.
    void finish(int id, CCPoint point, bool ended) {
        auto found = live.find(id);
        if (found == live.end()) return;
        auto owned = std::move(found->second); live.erase(found);
        auto* touch = owned.get(); touch->point = point;
        touch->retain(); touch->release();
        m_bLocked = true;
        for (auto& h : handlers) if (h->claimed.containsObject(touch)) {
            if (ended) h->delegate->ccTouchEnded(touch, nullptr);
            else h->delegate->ccTouchCancelled(touch, nullptr);
            h->claimed.removeObject(touch);
            if (ended && h->swallow) break;
        }
        m_bLocked = false;
        touch->release();
        retire(std::move(owned));
    }
    void retire(std::unique_ptr<CCTouch> touch) {
        if (touch->refs == 0) return;
        // Still referenced (a claim or an intentionally abandoned reference).
        ++leakedTouches; (void)touch.release();
    }
    void ended(int id, CCPoint point = {110,110}) { finish(id, point, true); }
    void moved(int id,CCPoint point){
        auto found=live.find(id); if(found==live.end())return;
        auto* touch=found->second.get(); touch->point=point;
        m_bLocked=true;
        for(auto& h:handlers)if(h->claimed.containsObject(touch))h->delegate->ccTouchMoved(touch,nullptr);
        m_bLocked=false;
    }
    void cancelled(int id){ auto found=live.find(id); finish(id, found==live.end()?CCPoint{}:found->second->point, false); }
};
struct CCDirector {
    CCTouchDispatcher dispatcher;
    CCNode* runningScene = nullptr;
    CCNode* nextScene = nullptr;
    static CCDirector* sharedDirector() { static CCDirector director; return &director; }
    CCTouchDispatcher* getTouchDispatcher() { return &dispatcher; }
    CCSize getWinSize() const {return {568,320};}
    CCNode* getNextScene(){return nextScene;}CCNode* getRunningScene() { return runningScene; }
};
struct CCLayer : CCNode, CCTouchDelegate {
    bool touch = true, running = false;
    int touchWrites = 0;
    bool isRunning()const override{return running;}
    bool init(){return true;}
    void scheduleUpdate(){}
    virtual void update(float){}
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
    CCObject* m_pListener = nullptr;
    void (*m_pfnSelector)() = nullptr;
    bool enabled = true, selected = false;
    int activations = 0, unselections = 0;
    bool isEnabled() const { return enabled; }
    void setEnabled(bool value) { enabled = value; }
    void unselected() { selected = false; ++unselections; }
    std::function<void()> callback;
    void activate() { assert(enabled); ++activations; if (callback) callback(); }
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
