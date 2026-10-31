#pragma once
#include <algorithm>
#include <cassert>
#include <set>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
using GLubyte=unsigned char;
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
struct Color { unsigned char r=255,g=255,b=255; };
using ccColor3B=Color;inline Color ccc3(int r,int g,int b){return {static_cast<unsigned char>(r),static_cast<unsigned char>(g),static_cast<unsigned char>(b)};}
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
    Color color{};
    void setColor(Color c){color=c;}Color getColor()const{return color;}
    void setOpacity(unsigned char c){opacity=c;}unsigned char getOpacity()const{return opacity;}
    void runAction(CCFadeOut*) { opacity=0; }
    void setScaleY(float s) { scaleY=s; } float getScaleY()const{return scaleY;}
    void setScaleX(float s) { scaleX=s; } float getScaleX()const{return scaleX;}
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
    auto const& getChildrenExt()const{return children;}
    unsigned getChildrenCount()const{return static_cast<unsigned>(children.size());}
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
struct CCSprite: CCNode {
    CCRect rect{};void* image=nullptr;
    static CCSprite* create(){auto* s=new CCSprite;s->autorelease();return s;}
    void setTextureRect(CCRect r){rect=r;}void* displayFrame(){return image;}void setDisplayFrame(void* v){image=v;}
};
struct CCMotionStreak:CCNode {int resets=0,updates=0;void reset(){++resets;}void update(float){++updates;}};
struct CCParticleSystemQuad:CCNode {
    bool active=true,autoRemove=false;unsigned count=0,total=100;int updates=0,resets=0;
    bool isActive()const{return active;}void resetSystem(){active=true;count=0;++resets;}void stopSystem(){active=false;}
    void setAutoRemoveOnFinish(bool a){autoRemove=a;}unsigned getTotalParticles()const{return total;}unsigned getParticleCount()const{return count;}
    void update(float){++updates;if(active)count=std::min(total,count+1);else if(count)--count;}
};
struct CCArray{unsigned size=0;unsigned count()const{return size;}};
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
namespace gd {template<class T>using vector=std::vector<T>;}
enum class IconType{Cube,Ship,Ball,Ufo,Wave,Robot,Spider,Swing,Jetpack,Special=99};
struct GameManager{
    static GameManager* sharedState(){static GameManager g;return &g;}int countForType(IconType)const{return 500;}
    int getPlayerFrame(){return 17;}int getPlayerShip(){return 18;}int getPlayerBall(){return 19;}int getPlayerBird(){return 20;}
    int getPlayerDart(){return 21;}int getPlayerRobot(){return 22;}int getPlayerSpider(){return 23;}int getPlayerSwing(){return 24;}
    int getPlayerColor(){return 0;}int getPlayerColor2(){return 1;}int getPlayerGlowColor(){return 2;}bool getPlayerGlow(){return true;}
    int getPlayerStreak(){return 3;}cocos2d::Color colorForIdx(int i){return cocos2d::ccc3(100+i,150,200);}
};
struct GhostTrailEffect:cocos2d::CCNode{};
struct HardStreak:cocos2d::CCNode{cocos2d::CCArray points; cocos2d::CCArray* m_pointArray=&points;int updates=0;
    void resumeStroke(){}void reset(){points.size=0;}void addPoint(cocos2d::CCPoint){++points.size;}void updateStroke(float){++updates;}};
struct GameObject{bool m_isTrigger=false;};
struct GJGameLevel { int m_levelID=123,m_timestamp=0; };
struct PlayLayer;
struct GJBaseGameLayer;
struct PlayerObject: cocos2d::CCNode {
    inline static int creates=0; inline static bool fail=false;
    bool m_playEffects=true,m_isHidden=false,m_isDead=false;
    bool m_isShip=false,m_isBall=false,m_isBird=false,m_isDart=false,m_isRobot=false,m_isSpider=false,m_isSwing=false,m_isUpsideDown=false;
    float m_vehicleSize=1; int nativeModeCalls=0,miniCalls=0,frame=0;
    bool m_hasGlow=false,m_hasCustomGlowColor=false;int m_playerStreak=1,streakSetups=0,iconId=1,iconUpdates=0,glowUpdates=0;
    cocos2d::Color m_playerColor1{},m_playerColor2{},m_glowColor{};cocos2d::CCSprite* m_iconSprite=nullptr;
    void enableCustomGlowColor(cocos2d::Color c){m_glowColor=c;m_hasCustomGlowColor=true;}void updatePlayerGlow(){++glowUpdates;}void updateGlowColor(){}
    void setupStreak(){++streakSetups;}
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
    void updatePlayerFrame(int id){frame=0;iconId=id;++iconUpdates;} void updatePlayerShipFrame(int id){frame=1;iconId=id;++iconUpdates;}
    void updatePlayerRollFrame(int id){frame=2;iconId=id;++iconUpdates;} void updatePlayerBirdFrame(int id){frame=3;iconId=id;++iconUpdates;}
    void updatePlayerDartFrame(int id){frame=4;iconId=id;++iconUpdates;} void updatePlayerRobotFrame(int id){frame=5;iconId=id;++iconUpdates;}
    void updatePlayerSpiderFrame(int id){frame=6;iconId=id;++iconUpdates;} void updatePlayerSwingFrame(int id){frame=7;iconId=id;++iconUpdates;}
    cocos2d::CCPoint m_startPosition{},m_lastPosition{};
    double m_positionX=0,m_positionY=0;
    int gameplayPositionWrites=0,physicsUpdates=0,rotationUpdates=0,spiderJumps=0;
    bool m_wasTeleported=false;int m_followRelated=0;float m_unk838=0;std::vector<float> m_playerFollowFloats=std::vector<float>(200);
    GJBaseGameLayer* m_gameLayer=nullptr;
    void playerTeleported(){m_wasTeleported=true;}
    virtual void update(float){++physicsUpdates;} virtual void updateRotation(float){++rotationUpdates;}
    virtual void spiderTestJump(bool){++spiderJumps;}
    virtual void updateSpecial(float dt){m_unk838+=dt;if(m_unk838>=.01f){++m_followRelated;m_unk838-=.01f;}m_playerFollowFloats[m_followRelated%200]=position.y;}
    virtual void setPosition(cocos2d::CCPoint const& p){++gameplayPositionWrites;cocos2d::CCNode::setPosition(p);m_positionX=p.x;m_positionY=p.y;}
    virtual void setRotation(float r){cocos2d::CCNode::setRotation(r);}
    virtual void setVisible(bool b){cocos2d::CCNode::setVisible(b);}
    cocos2d::CCMotionStreak *m_shipStreak=nullptr,*m_regularTrail=nullptr;
    HardStreak* m_waveTrail=nullptr;GhostTrailEffect* m_ghostTrail=nullptr;
    cocos2d::CCParticleSystemQuad *m_playerGroundParticles=nullptr,*m_trailingParticles=nullptr,*m_shipClickParticles=nullptr,
      *m_vehicleGroundParticles=nullptr,*m_ufoClickParticles=nullptr,*m_robotBurstParticles=nullptr,
      *m_dashParticles=nullptr,*m_swingBurstParticles1=nullptr,*m_swingBurstParticles2=nullptr,
      *m_landParticles0=nullptr,*m_landParticles1=nullptr;
    static PlayerObject* create(int,int,PlayLayer*,cocos2d::CCLayer* layer,bool) {
        ++creates; if(fail)return nullptr;
        auto* p=new PlayerObject; p->autorelease();
        p->m_mainLayer=cocos2d::CCNode::create(); p->addChild(p->m_mainLayer);
        p->m_regularTrail=new cocos2d::CCMotionStreak;p->m_regularTrail->autorelease();layer->addChild(p->m_regularTrail);
        p->m_waveTrail=new HardStreak;p->m_waveTrail->autorelease();layer->addChild(p->m_waveTrail);
        p->m_iconSprite=cocos2d::CCSprite::create();p->addChild(p->m_iconSprite);
        for(auto** slot:{&p->m_playerGroundParticles,&p->m_trailingParticles,&p->m_shipClickParticles,&p->m_vehicleGroundParticles,
          &p->m_ufoClickParticles,&p->m_robotBurstParticles,&p->m_swingBurstParticles1,&p->m_swingBurstParticles2,&p->m_landParticles0,&p->m_landParticles1,&p->m_dashParticles}){
          *slot=new cocos2d::CCParticleSystemQuad;(*slot)->autorelease();layer->addChild(*slot);
        }
        p->m_shipStreak=new cocos2d::CCMotionStreak;p->m_shipStreak->autorelease();layer->addChild(p->m_shipStreak);
        return p;
    }
    void setColor(cocos2d::Color c){m_playerColor1=c;} void setSecondColor(cocos2d::Color c){m_playerColor2=c;}
};
struct GJBaseGameLayer {
    struct GameState {
        bool m_unkBool4=false,m_unkBool5=false;
        float m_unkInt13=0;
        cocos2d::CCPoint m_cameraPosition{},m_cameraPosition2{},m_cameraStepDiff{};
        float m_cameraZoom=1,m_cameraAngle=0,m_targetCameraZoom=1,m_targetCameraAngle=0;double m_levelTime=0;
        void stopTweenAction(int) {}
    } m_gameState;
    int cameraUpdates=0,worldUpdates=0,collisionQueries=0,triggerObjects=0;
    bool damageIgnored=false;inline static gd::vector<GameObject*> nativeObjects;
    virtual int checkCollisions(PlayerObject* p,float dt,bool ignore){++collisionQueries;damageIgnored=ignore;collisionCheckObjects(p,&nativeObjects,static_cast<int>(nativeObjects.size()),dt);return 17;}
    virtual void collisionCheckObjects(PlayerObject*,gd::vector<GameObject*>* objects,int n,float){if(objects)triggerObjects+=n;}
    inline static void(*worldDispatch)(GJBaseGameLayer*,float)=nullptr;
    inline static void(*nativeWorldObserver)(GJBaseGameLayer*,float)=nullptr;
    bool worldDispatching=false;
    void update(float dt){
      if(worldDispatch && !worldDispatching){worldDispatching=true;try{worldDispatch(this,dt);}catch(...){worldDispatching=false;throw;}worldDispatching=false;return;}
      m_gameState.m_levelTime+=dt;++worldUpdates;if(nativeWorldObserver)nativeWorldObserver(this,dt);else updateCamera(dt);
    }
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
    bool m_isTestMode=false,m_isPracticeMode=false,m_decimalPercentage=false,m_isPaused=false,m_started=false,m_inResetDelay=true;
    int m_currentProgress=0,newBest=7,savedPercentage=7,resets=0;
    void resetLevel(){++resets;}
    cocos2d::CCSprite* m_progressFill=nullptr; cocos2d::CCLabelBMFont* m_percentageLabel=nullptr;
    inline static PlayLayer* current=nullptr; static PlayLayer* get(){return current;}
    void updateProgressbar(){if(m_progressFill)m_progressFill->setTextureRect({0,0,0,m_progressHeight});if(m_percentageLabel)m_percentageLabel->setString("0%");}
    // No reset/death/completion/Attempt methods: production renderer must not
    // invoke them. An accidental gameplay dependency fails this compilation.
};

// Explicit native event boundary ports; the separate world-event tests execute their production policy.
namespace corum::ranked {struct SpectatorRemoteFrame; inline void prepareNativeWorldEvents(::PlayLayer*,SpectatorRemoteFrame const&){} inline void applyDueNativeRandomEvents(::PlayLayer*){} }
