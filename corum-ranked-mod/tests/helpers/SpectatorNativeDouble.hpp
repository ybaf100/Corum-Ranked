#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
namespace cocos2d {
struct CCSize { float width=0, height=0; };
struct CCPoint {
    float x=0, y=0;
    CCPoint& operator=(CCSize const& s) { x=s.width; y=s.height; return *this; }
};
inline constexpr CCPoint CCPointZero{};
struct CCRect { float x=0,y=0,width=0,height=0; };
struct CCAffineTransform { float a=1,b=0,c=0,d=1,tx=0,ty=0; };
inline CCPoint CCPointApplyAffineTransform(CCPoint p,CCAffineTransform t) {
 return {t.a*p.x+t.c*p.y+t.tx,t.b*p.x+t.d*p.y+t.ty};
}
struct Color { unsigned char r,g,b; };
struct CCObject {
    inline static int alive=0;
    inline static std::vector<CCObject*> pool;
    int refs=1;
    CCObject() { ++alive; }
    virtual ~CCObject() { --alive; }
    void retain() { ++refs; }
    void release() { if (--refs==0) delete this; }
    void autorelease() { pool.push_back(this); }
    static void drain() { auto objects=pool; pool.clear(); for (auto* o:objects) o->release(); }
};
struct CCFadeOut: CCObject { static CCFadeOut* create(float) { auto* a=new CCFadeOut; a->autorelease(); return a; } };
struct CCNode: CCObject {
    CCNode* parent=nullptr; std::vector<CCNode*> children;
    CCPoint position{}; float rotation=0; bool visible=true; int z=0;
    int positionWrites=0, rotationWrites=0, unschedules=0; float scaleY=1,scaleX=1; unsigned char opacity=255;
    void runAction(CCFadeOut*) { opacity=0; }
    void setScaleY(float s) { scaleY=s; } float getScaleY()const{return scaleY;}
    CCPoint getPosition()const{return position;} float getRotation()const{return rotation;}
    static CCNode* create() { auto* p=new CCNode; p->autorelease(); return p; }
    ~CCNode() override { for(auto* p:children) { p->parent=nullptr; p->release(); } }
    void addChild(CCNode* p,int order=0) { assert(!p->parent); p->retain(); p->parent=this; p->z=order; children.push_back(p); }
    void removeFromParentAndCleanup(bool) {
        if (!parent) return;
        auto& c=parent->children; c.erase(std::find(c.begin(),c.end(),this)); parent=nullptr; release();
    }
    void unscheduleAllSelectors() { ++unschedules; }
    void stopAllActions() {}
    void setID(std::string const&) {}
    void setAnchorPoint(CCPoint) {}
    void ignoreAnchorPointForPosition(bool) {}
    void setPosition(CCPoint p) { position=p; ++positionWrites; }
    void setRotation(float r) { rotation=r; ++rotationWrites; }
    void setVisible(bool b) { visible=b; }
    bool isVisible() const { return visible; }
    int getZOrder() const { return z; }
    float getScale() const { return scaleX; }
    void setScale(float s) { scaleX=scaleY=s; }
    CCNode* getParent()const{return parent;}
    float getPositionX()const{return position.x;} float getPositionY()const{return position.y;}
    CCAffineTransform nodeToParentTransform()const {
      auto angle=rotation*3.14159265f/180; auto c=std::cos(angle),s=std::sin(angle);
      return {c*scaleX,s*scaleX,-s*scaleY,c*scaleY,position.x,position.y};
    }
    CCAffineTransform nodeToWorldTransform()const {
      auto n=nodeToParentTransform(); if(!parent)return n; auto p=parent->nodeToWorldTransform();
      auto o=CCPointApplyAffineTransform({n.tx,n.ty},p);
      return {p.a*n.a+p.c*n.b,p.b*n.a+p.d*n.b,p.a*n.c+p.c*n.d,p.b*n.c+p.d*n.d,o.x,o.y};
    }
    CCPoint convertToNodeSpace(CCPoint p)const {
      auto t=nodeToWorldTransform();auto det=t.a*t.d-t.b*t.c;
      return {(t.d*(p.x-t.tx)-t.c*(p.y-t.ty))/det,(-t.b*(p.x-t.tx)+t.a*(p.y-t.ty))/det};
    }
};
struct CCLayer: CCNode { static CCLayer* create() { auto* p=new CCLayer; p->autorelease(); return p; } };
struct CCSprite: CCNode { CCRect rect{}; void setTextureRect(CCRect r){rect=r;} };
struct CCLabelBMFont: CCNode { std::string text; void setString(char const* s){text=s;} };
struct CCDirector {
    unsigned totalFrames=0; unsigned getTotalFrames()const{return totalFrames;}
    static CCDirector* sharedDirector() { static CCDirector d; return &d; }
    CCSize getWinSize() const { return {600,300}; }
};
}
namespace geode {
template<class T> class Ref {
    T* p=nullptr;
public:
    Ref()=default; Ref(T* v):p(v) { if(p)p->retain(); }
    Ref(Ref const& v):Ref(v.p) {}
    ~Ref() { if(p)p->release(); }
    Ref& operator=(T* v) { if(v)v->retain(); if(p)p->release(); p=v; return *this; }
    Ref& operator=(Ref const& v) { return *this=v.p; }
    T* data() const { return p; } T* operator->() const { return p; }
    explicit operator bool() const { return p!=nullptr; }
};
namespace log { template<class... T> void info(T...) {} template<class... T> void warn(T...) {} template<class... T> void debug(T...) {} }
namespace prelude { using namespace cocos2d; using geode::Ref; namespace log=geode::log; }
}
struct GJGameLevel { int m_levelID=123,m_timestamp=0; };
struct PlayLayer;
struct PlayerObject: cocos2d::CCNode {
    inline static int creates=0; inline static bool fail=false;
    bool m_playEffects=true,m_isHidden=false,m_isDead=false;
    bool m_isShip=false,m_isBall=false,m_isBird=false,m_isDart=false,m_isRobot=false,m_isSpider=false,m_isSwing=false,m_isUpsideDown=false;
    float m_vehicleSize=1; int nativeModeCalls=0,miniCalls=0,frame=0;
    cocos2d::CCNode* m_mainLayer=nullptr;
    void toggleFlyMode(bool b,bool){m_isShip=b;++nativeModeCalls;}
    void toggleRollMode(bool b,bool){m_isBall=b;++nativeModeCalls;}
    void toggleBirdMode(bool b,bool){m_isBird=b;++nativeModeCalls;}
    void toggleDartMode(bool b,bool){m_isDart=b;++nativeModeCalls;}
    void toggleRobotMode(bool b,bool){m_isRobot=b;++nativeModeCalls;}
    void toggleSpiderMode(bool b,bool){m_isSpider=b;++nativeModeCalls;}
    void toggleSwingMode(bool b,bool){m_isSwing=b;++nativeModeCalls;}
    void togglePlayerScale(bool b,bool){m_vehicleSize=b?0.6f:1.0f;++miniCalls;}
    void updatePlayerScale(){}
    void updatePlayerFrame(int){frame=0;} void updatePlayerShipFrame(int){frame=1;}
    void updatePlayerRollFrame(int){frame=2;} void updatePlayerBirdFrame(int){frame=3;}
    void updatePlayerDartFrame(int){frame=4;} void updatePlayerRobotFrame(int){frame=5;}
    void updatePlayerSpiderFrame(int){frame=6;} void updatePlayerSwingFrame(int){frame=7;}
    cocos2d::CCPoint m_startPosition{},m_lastPosition{};
    double m_positionX=0,m_positionY=0;
    int gameplayPositionWrites=0;
    void setPosition(cocos2d::CCPoint p){++gameplayPositionWrites;cocos2d::CCNode::setPosition(p);m_positionX=p.x;m_positionY=p.y;}
    cocos2d::CCNode *m_shipStreak=nullptr,*m_regularTrail=nullptr,*m_waveTrail=nullptr,*m_ghostTrail=nullptr,
      *m_playerGroundParticles=nullptr,*m_trailingParticles=nullptr,*m_shipClickParticles=nullptr,
      *m_vehicleGroundParticles=nullptr,*m_ufoClickParticles=nullptr,*m_robotBurstParticles=nullptr,
      *m_dashParticles=nullptr,*m_swingBurstParticles1=nullptr,*m_swingBurstParticles2=nullptr,
      *m_landParticles0=nullptr,*m_landParticles1=nullptr;
    static PlayerObject* create(int,int,PlayLayer*,cocos2d::CCLayer* layer,bool) {
        ++creates; if(fail)return nullptr;
        auto* p=new PlayerObject; p->autorelease();
        p->m_mainLayer=cocos2d::CCNode::create(); p->addChild(p->m_mainLayer);
        p->m_regularTrail=cocos2d::CCNode::create(); layer->addChild(p->m_regularTrail);
        p->m_shipStreak=cocos2d::CCNode::create(); layer->addChild(p->m_shipStreak);
        return p;
    }
    void setColor(cocos2d::Color) {} void setSecondColor(cocos2d::Color) {}
};
struct GJBaseGameLayer {
    struct GameState {
        bool m_unkBool4=false,m_unkBool5=false;
        float m_unkInt13=0;
        cocos2d::CCPoint m_cameraPosition{},m_cameraPosition2{},m_cameraStepDiff{};
        float m_cameraZoom=1;
        void stopTweenAction(int) {}
    } m_gameState;
    int cameraUpdates=0;
    void cameraMoveX(float x,float,float,bool) { m_gameState.m_unkBool4=true; m_gameState.m_cameraPosition.x=x; }
    void cameraMoveY(float y,float,float,bool) {
        m_gameState.m_unkBool5=true; m_gameState.m_unkInt13=y; m_gameState.m_cameraPosition.y=y;
    }
    PlayerObject *m_player1=nullptr,*m_player2=nullptr;
    cocos2d::CCLayer *m_objectLayer=nullptr,*m_inShaderObjectLayer=nullptr,*m_aboveShaderObjectLayer=nullptr;
    inline static void(*cameraDispatch)(GJBaseGameLayer*,float)=nullptr;
    bool dispatching=false,throwCamera=false; int bgUpdates=0;
    void updateCameraBGArt(cocos2d::CCPoint,float){++bgUpdates;}
    void updateCamera(float dt) {
      if(cameraDispatch && !dispatching){
        dispatching=true;try{cameraDispatch(this,dt);}catch(...){dispatching=false;throw;}dispatching=false;return;
      }
      ++cameraUpdates;
      if(m_player1){
        m_gameState.m_cameraPosition=cocos2d::CCPoint{m_player1->getPosition().x-165,m_player1->getPosition().y-100};
        if(m_objectLayer)m_objectLayer->setPosition({-m_gameState.m_cameraPosition.x,-m_gameState.m_cameraPosition.y});
      }
      if(throwCamera)throw std::runtime_error("native camera failure");
    }
};
struct PlayLayer: GJBaseGameLayer {
    GJGameLevel* m_level=nullptr; float m_levelLength=1000,m_progressWidth=200,m_progressHeight=10;
    bool m_isTestMode=false,m_isPracticeMode=false,m_decimalPercentage=false;
    int m_currentProgress=0,newBest=7,savedPercentage=7;
    cocos2d::CCSprite* m_progressFill=nullptr; cocos2d::CCLabelBMFont* m_percentageLabel=nullptr;
    inline static PlayLayer* current=nullptr; static PlayLayer* get(){return current;}
    void updateProgressbar(){if(m_progressFill)m_progressFill->setTextureRect({0,0,0,m_progressHeight});if(m_percentageLabel)m_percentageLabel->setString("0%");}
    // No reset/death/completion/Attempt methods: production renderer must not
    // invoke them. An accidental gameplay dependency fails this compilation.
};
