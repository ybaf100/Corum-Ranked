#pragma once

// Renderer/actions/GD are boundary doubles. Tests compile the Corum layer and
// callbacks unchanged; this fixture does not claim native-device rendering.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using GLubyte = unsigned char;
struct CCPoint { float x = 0, y = 0; CCPoint operator+(CCPoint p) const { return {x+p.x,y+p.y}; } };
struct CCSize { float width = 0, height = 0; };
struct ccColor3B { GLubyte r,g,b; bool operator==(ccColor3B const&) const = default; };
struct ccColor4B { GLubyte r,g,b,a; };
inline CCPoint ccp(float x,float y) { return {x,y}; }
inline ccColor3B ccc3(GLubyte r,GLubyte g,GLubyte b) { return {r,g,b}; }
inline constexpr CCPoint CCPointZero{};
inline constexpr CCSize CCSizeZero{};
struct CCObject { virtual ~CCObject() = default; };
struct CCAction : CCObject { std::string type; float duration=0,value=0; std::vector<CCAction*> children; };
template<class T> CCAction* action(char const* name,float duration,T) {
    auto* a=new CCAction; a->type=name; a->duration=duration; return a;
}
inline CCAction* action(char const* name,float duration,float value=0) {
    auto* a=new CCAction; a->type=name; a->duration=duration; a->value=value; return a;
}
#define TIMED_ACTION(name,type) struct name { static CCAction* create(float d,type v) { return action(#name,d,v); } }
TIMED_ACTION(CCScaleTo,float); TIMED_ACTION(CCMoveBy,CCPoint); TIMED_ACTION(CCMoveTo,CCPoint); TIMED_ACTION(CCRotateBy,float);
#undef TIMED_ACTION
#define DURATION_ACTION(name) struct name { static CCAction* create(float d) { return action(#name,d); } }
DURATION_ACTION(CCFadeIn); DURATION_ACTION(CCFadeOut); DURATION_ACTION(CCDelayTime);
#undef DURATION_ACTION
#define WRAPPED_ACTION(name) struct name { static CCAction* create(CCAction* a) { auto* r=action(#name,a->duration); r->children={a}; return r; } }
WRAPPED_ACTION(CCEaseBackIn); WRAPPED_ACTION(CCEaseBackOut); WRAPPED_ACTION(CCEaseSineIn);
#undef WRAPPED_ACTION
struct CCSequence { template<class... T> static CCAction* create(T... children) { auto* a=action("sequence",0); ((a->children.push_back(children)),...); return a; } };
struct CCSpawn { template<class... T> static CCAction* create(T... children) { auto* a=action("spawn",0); ((a->children.push_back(children)),...); return a; } };
struct CCRemoveSelf { static CCAction* create() { return action("remove",0); } };
struct CCShow { static CCAction* create() { return action("show",0); } };
struct CCNode : CCObject {
    CCSize size{}; CCPoint position{},anchor{}; ccColor3B color{255,255,255};
    std::string id; float scaleX=1,scaleY=1,rotation=0; int references=1,opacity=255;
    bool visible=true,running=true,keypad=false,scheduled=false; CCNode* parent=nullptr;
    std::vector<CCNode*> children; std::vector<CCAction*> actions;
    static CCNode* create() { return new CCNode; }
    virtual void onEnter() { running=true; }
    virtual void onExit() { running=false; }
    virtual void update(float) {} virtual void keyBackClicked() {}
    void addChild(CCNode* node,int=0) { children.push_back(node); node->parent=this; }
    void removeFromParentAndCleanup(bool) { onExit(); if(parent) { std::erase(parent->children,this); parent=nullptr; --references; } }
    CCNode* getParent() { return parent; }
    auto const& getChildrenExt() { return children; }
    void setContentSize(CCSize v) { size=v; } CCSize getContentSize() const { return size; }
    void setPosition(CCPoint v) { position=v; } void setAnchorPoint(CCPoint v) { anchor=v; }
    void ignoreAnchorPointForPosition(bool) {} void setCascadeOpacityEnabled(bool) {}
    void setScale(float v) { scaleX=scaleY=v; } float getScale() const { return scaleX; }
    float getScaleX() const { return scaleX; } float getScaleY() const { return scaleY; }
    void setColor(ccColor3B v) { color=v; } void setRotation(float v) { rotation=v; }
    ccColor3B getColor() const { return color; }
    void setOpacity(int v) { opacity=v; } int getOpacity() const { return opacity; }
    void setID(std::string v) { id=std::move(v); } void setVisible(bool v) { visible=v; }
    bool isVisible() const { return visible; }
    bool isRunning() const { return running; } void setKeypadEnabled(bool v) { keypad=v; }
    void autorelease() {} void scheduleUpdate() { scheduled=true; } void unscheduleUpdate() { scheduled=false; }
    void runAction(CCAction* a) { actions.push_back(a); } void stopAllActions() { actions.clear(); }
};
struct CCLayerColor : CCNode {
    static CCLayerColor* create(ccColor4B c) { auto* n=new CCLayerColor; n->color={c.r,c.g,c.b}; n->opacity=c.a; return n; }
    bool initWithColor(ccColor4B c) { color={c.r,c.g,c.b}; opacity=c.a; return true; }
};
struct CCSprite : CCNode {
    static CCSprite* createWithSpriteFrameName(char const* frame) { auto* n=new CCSprite; n->id=frame; n->size={32,32}; return n; }
};
struct CCLabelBMFont : CCNode {
    std::string text; void setString(char const* v) { text=v; size={static_cast<float>(text.size())*7,16}; }
};
struct CCMenu : CCNode { bool touch=true; int cancellations=0; static CCMenu* create() { return new CCMenu; } void setTouchEnabled(bool v) { touch=v; } };
struct CCMenuItemSpriteExtra : CCNode {
    bool enabled=true; void setEnabled(bool value) { enabled=value; }
    CCNode* getNormalImage() const { return children.front(); }
    static CCMenuItemSpriteExtra* create(CCNode* face,CCNode*,int) { auto* n=new CCMenuItemSpriteExtra; n->addChild(face); return n; }
};
struct CCDirector { CCSize size{568,320}; static CCDirector* sharedDirector() { static CCDirector d; return &d; } CCSize getWinSize() const { return size; } };
template<class T> struct Ref { T* ptr; explicit Ref(T* p):ptr(p) { ++ptr->references; } ~Ref() { --ptr->references; } };
template<class T> struct WeakRef { T* ptr; explicit WeakRef(T* p):ptr(p) {} struct Locked { T* p; explicit operator bool() const { return p&&p->references>0; } T* operator->() const { return p; } T* data() const { return p; } }; Locked lock() const { return {ptr}; } };
inline std::vector<std::function<void()>> mainQueue;
inline void queueInMainThread(std::function<void()> f) { mainQueue.push_back(std::move(f)); }
inline void drainMainQueue() { auto queue=std::move(mainQueue); mainQueue.clear(); for(auto& callback:queue) callback(); }
#define menu_selector(x) 0
inline std::string operator""_spr(char const* s,std::size_t n) { return {s,n}; }
namespace cocos2d { using ::CCObject; using ::CCNode; using ::CCLayerColor; using ::CCSprite; using ::CCLabelBMFont; using ::CCMenu; using ::CCPoint; }
namespace geode::prelude {}
namespace fmt {
template<class... T> std::string format(std::string pattern,T const&... args) {
    std::vector<std::string> parts;
    auto part=[&](auto const& v) { std::ostringstream s; if(pattern=="{:.2f}") s<<std::fixed<<std::setprecision(2); s<<v; parts.push_back(s.str()); };
    (part(args),...); std::size_t pos=0;
    for(auto value:parts) { auto open=pattern.find('{',pos); if(open==std::string::npos) break; auto close=pattern.find('}',open);
        if(pattern.substr(open,close-open).find('+')!=std::string::npos&&!value.empty()&&value[0]!='-') value="+"+value;
        pattern.replace(open,close-open+1,value); pos=open+value.size(); }
    return pattern;
}
}
namespace corum::ranked::log { template<class... T> void warn(T const&...) {} }
