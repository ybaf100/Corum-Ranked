import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));

// Compile the COMPLETE production renderer translation unit. These observable
// native lifecycle doubles verify ownership/control flow, not GD ABI or visuals.
test('production Remote Player owns a separate visual and cleans up its native lifecycle', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-remote-native-'));
    try {
        mkdirSync(join(dir, 'Geode/binding'), { recursive: true });
        const header = `
#pragma once
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
namespace cocos2d {
struct CCSize { float width=0, height=0; };
struct CCPoint {
    float x=0, y=0;
    CCPoint& operator=(CCSize const& s) { x=s.width; y=s.height; return *this; }
};
inline constexpr CCPoint CCPointZero{};
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
struct CCNode: CCObject {
    CCNode* parent=nullptr; std::vector<CCNode*> children;
    CCPoint position{}; float rotation=0; bool visible=true; int z=0;
    int positionWrites=0, rotationWrites=0, unschedules=0;
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
    int getZOrder() const { return z; }
};
struct CCLayer: CCNode { static CCLayer* create() { auto* p=new CCLayer; p->autorelease(); return p; } };
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
namespace log { template<class... T> void info(T...) {} template<class... T> void warn(T...) {} }
namespace prelude { using namespace cocos2d; using geode::Ref; namespace log=geode::log; }
}
struct GJGameLevel { int m_levelID=123; };
struct PlayLayer;
struct PlayerObject: cocos2d::CCNode {
    inline static int creates=0; inline static bool fail=false;
    bool m_playEffects=true,m_isHidden=false,m_isDead=false;
    cocos2d::CCPoint m_startPosition{},m_lastPosition{};
    double m_positionX=0,m_positionY=0;
    cocos2d::CCNode *m_shipStreak=nullptr,*m_regularTrail=nullptr,*m_waveTrail=nullptr,*m_ghostTrail=nullptr,
      *m_playerGroundParticles=nullptr,*m_trailingParticles=nullptr,*m_shipClickParticles=nullptr,
      *m_vehicleGroundParticles=nullptr,*m_ufoClickParticles=nullptr,*m_robotBurstParticles=nullptr,
      *m_dashParticles=nullptr,*m_swingBurstParticles1=nullptr,*m_swingBurstParticles2=nullptr,
      *m_landParticles0=nullptr,*m_landParticles1=nullptr;
    static PlayerObject* create(int,int,PlayLayer*,cocos2d::CCLayer* layer,bool) {
        ++creates; if(fail)return nullptr;
        auto* p=new PlayerObject; p->autorelease();
        p->m_regularTrail=cocos2d::CCNode::create(); layer->addChild(p->m_regularTrail);
        p->m_shipStreak=cocos2d::CCNode::create(); layer->addChild(p->m_shipStreak);
        return p;
    }
    void setColor(cocos2d::Color) {} void setSecondColor(cocos2d::Color) {}
};
struct PlayLayer {
    cocos2d::CCLayer* m_objectLayer=nullptr; PlayerObject* m_player1=nullptr;
    PlayerObject* m_player2=nullptr; GJGameLevel* m_level=nullptr;
    // No reset/death/completion/Attempt methods: production renderer must not
    // invoke them. An accidental gameplay dependency fails this compilation.
};
`;
        writeFileSync(join(dir, 'Geode/Geode.hpp'), header);
        for (const name of ['PlayLayer', 'PlayerObject', 'GJGameLevel', 'GhostTrailEffect', 'HardStreak']) {
            writeFileSync(join(dir, `Geode/binding/${name}.hpp`), '#include <Geode/Geode.hpp>\n');
        }
        // Verify the double actually reproduces the native overload failure.
        writeFileSync(join(dir, 'ambiguous.cpp'), '#include <Geode/Geode.hpp>\nvoid bad() { cocos2d::CCPoint p; p = {1, 2}; }\n');
        assert.throws(() => execFileSync('g++', ['-std=c++23', '-I', dir, '-c', join(dir, 'ambiguous.cpp'),
            '-o', join(dir, 'ambiguous.o')], { stdio: 'pipe' }), error => /ambiguous/.test(error.stderr.toString()));
        writeFileSync(join(dir, 'test.cpp'), `
#include "${join(root, 'src/spectator/CorumRemotePlayer.hpp')}"
#include <iostream>
using namespace corum::ranked; using namespace cocos2d;
int main() {
  {
    GJGameLevel level; PlayerObject local, other; PlayLayer layer;
    layer.m_level=&level; layer.m_player1=&local; layer.m_player2=&other;
    auto* world=CCLayer::create(); geode::Ref<CCLayer> worldOwner(world); layer.m_objectLayer=world;
    local.setPosition({11,22}); local.setRotation(33);
    SpectatorRemoteFrame f{{"match",1,0,123,"attempt",1,1},0,std::nullopt};
    CorumRemotePlayer remote;
    remote.update(&layer,false,f); assert(!remote.player() && PlayerObject::creates==0);
    remote.update(&layer,true,f); auto* first=remote.player(); assert(first && first!=&local && first!=&other && !first->visible);
    CCObject::drain();
    for(int i=0;i<1000;++i)remote.update(&layer,true,f);
    assert(PlayerObject::creates==1 && remote.player()==first);
    f.pose=SpectatorGhostPose{50,70,90,false,false}; remote.update(&layer,true,f);
    assert(first->position.x==50 && first->position.y==70 && first->rotation==90 && first->visible);
    assert(first->unschedules>0 && !first->m_regularTrail->visible);
    assert(local.position.x==11 && local.position.y==22 && local.rotation==33 && local.positionWrites==1);
    f.pose->dead=true; ++f.sequence; remote.update(&layer,true,f); assert(!first->visible && first->m_isDead);
    f.pose->dead=false; f.pose->x=200; ++f.sequence; remote.update(&layer,true,f);
    assert(!first->visible && first->position.x==50);
    geode::Ref<PlayerObject> old(first); ++f.context.epoch;
    remote.update(&layer,true,f); assert(remote.player()!=first && !first->parent && !first->m_regularTrail);
    auto* second=remote.player(); f.pose->finished=true; f.pose->x=300; ++f.sequence;
    remote.update(&layer,true,f); assert(second->visible && second->position.x==300);
    f.pose->x=400; ++f.sequence; remote.update(&layer,true,f); assert(second->position.x==300);
    f.pose.reset(); remote.update(&layer,true,f); assert(!second->visible);
    remote.update(&layer,false,f); assert(!remote.player() && world->children.empty());
    remote.cleanup(); remote.cleanup();
    PlayerObject::fail=true; ++f.context.generation; remote.update(&layer,true,f);
    auto creates=PlayerObject::creates; for(int i=0;i<20;++i)remote.update(&layer,true,f);
    assert(PlayerObject::creates==creates && !remote.player() && world->children.empty());
    PlayerObject::fail=false; ++f.context.generation; remote.update(&layer,true,f); assert(remote.player());
    CCObject::drain();
    // Simulated parent teardown before wrapper cleanup; retained visual/root
    // ownership prevents use-after-free. Cleanup removes all owned companions.
    worldOwner=nullptr; layer.m_objectLayer=nullptr; remote.cleanup(); CCObject::drain();
    assert(local.position.x==11 && local.rotation==33);
  }
  CCObject::drain(); assert(CCObject::alive==0);
  std::cout << "production remote lifecycle/pose/ownership scenarios passed\\n";
}
`);
        const binary = join(dir, 'test');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
            join(root, 'src/spectator/CorumRemotePlayer.cpp'), join(dir, 'test.cpp'), '-o', binary], { stdio: 'pipe' });
        assert.match(execFileSync(binary, { encoding: 'utf8' }), /scenarios passed/);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
