import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
function method(source, name) {
    const start = source.indexOf(name); assert(start >= 0, name);
    const open = source.indexOf('{', start); let depth = 1, end = open + 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}
for (const debugOn of [false, true]) test(`production audio handoff + retained lobby lifecycle (Debug ${debugOn ? 'ON' : 'OFF'})`, () => {
    const play = read('src/PlayLayer.cpp'), audio = read('src/RankedAudioManager.cpp'), popup = read('src/RankedPopup.cpp');
    const init = method(play, 'bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects)');
    const begin = init.indexOf('        // Establish authority BEFORE');
    const end = init.indexOf('        if (m_fields->rankedLevel) {\n            if (auto const qualifying');
    assert(begin >= 0 && end > begin);
    const cpp = `
#include <cassert>
#include <chrono>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include "${join(root, 'src/domain/RankedMode.hpp')}"
namespace geode::log {template<class... T> void info(T...) {}}
using namespace geode;
namespace fmt {template<class... T> std::string format(T...) {return "key";}}
struct FMODAudioEngine {static inline bool voice=true;static inline int stops=0;
 static FMODAudioEngine* sharedEngine(){static FMODAudioEngine e;return &e;}
 void stopAllMusic(bool){voice=false;++stops;}};
struct GameManager {static inline int menus=0;static GameManager* sharedState(){static GameManager g;return &g;}
 void playMenuMusic(){++menus;}};
namespace corum::ranked {
struct RankedVolumeControl {static RankedVolumeControl& get(){static RankedVolumeControl c;return c;}unsigned long beginSession(){return 1;}};
enum class RankedAudioMode {Silent,Lobby,LobbyPromotion,LobbyDemotionWarning,ResultWin,ResultLose,Round3DeathmatchWait,Round12};
struct TransitionTrace {static TransitionTrace& get(){static TransitionTrace t;return t;}void mark(char const*){}};
struct RankedAudioManager {
 struct PreparedChannel{struct Channel{void stop(){}};Channel* channel=nullptr;bool failed=false;};
 std::unordered_map<std::string,PreparedChannel> m_preparedChannels;
 std::set<std::string> m_consumedPreparedKeys;
 void stopPreparedChannels(){m_preparedChannels.clear();}
 void releaseMusicGroup(){}
 RankedAudioMode m_desiredMode=RankedAudioMode::Lobby;bool m_switchPending=false,m_ownedFadeOutPending=false,m_playbackVerifyPending=false;
 RankedAudioMode m_requestedMode=RankedAudioMode::Lobby;std::set<std::string> m_failedPlaybackKeys;
 std::chrono::steady_clock::time_point m_audioStartBeganAt{};
 bool m_ownsMusicChannel=true;std::string m_playingKey="lobby";int m_playbackRetryCount=0,channels=1,creations=1,fadeouts=0;
 static RankedAudioManager& get(){static RankedAudioManager a;return a;}
 struct Resource{std::string key;};Resource resource;
 bool enabled()const{return true;}bool resourcesReady()const{return true;}
 RankedAudioMode availableMode(RankedAudioMode mode)const{return mode;}
 Resource* resourceForMode(RankedAudioMode mode){if(mode==RankedAudioMode::Silent)return nullptr;resource.key=std::to_string(int(mode));return &resource;}
 bool ownedChannelPlaying()const{return channels==1;}
 void cancelPendingAudioStart(){}void stopOwnedAudio(){channels=0;m_ownsMusicChannel=false;++fadeouts;}
 void requestAudioSwitch(){stopOwnedAudio();if(m_desiredMode!=RankedAudioMode::Silent){channels=1;m_ownsMusicChannel=true;++creations;m_playingKey=resourceForMode(m_desiredMode)->key;}}
 void configure(int){}void tick(){}void prewarmReadySounds(){}
 void setMode(RankedAudioMode);void fadeOutForGameplay();void restoreMenuMusic();
};
${['void RankedAudioManager::setMode(', 'void RankedAudioManager::fadeOutForGameplay()', 'void RankedAudioManager::restoreMenuMusic()'].map(n => method(audio,n)).join('\n')}
enum class RuntimeStage { AwaitingUpdate,Ready,Matched};
struct RankedRuntime {
 struct Result{std::string stageGrade="A";int stageIndex=0;};
 struct View{RuntimeStage stage=RuntimeStage::Ready;int client=0;
  struct{bool active=false;}promotion;bool demotionWarning=false;
  struct{std::string state="ROUND_PLAYING",matchId="match",winnerSide="P1",side="P1";bool placement=false;int roundNumber=1;}match;
  struct{std::string runId="run";std::vector<Result> stageResults;}placement;}data;
 static RankedRuntime& get(){static RankedRuntime r;return r;}View const& view()const{return data;}
 void tick(){}void setSpectatorLocalTransportBlocked(void*,bool){}
};
using RuntimeView = RankedRuntime::View;
}
struct GJGameLevel{};
struct PlayLayer {static inline bool succeeds=true;bool init(GJGameLevel*,bool,bool){return succeeds;}};
struct ScopedPersistenceOwner {explicit ScopedPersistenceOwner(void*){}};
struct DebugRuntime{void exitScene(){}};
struct CorumRankedPlayLayer: PlayLayer {
 struct Fields{bool rankedLevel=false,rankedInitialized=false,debugRecordScene=false,debugPlaybackScene=false;int levelId=88350599;}fields;
 Fields* m_fields=&fields;void syncSpectatorLocalSafety(){}
 ${method(play,'void acquireGameplayAudio()')}
 bool init(GJGameLevel* level,bool useReplay,bool dontCreateObjects){
  auto& runtime=corum::ranked::RankedRuntime::get();
  [[maybe_unused]] DebugRuntime debug;
  ${init.slice(begin,end)}
  return true;
 }
};
struct Menu{int m_eState=0;bool touch=true;void setTouchEnabled(bool value){touch=value;}bool isTouchEnabled(){return touch;}};
namespace corum::ranked::ui {void cancelMenuTouch(Menu*){}}
struct Handler{int m_nPriority=0;};
struct Dispatcher{Handler* findHandler(Menu*){return nullptr;}};
struct CCDirector{Dispatcher d;static CCDirector* sharedDirector(){static CCDirector c;return &c;}Dispatcher* getTouchDispatcher(){return &d;}};
struct CCLayerColor{virtual ~CCLayerColor()=default;virtual void onEnter(){}};
#define schedule_selector(x) 0
using namespace corum::ranked;
struct CorumRankedLayer: CCLayerColor {
 enum class Page{Live,Profile};Page m_page=Page::Live;
 bool m_rankedChildScenePushed=true,m_enteringSongGate=false,m_enteringLevel=false;
 corum::ranked::ModeLobbyTransition m_modeTransition;
 bool m_returnToModesAfterQueue=false,m_uiFadingOut=false,m_lobbyPanel=true;
 int modeReturns=0;
 void animateModeLobby(corum::ranked::RankedMode,bool opening){assert(!opening);++modeReturns;m_modeTransition.finish();}
 void* m_rankTransitionLayer=nullptr;
 void* m_resultScoreLayer=nullptr;bool m_resultScorePending=false;
 std::string m_stageResultAudioPlayedKey;Menu* m_menu=nullptr;
 Menu* m_settingsMenu=nullptr;unsigned long m_volumeSession=0;
 void setKeypadEnabled(bool){}void schedule(int,float){}
 bool showResourceGate()const{return false;}void syncResources(){}void syncMapDetails(){}void syncPhase(){}void updateAutomation(){}
 std::string presentationKey()const{return "lobby";}bool prepareUiTransition(std::string const&){return false;}
 void render(){}void applyFadeInIfNeeded(){}void updateDynamicUi(){}void maybeEnterLevel(){}
 ${method(popup,'void resumeModeSelectionAfterQueue(')}
 ${method(popup,'void syncAudioMode()')}
 ${method(popup,'void refresh(float)')}
 ${method(popup,'void onEnter() override')}
};
int main(){
 auto& audio=RankedAudioManager::get();auto& runtime=RankedRuntime::get();GJGameLevel level;
 // LevelInfo preparation never calls the gameplay handoff. Failed init keeps
 // the already owned channel; only a successfully initialized Corum scene stops it.
 CorumRankedPlayLayer scene;scene.fields.rankedLevel=true;
 PlayLayer::succeeds=false;assert(!scene.init(&level,false,false));assert(audio.channels==1 && FMODAudioEngine::stops==0);
 PlayLayer::succeeds=true;assert(scene.init(&level,false,false));assert(audio.channels==0 && FMODAudioEngine::voice);
 CorumRankedLayer lobby;lobby.onEnter();assert(audio.channels==1 && audio.m_desiredMode==RankedAudioMode::Lobby);
 auto creations=audio.creations;for(int i=0;i<20;++i)lobby.refresh(0);assert(audio.creations==creations && audio.channels==1);
 // A confirmed queue departure runs the production mode-return hook without
 // duplicating the lobby audio channel. ModeLobbyNativeTest checks its actions.
 lobby.m_modeTransition.recoverSolo();lobby.m_returnToModesAfterQueue=true;
 lobby.refresh(0);assert(lobby.modeReturns==1&&!lobby.m_modeTransition.selected());
 assert(audio.creations==creations&&audio.channels==1);
 for(int kind=0;kind<2;++kind){
  CorumRankedPlayLayer debug;debug.fields.debugRecordScene=kind==0;debug.fields.debugPlaybackScene=kind==1;
  PlayLayer::succeeds=false;assert(!debug.init(&level,false,false));assert(audio.channels==1);
  PlayLayer::succeeds=true;assert(debug.init(&level,false,false));
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
  assert(audio.channels==0);
#else
  assert(audio.channels==1);
#endif
  assert(FMODAudioEngine::voice && FMODAudioEngine::stops==0);
  runtime.data.promotion.active=kind==0;runtime.data.demotionWarning=kind==1;lobby.onEnter();
  assert(audio.channels==1 && audio.m_desiredMode==(kind==0?RankedAudioMode::LobbyPromotion:RankedAudioMode::LobbyDemotionWarning));
  creations=audio.creations;lobby.onEnter();lobby.refresh(0);assert(audio.creations==creations);
 }
 // A completely unrelated GD scene never acquires Ranked audio ownership.
 CorumRankedPlayLayer vanilla;auto previous=audio.channels;assert(vanilla.init(&level,false,false));assert(audio.channels==previous);
 audio.restoreMenuMusic();assert(audio.channels==0 && GameManager::menus==1 && FMODAudioEngine::stops==1);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-audio-native-'));
    try {
        const file=join(dir,'audio.cpp'), binary=join(dir,'audio');writeFileSync(file,cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(debugOn?['-DCORUM_RANKED_DEBUG_SPECTATE=1']:[]),file,'-o',binary],{stdio:'pipe'});
        execFileSync(binary,[],{stdio:'pipe'});
    } finally {rmSync(dir,{recursive:true,force:true});}
});
