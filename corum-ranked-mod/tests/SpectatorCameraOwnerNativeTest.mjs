import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root,p),'utf8');
const source=read('src/PlayLayer.cpp');
const evidence=JSON.parse(read('tests/fixtures/debug-spectate-ipados-beta27.json'));
function method(signature, from=0) {
    const start=source.indexOf(signature,from);assert(start>=0,signature);
    const open=source.indexOf('{',start);let depth=1,end=open+1;
    while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}
    return source.slice(start,end);
}

test('actual camera owner hook ends in recorded viewport, drives once, restores dummy and updates UI only',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-camera-owner-'));
    try {
        mkdirSync(join(dir,'Geode/binding'),{recursive:true});
        writeFileSync(join(dir,'Geode/Geode.hpp'),read('tests/helpers/SpectatorNativeDouble.hpp'));
        for(const name of ['PlayLayer','GJBaseGameLayer','PlayerObject','GJGameLevel','GhostTrailEffect','HardStreak','GameManager'])
            writeFileSync(join(dir,`Geode/binding/${name}.hpp`),'#include <Geode/Geode.hpp>\n');
        const methods=['bool spectatorCameraActive()','bool beginSpectatorPresentationFrame()', 'bool beginSpectatorWorldFrame()', 'void reconcileSpectatorWorldDriver()',
            'std::optional<CCPoint> spectatorNativeCameraTarget()','void logSpectatorNativeCamera(char const* stage)',
            'void applySpectatorProgressPresentation()','void updateProgressbar()',
            'void driveSpectatorCamera()','void ensureSpectatorCameraFrame(float dt)',
            'void cleanupSpectatorPresentation()'].map(s=>method(s)).join('\n');
        const hook=method('void updateCamera(float dt)',source.indexOf('class $modify(CorumRankedSpectatorCameraHook')).replace('void updateCamera(', 'void cameraHook(');
        const worldHook=method('void update(float dt)',source.indexOf('class $modify(CorumRankedSpectatorCameraHook')).replace('void update(', 'void worldHook(');
        const cpp=`
#include "${join(root,'src/spectator/CorumRemotePlayer.hpp')}"
#include "${join(root,'tests/helpers/InactiveReplayDouble.hpp')}"
#include "${join(root,'src/spectator/CorumSpectatorCamera.hpp')}"
#include "${join(root,'src/spectator/ScopedCameraTargetOverride.hpp')}"
#include "${join(root,'src/domain/SpectatorProgressPolicy.hpp')}"
#include "${join(root,'src/spectator/SpectatorWorldDriver.hpp')}"
#include "${join(root,'src/spectator/SpectatorTriggerScope.hpp')}"
#include "${join(root, 'src/domain/SpectatorNativeProbe.hpp')}"
#include "${join(root,'src/domain/SpectatorRestoreTimeline.hpp')}"
#include "${join(root,'src/spectator/SpectatorPlayerCapture.hpp')}"
#include <chrono>
namespace corum::ranked {struct SurvivalSpectatorRuntime {static SurvivalSpectatorRuntime& get(){static SurvivalSpectatorRuntime r;return r;}bool sceneLevel(int)const{return false;}replay::InactiveWorld* world()const{return nullptr;}bool publishing()const{return false;}bool canLaunch()const{return false;}template<class...T>bool arm(T...){return false;}template<class...T>bool capture(T const&...){return false;}template<class...T>void terminal(T const&...){}void sceneExited(){}bool exitRequested()const{return false;}};}

#include <memory>
#include <iostream>
#include <source_location>
using namespace geode::prelude;
namespace fmt { std::string format(char const* pattern,float value){
    char buf[40];snprintf(buf,sizeof(buf),std::string(pattern).find(".2")!=std::string::npos?"%.2f%%":"%.0f%%",value);return buf;
} }
namespace corum::ranked {
struct RankedRuntime {
    struct Match{std::optional<int> opponentProgress;bool spectatorLiveEnabled=true;};struct View{Match match;} data;
    bool spectator=true;static RankedRuntime& get(){static RankedRuntime r;return r;}
    bool isSpectating()const{return spectator;}View const& view()const{return data;}
};
struct SpectatorLiveRuntime {
    std::optional<SpectatorRemoteFrame> frame;mutable int reads=0;
    static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime r;return r;}
    auto presentationFrame()const{++reads;return frame;}auto worldFrame()const{++reads;return frame;}bool restoringWorld()const{return restore.active();}
    SpectatorRestoreTimeline restore;
    auto takeRestorationFrames(){return restore.takeBatch();}
    void restorationApplied(SpectatorRemoteFrame const& f,bool,double){restore.applied(f);}
    void requestSceneRestore(){}void noteWorldPartial(std::string){}

};
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
namespace debug { struct DebugSpectateRuntime {
    int m_levelId=88350599;int levelId()const{return m_levelId;}static DebugSpectateRuntime& get(){static DebugSpectateRuntime r;return r;}
    bool playbackLevel(int level)const{return level==88350599;}
}; }
#endif
}
struct ScopedPersistenceOwner{explicit ScopedPersistenceOwner(PlayLayer*){}};
struct ScopedRealGameplayEnd{bool& flag;bool previous;ScopedRealGameplayEnd(bool& f,bool b):flag(f),previous(f){flag=b;}~ScopedRealGameplayEnd(){flag=previous;}};
template<class T>struct FieldsAccess { std::unique_ptr<T> value=std::make_unique<T>();T* operator->(){return value.get();} };
struct CorumRankedPlayLayer: PlayLayer {
    corum::ranked::SpectatorLiveRuntime& spectatorTransport(){return corum::ranked::SpectatorLiveRuntime::get();}
    void survivalSpectatorFrame(float){}
    corum::ranked::SpectatorSceneContext survivalSpectatorSceneContext(){return {};}

 void syncSpectatorAudio(){}
 bool blockEntryGameplayUpdate(){return false;}
    struct Fields {struct Trace{unsigned frame=0;}spectatorSourceTrace;bool survivalLiveLevel=false,survivalReplayLevel=false;
#if !defined(CORUM_RANKED_DEBUG_SPECTATE)
 int levelId=123;
#endif

        bool spectatorScenePrepared=true,spectatorPresentationRetired=false,autoExitRequested=false;
        struct Safety{bool enabled=true;bool active()const{return enabled;}void resetDummy(){}}localSafety;
        corum::ranked::CorumRemotePlayer remotePlayer;
        corum::ranked::CorumSpectatorCamera spectatorCamera;
        std::optional<corum::ranked::SpectatorRemoteFrame> spectatorRenderedFrame;
        std::optional<unsigned> spectatorCameraFrameNumber,spectatorPresentationFrameNumber,spectatorWorldFrameNumber;
        bool spectatorWorldUpdateInProgress=false,spectatorWorldResetInProgress=false,spectatorCollisionChecked=false,spectatorWorldPoseApplied=false;
        corum::ranked::SpectatorWorldDriver spectatorWorldDriver;
        corum::ranked::SpectatorSceneContext sceneContext{"debug-spectate:server",1,0,88350599};
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        corum::ranked::SpectatorNativeProbe spectatorNativeProbe;
        bool debugPlaybackScene=true,cameraForensicFrame=false;
        int levelId=88350599,cameraForensicFrames=0;std::int64_t cameraForensicSequence=-1;
#endif
    };
    FieldsAccess<Fields> m_fields;bool watchable=true;
    bool watchableSpectatorScene(){return watchable;}
    bool prepareSpectatorScene(){return spectatorCameraActive();}
    bool observesSpectatorPlayer(PlayerObject*){return false;}
    bool tracesSpectatorPlayer(PlayerObject*){return false;}
    template<class...T>void traceSpectatorSource(T const&...){}
    void sampleSpectatorPresentation(){}void logSpectatorWorldDriver(){}void logSpectatorNativeProbe(){}
    ${methods}
    ${hook}
    ${worldHook}
    ${method('int checkCollisions(PlayerObject* player,float dt,bool ignoreDamage)')}
    ${method('void collisionCheckObjects(PlayerObject* player,gd::vector<GameObject*>* objects,int count,float dt)')}
};
void close(float a,float b,std::source_location loc=std::source_location::current()){if(std::abs(a-b)>=0.001f){std::cerr<<loc.line()<<": "<<a<<" != "<<b<<std::endl;std::abort();}}
int main(){
    using namespace corum::ranked;
    {
        CorumRankedPlayLayer scene;PlayerObject dummy,second;
        scene.m_player1=&dummy;scene.m_player2=&second;dummy.m_gameLayer=&scene;PlayLayer::current=&scene;
        GJGameLevel level;level.m_levelID=88350599;scene.m_level=&level;
        auto* world=CCLayer::create();geode::Ref<CCLayer> owner(world);scene.m_objectLayer=world;
        auto* shader=CCLayer::create();auto* above=CCLayer::create();
        geode::Ref<CCLayer> shaderOwner(shader),aboveOwner(above);
        scene.m_inShaderObjectLayer=shader;scene.m_aboveShaderObjectLayer=above;
        CCSprite fill;CCLabelBMFont label;scene.m_progressFill=&fill;scene.m_percentageLabel=&label;
        dummy.setPosition({11,22});dummy.m_positionX=11.123456789;dummy.m_positionY=22.987654321;
        dummy.m_lastPosition=CCPoint{5,6};dummy.visible=false;
        auto original=dummy.getPosition();auto x=dummy.m_positionX,y=dummy.m_positionY;
        scene.m_gameState.m_cameraPosition=CCPoint{150,20};world->setPosition({-150,-20});
        auto& live=SpectatorLiveRuntime::get();
        live.frame=SpectatorRemoteFrame{{"debug-spectate:server",1,0,88350599,"recording",1,1},18,SpectatorGhostPose{290.80777f,686.50934f,0,false,false},908};
        live.frame->pose->camera=SpectatorCameraOrigin{125.80777f,532.2815f};
        scene.m_fields->remotePlayer.update(&scene,true,live.frame);
        auto* remote=scene.m_fields->remotePlayer.player();assert(remote && remote!=&dummy && remote!=&second);
        // Reproduce beta27's actual order: recorded target THEN vanilla dummy.
        scene.m_fields->spectatorCamera.update(&scene,true,live.frame->context,live.frame);
        close(scene.m_gameState.m_cameraPosition.x,125.80777f);
        scene.GJBaseGameLayer::updateCamera(0.016f);
        assert(scene.m_gameState.m_cameraPosition.x!=125.80777f);
        auto wrong=CCPointApplyAffineTransform(remote->getPosition(),world->nodeToWorldTransform());
        assert(std::abs(wrong.x-165)>100);
        scene.m_fields->spectatorCamera.cleanup(&scene);
        scene.m_fields->spectatorCamera.attach(&scene);
        GJBaseGameLayer::cameraDispatch=[](GJBaseGameLayer* layer,float dt){static_cast<CorumRankedPlayLayer*>(layer)->cameraHook(dt);};
        GJBaseGameLayer::worldDispatch=[](GJBaseGameLayer* layer,float dt){static_cast<CorumRankedPlayLayer*>(layer)->worldHook(dt);};
        auto frame=[&](){++CCDirector::sharedDirector()->totalFrames;if(live.frame)live.frame->publisherElapsedMs+=16;};
        auto seed=live.frame;seed->sequence=17;seed->publisherElapsedMs=858;scene.m_fields->spectatorWorldDriver.apply(&scene,true,seed);
        ${evidence.frames.map(f=>`
        frame();live.frame->sequence=${f.sequence};live.frame->publisherElapsedMs=${f.elapsedMs};
        live.frame->pose->x=${f.x}f;live.frame->pose->y=${f.y}f;
        live.frame->pose->camera=SpectatorCameraOrigin{${f.cameraX}f,${f.cameraY}f};
        {
            auto calls=scene.cameraUpdates,reads=live.reads;
            scene.GJBaseGameLayer::update(0.016f);scene.ensureSpectatorCameraFrame(0.016f);scene.GJBaseGameLayer::updateCamera(0.016f);
            assert(scene.cameraUpdates==calls+1 && live.reads==reads+1);
            close(scene.m_gameState.m_cameraPosition.x,${f.cameraX}f);close(scene.m_gameState.m_cameraPosition.y,${f.cameraY}f);
            for(auto* node:{world,shader,above}){
                auto origin=CCPointApplyAffineTransform({${f.cameraX}f,${f.cameraY}f},node->nodeToWorldTransform());close(origin.x,0);close(origin.y,0);
            }
            auto screen=CCPointApplyAffineTransform(remote->getPosition(),world->nodeToWorldTransform());close(screen.x,165);
            assert(scene.m_fields->remotePlayer.player()==remote);
        }
        `).join('\n')}
        // A stopped native lifecycle still gets one owned camera update, never two.
        frame();auto calls=scene.cameraUpdates;scene.ensureSpectatorCameraFrame(0.016f);scene.ensureSpectatorCameraFrame(0.016f);assert(scene.cameraUpdates==calls+1);
        // Parent transform, zoom/rotation and aliased shader node: rendered origin still maps to screen zero.
        CCNode parent;parent.setPosition({37,42});parent.setScale(1.2f);parent.setRotation(7);parent.addChild(world);
        world->setScale(1.5f);world->setRotation(13);scene.m_inShaderObjectLayer=world;
        frame();scene.ensureSpectatorCameraFrame(0.016f);
        auto origin=CCPointApplyAffineTransform({live.frame->pose->camera->x,live.frame->pose->camera->y},world->nodeToWorldTransform());close(origin.x,0);close(origin.y,0);
        world->removeFromParentAndCleanup(true);world->setScale(1);world->setRotation(0);scene.m_inShaderObjectLayer=shader;
        // Missing recorded camera borrows dummy position only during native calculation.
        frame();++live.frame->sequence;live.frame->pose->camera.reset();live.frame->pose->x=500;live.frame->pose->y=600;
        scene.ensureSpectatorCameraFrame(0.016f);close(scene.m_gameState.m_cameraPosition.x,335);close(scene.m_gameState.m_cameraPosition.y,500);
        close(dummy.getPosition().x,500);close(dummy.getPosition().y,600);assert(!dummy.visible);
        original=dummy.getPosition();x=dummy.m_positionX;y=dummy.m_positionY;
        // Early return and throwing native code restore exact independent coordinate mirrors.
        auto early=[&](){ScopedCameraTargetOverride scoped(&dummy,CCPoint{999,888});return;};early();assert(dummy.m_positionX==x && dummy.getPosition().x==original.x);
        frame();scene.throwCamera=true;bool thrown=false;
        try{scene.ensureSpectatorCameraFrame(0.016f);}catch(std::runtime_error const&){thrown=true;}
        assert(thrown && dummy.m_positionX==x && dummy.m_positionY==y && dummy.getPosition().x==original.x);scene.throwCamera=false;
        frame();live.frame->pose->cameraInvalid=true;scene.ensureSpectatorCameraFrame(0.016f);close(scene.m_gameState.m_cameraPosition.x,335);
        frame();live.frame.reset();scene.ensureSpectatorCameraFrame(0.016f);close(scene.m_gameState.m_cameraPosition.x,335);
        // Progress/UI uses remote rendered frame. Native dummy journal/completion never changes.
        live.frame=SpectatorRemoteFrame{{"debug-spectate:server",1,0,88350599,"recording",1,1},30,SpectatorGhostPose{500,600,0,false,false},5000};
        live.frame->pose->camera=SpectatorCameraOrigin{335,500};frame();scene.ensureSpectatorCameraFrame(0.016f);scene.updateProgressbar();
        close(fill.rect.width,100);assert(label.text=="50%");assert(scene.m_currentProgress==0 && scene.newBest==7 && scene.savedPercentage==7);
        level.m_timestamp=2400;live.frame->pose->x=100;++live.frame->sequence;frame();live.frame->publisherElapsedMs=5000;scene.ensureSpectatorCameraFrame(0.016f);scene.updateProgressbar();close(fill.rect.width,100);
#if !defined(CORUM_RANKED_DEBUG_SPECTATE)
        RankedRuntime::get().data.match.opponentProgress=73;scene.updateProgressbar();close(fill.rect.width,146);
        RankedRuntime::get().data.match.opponentProgress.reset();
#endif
        level.m_timestamp=0;live.frame->pose->finished=true;live.frame->sequence=32;frame();scene.ensureSpectatorCameraFrame(0.016f);scene.updateProgressbar();close(fill.rect.width,200);assert(label.text=="100%");
        assert(scene.m_currentProgress==0 && scene.newBest==7 && scene.savedPercentage==7 && !dummy.visible);
        // Ordinary Ranked, Placement and user Practice have unchanged native camera/UI behavior.
        for(int ordinary=0;ordinary<3;++ordinary){
            scene.m_fields->localSafety.enabled=true;scene.m_isPracticeMode=false;scene.m_isTestMode=false;
            if(ordinary==0)scene.m_fields->localSafety.enabled=false;
            if(ordinary==1)scene.m_isPracticeMode=true;
            if(ordinary==2)scene.m_isTestMode=true;
            calls=scene.cameraUpdates;scene.GJBaseGameLayer::updateCamera(0.016f);scene.GJBaseGameLayer::updateCamera(0.016f);assert(scene.cameraUpdates==calls+2);
            scene.updateProgressbar();close(fill.rect.width,0);assert(label.text=="0%");
        }
        scene.cleanupSpectatorPresentation();GJBaseGameLayer::cameraDispatch=nullptr;GJBaseGameLayer::worldDispatch=nullptr;PlayLayer::current=nullptr;
        CCObject::drain();
    }
    CCObject::drain();assert(CCObject::alive==0);
    std::cout<<"recorded viewport/native hook/dummy restoration/UI safety passed\\n";
}
`;
        writeFileSync(join(dir,'test.cpp'),cpp);
        for(const debug of [false,true]){
            const binary=join(dir,'test');
            execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(debug?['-DCORUM_RANKED_DEBUG_SPECTATE=1']:[]),'-I',dir,
                join(root,'src/spectator/CorumRemotePlayer.cpp'),join(root,'src/spectator/CorumSpectatorCamera.cpp'),join(root,'src/spectator/SpectatorWorldDriver.cpp'),join(root,'src/spectator/SpectatorPlayerCapture.cpp'),join(dir,'test.cpp'),'-o',binary],{stdio:'pipe'});
            assert.match(execFileSync(binary,{encoding:'utf8'}),/UI safety passed/);
        }
    } finally {rmSync(dir,{recursive:true,force:true});}
});
