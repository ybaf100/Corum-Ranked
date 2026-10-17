import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
const noIncludes = s => s.replace(/^\s*#(?:include|pragma once).*$/gm, '');

test('actual round preparation executes native download/resource/delegate/launch/return paths', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-round-native-'));
    // Compile the actual complete adapter and header. The double supplies native
    // SDK types, IO completion and a deterministic clock, not a copy of its state machine.
    let source = noIncludes(read('src/RankedRoundPreparation.cpp'));
    source = source.replace(/double clockSeconds\(\) \{[\s\S]*?\n\}/, 'double clockSeconds() { return fakeNow; }');
    source = source.replace('class $modify(CorumRankedRoundReturn, GameManager)', 'struct CorumRankedRoundReturn : GameManager');
    const popup = read('src/RankedPopup.cpp');
    const automationMethod = popup.slice(popup.indexOf('    void updateAutomation()'), popup.indexOf('    void maybeEnterLevel()'));
    const optionMethods = popup.slice(popup.indexOf('    void onLowDetail(CCObject*)'), popup.indexOf('    void addRoundOptions('));
    const cpp = String.raw`
#include "${join(root, 'src/domain/LevelPreparationPolicy.hpp')}"
#include "${join(root, 'src/domain/RoundResourcePolicy.hpp')}"
#include "${join(root, 'src/domain/SurvivalNativeOptions.hpp')}"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
double fakeNow = 100;
namespace gd { using string = std::string; }
struct CCNode {
    virtual ~CCNode()=default; bool live=true, running=false, visible=true;
    std::vector<CCNode*> children;
    bool init(){return true;} void release(){} virtual void update(float){}
    bool isRunning()const{return running;} bool isVisible()const{return visible;}
    auto const& getChildrenExt()const{return children;}
};
struct CCScene:CCNode {};
struct FLAlertLayer:CCNode {};
using CCObject = CCNode;
namespace cocos2d { using ::CCNode; using ::CCScene; }
template<class T> T typeinfo_cast(CCNode* n){return dynamic_cast<T>(n);}
namespace geode {
template<class T> struct Ref {
    T* ptr=nullptr; Ref()=default; Ref(T* p):ptr(p){if constexpr(requires(T* v){v->retain();})if(ptr)ptr->retain();}
    Ref(Ref const& other):Ref(other.ptr){}Ref(Ref&& other):ptr(std::exchange(other.ptr,nullptr)){}
    ~Ref(){if constexpr(requires(T* v){v->retain();})if(ptr)ptr->release();}
    Ref& operator=(T* p){if(ptr==p)return *this;if constexpr(requires(T* v){v->retain();}){if(p)p->retain();if(ptr)ptr->release();}ptr=p;return *this;}
    T* data()const{return ptr;} T* operator->()const{return ptr;}
    explicit operator bool()const{return ptr;}
};
template<class T> struct WeakRef {
    T* ptr=nullptr;WeakRef()=default;explicit WeakRef(T* p):ptr(p){}
    WeakRef(WeakRef const&)=default;
    WeakRef(WeakRef&& other):ptr(std::exchange(other.ptr,nullptr)){}
    WeakRef& operator=(WeakRef&& other){ptr=std::exchange(other.ptr,nullptr);return *this;}
    WeakRef& operator=(T* p){ptr=p;return *this;}
    Ref<T> lock()const{return ptr&&ptr->live?ptr:nullptr;}
};
namespace log {template<class... T>void info(T...){}template<class... T>void warn(T...){} }
namespace prelude { using geode::Ref; using geode::WeakRef; namespace log=geode::log; }
}
struct CCTextureCache {
    int references=1;void retain(){++references;}void release(){assert(references>0);--references;}
    static inline CCTextureCache* replacement=nullptr;
    static CCTextureCache* sharedTextureCache(){static CCTextureCache cache;return replacement?replacement:&cache;}
};
namespace cocos2d {using ::CCTextureCache;}
namespace fmt {template<class... T>std::string format(std::string s,T const&...){return s;}}
std::vector<std::function<void()>> queued;
void queueInMainThread(std::function<void()> f){queued.push_back(std::move(f));}
struct Scheduler {int registrations=0;void scheduleUpdateForTarget(CCNode*,int,bool){++registrations;}};
struct CCDirector {
    Scheduler scheduler; std::vector<CCScene*> stack; int pushes=0,pops=0;CCScene* next=nullptr;
    static CCDirector* sharedDirector(){static CCDirector d;return &d;}
    Scheduler* getScheduler(){return &scheduler;}
    CCScene* getRunningScene(){return stack.empty()?nullptr:stack.back();}
    CCScene* getNextScene(){return next;}
    void pushScene(CCScene* scene){++pushes;if(!stack.empty())stack.back()->running=false;stack.push_back(scene);scene->running=true;}
    void popScene(){assert(stack.size()>1);++pops;stack.back()->running=false;stack.pop_back();stack.back()->running=true;}
};
enum class GJLevelType {Main,Editor,Saved,SearchResult};
struct Mod {static Mod* get(){static Mod m;return &m;}template<class T>T getSavedValue(char const*,T fallback){return fallback;}};
struct GJGameLevel {
    int m_levelID=1,m_songID=42,m_audioTrack=0,m_levelVersion=1,m_userID=2,m_accountID=3;
    std::string m_levelName="Canonical",m_creatorName="Creator",m_levelString,m_songIDs,m_sfxIDs;
    GJLevelType m_levelType=GJLevelType::SearchResult;
    bool m_lowDetailMode=true,m_levelNotDownloaded=true,m_lowDetailModeToggled=false,m_disableShakeToggled=false;
};
enum class GJSongError {Failed};
struct SongInfoObject {int m_songID=0;bool m_unloaded=false;std::string m_songUrl;};
struct LevelDownloadDelegate {virtual ~LevelDownloadDelegate()=default;
    virtual void levelDownloadFinished(GJGameLevel*){}virtual void levelDownloadFailed(int){} };
struct MusicDownloadDelegate {virtual ~MusicDownloadDelegate()=default;
    virtual void loadSongInfoFinished(SongInfoObject*){}virtual void loadSongInfoFailed(int,GJSongError){}
    virtual void downloadSongFinished(int){}virtual void downloadSongFailed(int,GJSongError){}
    virtual void downloadSFXFinished(int){}virtual void downloadSFXFailed(int,GJSongError){} };
struct GameLevelManager {
    LevelDownloadDelegate* m_levelDownloadDelegate=nullptr;
    std::map<int,GJGameLevel*> saved;std::set<std::string> active;int requests=0,requested=0;std::string key;
    static GameLevelManager* sharedState(){static GameLevelManager m;return &m;}
    char const* getLevelDownloadKey(int id,bool,int){key="map"+std::to_string(id);return key.c_str();}
    bool isDLActive(char const* k)const{return active.contains(k);}
    GJGameLevel* getSavedLevel(int id){auto i=saved.find(id);return i==saved.end()?nullptr:i->second;}
    void downloadLevel(int id,bool gauntlet,int daily){assert(!gauntlet&&daily==0);++requests;requested=id;active.insert(getLevelDownloadKey(id,false,0));}
    void finish(GJGameLevel* level){active.erase("map"+std::to_string(requested));
        if(level){level->m_levelType=GJLevelType::Saved;saved[requested]=level;}
        assert(m_levelDownloadDelegate);m_levelDownloadDelegate->levelDownloadFinished(level);}
    void fail(int response){active.erase("map"+std::to_string(requested));assert(m_levelDownloadDelegate);m_levelDownloadDelegate->levelDownloadFailed(response);}
};
struct ActiveDownloads {unsigned n=0;unsigned count()const{return n;}};
struct MusicDownloadManager {
    ActiveDownloads activeCount;ActiveDownloads* m_activeDownloads=&activeCount;
    std::string m_customContentURL,key,base="${dir.replaceAll('\\', '/')}/";
    std::set<std::string> active;std::map<int,SongInfoObject> infos;
    std::vector<MusicDownloadDelegate*> delegates;
    std::map<std::string,int> requests;bool enumerating=false,cachedDownloaded=false;
    static MusicDownloadManager* sharedState(){static MusicDownloadManager m;return &m;}
    void tryLoadLibraries(){}void addMusicDownloadDelegate(MusicDownloadDelegate* d){assert(!enumerating);delegates.push_back(d);}
    std::string pathForSong(int id)const{return base+"song"+std::to_string(id);}
    std::string pathForSFX(int id)const{return base+"sfx"+std::to_string(id);}
    bool isResourceSong(int)const{return false;}bool isResourceSFX(int)const{return false;}
    bool isSongDownloaded(int)const{return cachedDownloaded;}
    char const* keyFor(std::string prefix,int id){key=prefix+std::to_string(id);return key.c_str();}
    char const* getSongInfoKey(int id){return keyFor("info",id);}
    char const* getSongDownloadKey(int id){return keyFor("song",id);}
    char const* getSFXDownloadKey(int id){return keyFor("sfx",id);}
    bool isDLActive(char const* k)const{return active.contains(k);}
    bool isRunningActionForSongID(int id)const{return active.contains("song"+std::to_string(id));}
    SongInfoObject* getSongInfoObject(int id){auto it=infos.find(id);return it==infos.end()?nullptr:&it->second;}
    int getDownloadProgress(int)const{return 50;}int getSFXDownloadProgress(int)const{return 25;}
    void begin(std::string k){assert(!active.contains(k));++requests[k];active.insert(k);activeCount.n=static_cast<unsigned>(active.size());}
    void getSongInfo(int id,bool download){assert(download);begin("info"+std::to_string(id));}
    void downloadSong(int id){if(id<=10000000)assert(infos.contains(id)&&!infos[id].m_songUrl.empty());begin("song"+std::to_string(id));}
    void downloadSFX(int id){assert(!m_customContentURL.empty());begin("sfx"+std::to_string(id));}
    void getCustomContentURL(){begin("url");}
    void finishKey(std::string k){active.erase(k);activeCount.n=static_cast<unsigned>(active.size());}
    void url(){finishKey("url");m_customContentURL="native-url";}
    void info(int id,bool success=true){finishKey("info"+std::to_string(id));
        if(success){infos[id]={id,false,"native-song-url"};}enumerating=true;
        for(auto* d:delegates){if(success)d->loadSongInfoFinished(&infos[id]);else d->loadSongInfoFailed(id,GJSongError::Failed);}enumerating=false;}
    void file(int id,bool sfx=false,bool write=true){finishKey((sfx?"sfx":"song")+std::to_string(id));
        if(write)std::ofstream(sfx?pathForSFX(id):pathForSong(id))<<"actual native audio";
        enumerating=true;for(auto* d:delegates){if(sfx)d->downloadSFXFinished(id);else d->downloadSongFinished(id);}enumerating=false;}
};
struct PlayLayer:CCNode {
    GJGameLevel* m_level=nullptr;static inline PlayLayer* active=nullptr;
    static inline int constructions=0;static inline bool failLoad=false;
    static PlayLayer* get(){return active;}
    static CCScene* scene(GJGameLevel* level,bool replay,bool noObjects){
        assert(!replay&&!noObjects);++constructions;if(failLoad)return nullptr;
        assert(level->m_levelType==GJLevelType::Saved&&!level->m_levelString.empty()&&!level->m_levelNotDownloaded);
        auto* p=new PlayLayer;p->m_level=level;p->running=true;active=p;
        auto* s=new CCScene;s->children.push_back(p);return s;}
};
struct GameManager {int vanillaReturns=0;void returnToLastScene(GJGameLevel*){++vanillaReturns;}};
struct PlatformToolbox {static inline int cursorRestores=0;
    static void toggleLockCursor(bool lock){assert(!lock);}static void showCursor(){++cursorRestores;}};
namespace corum::ranked {
struct SurvivalRuntime {static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}
 bool interceptFreePlayNativeQuit(GJGameLevel*)const{return false;}bool acquiring()const{return false;}bool active()const{return false;}bool canEnter()const{return false;}
 bool validateNative(GJGameLevel*)const{return false;}bool armNative(GJGameLevel*)const{return false;}
 void preparationFailed(std::string const&){} };

enum class RuntimeStage {Ready,Matched};
struct RuntimeView {RuntimeStage stage=RuntimeStage::Ready;struct Match {std::string matchId,state,side="A";bool readyA=false,readyB=false,placement=false;}match;};
struct RankedRuntime {
    RuntimeView state;LevelPreparationContext context;bool viewer=false,canPlay=true,entryAllowed=false;int arms=0,mapFailures=0,readyRequests=0,deadlineQueries=0,sceneEntries=0;
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    RuntimeView const& view()const{return state;}
    bool canEnterCurrentLevelAsSpectator()const{return viewer;}
    bool canEnterNativeRoundScene()const{return entryAllowed;}
    void noteNativeRoundSceneEntered(int id){assert(id==context.levelId);++sceneEntries;}
    bool canEnterCurrentLevel()const{return !viewer&&canPlay&&state.match.state=="ROUND_PLAYING";}
    bool armCurrentLevelForGameplay(){++arms;return canEnterCurrentLevel();}
    void reportMapDownloadFailure(){++mapFailures;}
    void submitReady(){++readyRequests;}
    void updateSessionHeartbeat(){}
    std::optional<std::int64_t> deadlineMillis(){++deadlineQueries;return 50000;}
};
LevelPreparationContext currentPreparationContext(){return RankedRuntime::get().context;}
std::string preparationContextText(LevelPreparationContext const&){return "context";}
LevelMetadata levelMetadata(GJGameLevel* l){return l?LevelMetadata{l->m_levelID,l->m_levelName,l->m_creatorName,l->m_userID,l->m_accountID,l->m_songID,l->m_audioTrack,l->m_levelVersion,true}:LevelMetadata{};}
struct RankedLevelPreparation {
    PreparedLevelProof proof;GJGameLevel* current=nullptr;int polls=0,invalidations=0;
    static RankedLevelPreparation& get(){static RankedLevelPreparation p;return p;}
    void poll(){++polls;}
    bool busy()const{return false;}
    std::string const& error()const{static std::string s;return s;}
    bool authorizes(GJGameLevel* l,LevelPreparationContext const& c)const{return l&&c==currentPreparationContext()&&proof.matches(c,levelMetadata(l));}
    GJGameLevel* level()const{return authorizes(current,currentPreparationContext())?current:nullptr;}
    void invalidate(LevelPreparationContext const&){++invalidations;proof.clear();current=nullptr;}
    void attest(GJGameLevel* l){current=l;assert(proof.attest(currentPreparationContext(),levelMetadata(l)));}
    bool adoptVerifiedSavedLevel(LevelPreparationContext const& c){
        auto* saved=GameLevelManager::sharedState()->getSavedLevel(c.levelId);
        if(!authorizes(current,c)||!saved||saved->m_levelType!=GJLevelType::Saved||saved->m_levelString.empty()||saved->m_levelNotDownloaded||!authorizes(saved,c))return false;
        current=saved;return true;}
};
void tracePreparedLevel(char const*,GJGameLevel*,void*,void*){}
}
${noIncludes(read('src/RankedRoundPreparation.hpp'))}
${source}
using namespace corum::ranked;
struct RoundOptions {
    GJGameLevel* level=nullptr;bool m_enteringLevel=false;int renders=0;
    GJGameLevel* findPlayableMap(){return level;}void render(){++renders;}
    ${optionMethods}
};
using SteadyClock=std::chrono::steady_clock;
struct RoundAutomation {
    bool m_matchFoundReadySent=false,m_levelLookupBusy=false;
    std::string m_levelLookupError,m_resourceKey="fixture";
    SteadyClock::time_point m_lastReadySubmitAt{};
    double elapsed=0;
    double phaseSeconds()const{return elapsed;}
    ${automationMethod}
};
void ticks(RankedRoundPreparation& p,int n=3){for(int i=0;i<n;++i){fakeNow+=.21;p.pump();}}
void context(int id,std::string match="match"){auto& r=RankedRuntime::get();r.state.stage=RuntimeStage::Matched;r.state.match={match,"ROUND_PREPARE","A"};r.context={match,id,1,0,false,0,0};}
int main(){
    assert(roundReadyDue("ROUND_PREPARE",false,10,true));
    assert(!roundReadyDue("ROUND_PREPARE",false,9.9,true));
    assert(roundReadyDue("DEATHMATCH_PREPARE",false,10,true));
    assert(!roundReadyDue("DEATHMATCH_PREPARE",false,9.9,true));
    assert(!roundReadyDue("DEATHMATCH_PREPARE",false,10,false));
    assert(!roundReadyDue("DEATHMATCH_ENTERING",false,99,true));
    assert(roundReadyDue("MAP_PREPARE",true,5,true));
    assert(!roundReadyDue("MAP_PREPARE",true,4.9,true));
    assert(!roundReadyDue("ROUND_PLAYING",false,99,true));
    assert(!roundReadyDue("ROUND_PREPARE",false,10,false));
    std::vector<int> ids{42};assert(appendRoundResourceIds(ids,"42,51,51"));assert(ids.size()==2);
    for(auto s:{"42x","2147483648","0","-2",",5","5,"}){std::vector<int> bad;assert(!appendRoundResourceIds(bad,s));}
    auto& p=RankedRoundPreparation::get();auto* maps=GameLevelManager::sharedState();auto* music=MusicDownloadManager::sharedState();
#if defined(GEODE_IS_WINDOWS)
    music->base+="win/";
#else
    music->base+="ios/";
#endif
    std::filesystem::create_directories(music->base);
    auto& meta=RankedLevelPreparation::get();auto& runtime=RankedRuntime::get();
    // Ordinary non-Ranked Practice: no metadata/resource/native scene mutations.
    ticks(p);assert(maps->requests==0&&music->requests.empty()&&meta.polls==0);
    context(1);GJGameLevel list;list.m_songIDs="42,43";list.m_sfxIDs="7";meta.attest(&list);
    LevelDownloadDelegate foreign;maps->m_levelDownloadDelegate=&foreign;ticks(p);
    assert(maps->requests==0&&maps->m_levelDownloadDelegate==&foreign);
    maps->m_levelDownloadDelegate=nullptr;ticks(p,1);assert(maps->requests==1&&!p.view().mapReady);
    p.pump();p.pump();assert(maps->requests==1); // same-tick duplicate callers
    GJGameLevel downloaded=list;downloaded.m_levelString="native geometry";downloaded.m_levelNotDownloaded=false;
    maps->finish(&downloaded);assert(!p.view().mapReady&&PlayLayer::constructions==0);ticks(p);
    assert(p.view().mapReady&&meta.level()==&downloaded&&music->requests["url"]==1&&!p.ready());
    // A retained GD object carrying a previous LDM ON must start this round OFF.
    downloaded.m_lowDetailModeToggled=true;ticks(p);assert(!downloaded.m_lowDetailModeToggled);
    music->url();ticks(p);assert(music->requests["info42"]==1&&music->requests["info43"]==1&&music->requests["sfx7"]==1);
    assert(music->active.size()<=3);ticks(p);assert(music->requests["info42"]==1);
    music->info(42);music->info(43);ticks(p);assert(music->requests["song42"]==1&&music->requests["song43"]==1);
    music->cachedDownloaded=true;music->file(42,false,false);ticks(p);assert(!p.ready()); // callback/cache alone proves nothing
    music->file(43);music->file(7,true);ticks(p);assert(p.view().songsDone==1&&p.view().sfxDone==1&&!p.ready());
    RoundAutomation incomplete;incomplete.elapsed=50;incomplete.updateAutomation();assert(runtime.readyRequests==0);
    fakeNow+=5;ticks(p);assert(music->requests["song42"]==2);music->file(42);ticks(p);assert(p.ready());
    assert(p.view().songsTotal==2&&p.view().sfxTotal==1&&p.view().audioProgress==1);
    // Execute the actual popup automation against completed automatic map/song/SFX
    // acquisition. With 50s still on the server's resource deadline, READY must
    // occur at the independent 10s countdown, never at timeout. No button click.
    RoundAutomation automation;
    automation.elapsed=9.9;automation.updateAutomation();assert(runtime.readyRequests==0);
    automation.elapsed=10;automation.updateAutomation();assert(runtime.readyRequests==1);
    assert(runtime.deadlineQueries==0); // safety deadline cannot drive READY
    automation.updateAutomation();assert(runtime.readyRequests==1); // one-second retry bound
    automation.m_lastReadySubmitAt-=std::chrono::seconds(2);
    automation.updateAutomation();assert(runtime.readyRequests==2); // ACK may be delayed
    runtime.state.match.readyA=true;automation.m_lastReadySubmitAt-=std::chrono::seconds(2);
    automation.updateAutomation();assert(runtime.readyRequests==2); // ACK stops retries
    runtime.state.match.readyA=false;
    runtime.state.match.placement=true;runtime.state.match.state="MAP_PREPARE";
    automation.elapsed=4.9;automation.updateAutomation();assert(runtime.readyRequests==2);
    automation.elapsed=5;automation.updateAutomation();assert(runtime.readyRequests==3);
    // Direct Force Death Match starts at this resource gate: the actual popup
    // sends READY at ten seconds, without any synthetic player READY or click.
    runtime.state.match.placement=false;runtime.state.match.state="DEATHMATCH_PREPARE";
    RoundAutomation deathmatch;
    deathmatch.elapsed=9.9;deathmatch.updateAutomation();assert(runtime.readyRequests==3);
    deathmatch.elapsed=10;deathmatch.updateAutomation();assert(runtime.readyRequests==4);
    runtime.state.match.state="DEATHMATCH_ENTERING";
    deathmatch.elapsed=99;deathmatch.updateAutomation();assert(runtime.readyRequests==4);
    runtime.state.match.placement=false;runtime.state.match.state="ROUND_PREPARE";
    CCScene origin;origin.running=true;auto* director=CCDirector::sharedDirector();director->stack={&origin};
    assert(!p.launch(&origin)&&PlayLayer::constructions==0&&runtime.arms==0); // READY is not PLAYING
    runtime.state.match.state="ROUND_PLAYING";runtime.canPlay=false;assert(!p.launch(&origin));runtime.canPlay=true;
    downloaded.m_levelNotDownloaded=true;assert(!p.launch(&origin)&&runtime.arms==0);downloaded.m_levelNotDownloaded=false;
    downloaded.m_songIDs="42,43,909";assert(!p.launch(&origin)&&runtime.arms==0);downloaded.m_songIDs="42,43";
    FLAlertLayer modal;origin.children={&modal};assert(!p.launch(&origin)&&runtime.arms==0);
    modal.visible=false; // hidden, retained popups must not strand launch forever
    RoundOptions options;options.onLowDetail(nullptr);assert(options.renders==0);
    options.level=&downloaded;options.onLowDetail(nullptr);options.onDisableShake(nullptr);
    assert(downloaded.m_lowDetailModeToggled&&downloaded.m_disableShakeToggled&&options.renders==2);
    ticks(p);assert(downloaded.m_lowDetailModeToggled); // explicit choice survives pumps
    options.m_enteringLevel=true;options.onLowDetail(nullptr);assert(downloaded.m_lowDetailModeToggled&&options.renders==2);
    assert(meta.authorizes(&downloaded,currentPreparationContext())); // options do not alter metadata proof
    CCScene transition;director->next=&transition;
    assert(!p.launch(&origin)&&PlayLayer::constructions==0&&runtime.arms==0);
    director->next=nullptr;PlayLayer departing;departing.running=false;PlayLayer::active=&departing;
    assert(!p.launch(&origin)&&PlayLayer::constructions==0&&runtime.arms==0);
    PlayLayer::active=nullptr;
    assert(p.launch(&origin));assert(runtime.arms==1&&PlayLayer::active->m_level==&downloaded);
    assert(CCTextureCache::sharedTextureCache()->references==2);
    assert(PlayLayer::active->m_level->m_lowDetailModeToggled&&PlayLayer::active->m_level->m_disableShakeToggled);
    assert(!p.launch(&origin)&&PlayLayer::constructions==1&&director->pushes==1);
    CorumRankedRoundReturn navigation;GJGameLevel unrelated;
    navigation.returnToLastScene(&unrelated);assert(navigation.vanillaReturns==1);
    {ScopedRankedRoundQuit quit(PlayLayer::get());navigation.returnToLastScene(&downloaded);
        navigation.returnToLastScene(&downloaded); // duplicate return is consumed, no second scene operation
        assert(queued.size()==1&&director->pops==0&&navigation.vanillaReturns==1);}
    assert(!p.launch(director->getRunningScene())); // pending native return cannot create R2
    PlayLayer::active->running=false;PlayLayer::active->live=false;PlayLayer::active=nullptr;
    auto tasks=std::exchange(queued,{});for(auto& t:tasks)t();assert(director->getRunningScene()==&origin&&director->pops==1);
    assert(CCTextureCache::sharedTextureCache()->references==2); // retained between rounds
    auto* priorCache=CCTextureCache::sharedTextureCache();static CCTextureCache reloaded;
    CCTextureCache::replacement=&reloaded;
    runtime.viewer=true;assert(p.launch(&origin));assert(runtime.arms==1);
    assert(reloaded.references==2&&priorCache->references==1); // spectator never arms
    {ScopedRankedRoundQuit quit(PlayLayer::get());navigation.returnToLastScene(&downloaded);}
    PlayLayer::active->running=false;PlayLayer::active->live=false;PlayLayer::active=nullptr;
    tasks=std::exchange(queued,{});for(auto& t:tasks)t();runtime.viewer=false;
#if defined(GEODE_IS_WINDOWS)
    assert(PlatformToolbox::cursorRestores==2);
#else
    assert(PlatformToolbox::cursorRestores==0);
#endif
    // Completed assets via GD's effective NONG paths, including a built-in track 0.
    context(2);GJGameLevel track;track.m_levelID=2;track.m_songID=0;track.m_levelType=GJLevelType::Saved;track.m_levelNotDownloaded=false;track.m_levelString="geometry";
    track.m_lowDetailModeToggled=true;
    maps->saved[2]=&track;meta.attest(&track);ticks(p);assert(p.ready()&&p.view().songsTotal==0);
    assert(!track.m_lowDetailModeToggled); // new context gets a fresh OFF default
    context(2);runtime.state.match.state="ROUND_ENTERING";runtime.entryAllowed=true;
    int const previousArms=runtime.arms;assert(p.launch(&origin));
    assert(runtime.arms==previousArms&&runtime.sceneEntries==0&&PlayLayer::active->m_level==&track);
    {ScopedRankedRoundQuit quit(PlayLayer::get());navigation.returnToLastScene(&track);}
    PlayLayer::active->running=false;PlayLayer::active->live=false;PlayLayer::active=nullptr;
    tasks=std::exchange(queued,{});for(auto& t:tasks)t();runtime.entryAllowed=false;
    // Cancel A -> B -> A: preserve the untagged native ticket until drained.
    context(3);GJGameLevel old;old.m_levelID=3;old.m_songID=0;meta.attest(&old);ticks(p,1);int const before=maps->requests;
    context(4);ticks(p,1);context(3);ticks(p,1);assert(maps->requests==before&&maps->m_levelDownloadDelegate==&p);
    maps->fail(-1);ticks(p);assert(maps->requests==before+1&&maps->m_levelDownloadDelegate==&p);
    meta.attest(&old);ticks(p);assert(maps->requests==before+1);
    maps->fail(-2);ticks(p);fakeNow+=5;ticks(p);assert(maps->requests==before+2);
    // A download version mismatch cannot replace attested metadata/geometry.
    GJGameLevel changed=old;changed.m_levelVersion=2;changed.m_levelString="new geometry";changed.m_levelNotDownloaded=false;
    maps->finish(&changed);ticks(p);assert(meta.invalidations==1&&!p.ready());
    meta.attest(&changed);ticks(p);assert(p.ready()&&meta.level()==&changed);
    // Native failures retry at bounded cadence, cap at three, and manual retry
    // cannot duplicate an active request. A later real completion can recover.
    context(5);GJGameLevel audio=changed;audio.m_levelID=5;audio.m_songID=101;maps->saved[5]=&audio;meta.attest(&audio);ticks(p);
    assert(music->requests["info101"]==1);fakeNow+=121;ticks(p);p.retry();ticks(p);
    assert(music->requests["info101"]==1&&!p.ready()&&!p.view().error.empty());
    for(int attempt=1;attempt<=3;++attempt){
        assert(music->requests["info101"]==attempt);music->info(101,false);ticks(p);fakeNow+=7;
        if(attempt<3)ticks(p);
    }
    ticks(p);assert(!p.view().error.empty()&&music->requests["info101"]==3);
    p.retry();ticks(p);assert(music->requests["info101"]==4);
    music->info(101);ticks(p);assert(music->requests["song101"]==1);music->file(101);ticks(p);assert(p.ready());
    // An unacknowledged timer=0 result phase cannot create any scene/Attempt.
    runtime.state.match.state="MAP_SETTLING";ticks(p);int arms=runtime.arms;assert(!p.launch(&origin)&&runtime.arms==arms);
    runtime.state.stage=RuntimeStage::Ready;ticks(p);int polls=meta.polls,reqs=maps->requests;ticks(p);assert(meta.polls==polls&&maps->requests==reqs);
}
`;
    try {
        writeFileSync(join(dir, 'test.cpp'), cpp);
        for (const platform of ['GEODE_IS_WINDOWS', 'GEODE_IS_IOS']) {
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-D' + platform, join(dir, 'test.cpp'), '-o', join(dir, 'test')], { stdio: 'pipe' });
            execFileSync(join(dir, 'test'), [], { stdio: 'pipe' });
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('round preparation is passive and automatic with native user options, server gate and preserved hook inventory', () => {
    const popup = read('src/RankedPopup.cpp');
    const preparation = read('src/RankedRoundPreparation.cpp');
    const play = read('src/PlayLayer.cpp');
    assert.doesNotMatch(popup, /LevelInfoLayer::scene|showRankedSongDownloadGate/);
    assert.doesNotMatch(popup, /MAP DOWNLOAD|MUSIC DOWNLOAD|onDownloadMap|onDownloadSong|openSongDownloadGate/);
    for (const token of ['tr("low-detail", "LDM")', 'tr("disable-shake", "DISABLE SHAKE")']) assert(popup.includes(token));
    const status = popup.slice(popup.indexOf('    void addPreparationStatusRows('), popup.indexOf('    std::vector<int> collectSongIds('));
    assert.match(status, /makeTextPlate\(/);
    assert.doesNotMatch(status, /addButton\(|CCMenuItem(?:SpriteExtra)?::create|menu_selector\(/);
    for (const token of ['"downloading"', '"map-ready"', '"audio-ready"', '"download-failed"', '"semibold"']) assert(status.includes(token));
    const placement = popup.slice(popup.indexOf('    void renderPlacementMap('), popup.indexOf('    void renderPlacementMapResult('));
    const round = popup.slice(popup.indexOf('    void renderPrepare('), popup.indexOf('    void renderSpectator('));
    assert.match(placement, /addPreparationStatusRows\(/);
    assert.match(round, /addPreparationStatusRows\(/);
    assert.match(popup, /setLowDetail\(!level->m_lowDetailModeToggled\)/);
    assert.match(popup, /level->m_disableShakeToggled = !level->m_disableShakeToggled/);
    assert.doesNotMatch(popup, /level->m_lowDetailMode\s*=|level->m_disableShake\s*=/);
    assert.match(popup, /roundReadyDue\(match.state, match.placement, phaseSeconds\(\), preparation.ready\(\)\)/);
    const automation = popup.slice(popup.indexOf('    void updateAutomation()'), popup.indexOf('    void maybeEnterLevel()'));
    assert.doesNotMatch(automation, /runtime.deadlineMillis\(\)/);
    assert(play.indexOf('ScopedRankedRoundQuit roundQuit(this)') < play.indexOf('PlayLayer::onQuit();', play.indexOf('void onQuit()')));
    assert.equal((preparation.match(/class \$modify\(/g) || []).length, 1);
    assert.match(preparation, /class \$modify\(CorumRankedRoundReturn, GameManager\)/);
    assert.doesNotMatch(preparation, /\$modify\([^,]+, (?:PlayerObject|GJBaseGameLayer|GJGameLevel|PlayLayer)\)/);
    assert.doesNotMatch(play, /void setVisible\(bool/);
    assert.doesNotMatch(play, /void saveNewScore\(/);
    assert.match(read('src/LevelInfoLayer.cpp'), /void prepareDebugSpectate\(float\)/);
});

test('equipped title stays separate from the shared streak/promotion/warning status row', () => {
    const popup = read('src/RankedPopup.cpp');
    const lobby = popup.slice(popup.indexOf('    void renderModeCard('), popup.indexOf('    void renderModePanel('));
    const status = popup.slice(popup.indexOf('    void addRankStatus('), popup.indexOf('    struct PlayerCardRefs'));
    assert.match(status, /profile.promotion.active/);
    assert.match(status, /i18n::promotionMatch/);
    assert.match(status, /profile.demotionWarning/);
    assert.match(status, /tr\("your-rank"/);
    assert.doesNotMatch(status, /achievementTitleText|achievementTitleColor/);
    assert.match(lobby, /equippedTitle->setID\("ranked-profile-equipped-title"_spr\)/);
    assert.match(lobby, /addRankStatus\(profile/);
    assert.match(lobby, /leftCenter\.y - 68\.0f/);
    assert.match(lobby, /addPlacementProgress/);
    const card = popup.slice(popup.indexOf('    void addPlayerCard('), popup.indexOf('    void renderResources('));
    assert.match(card, /addRankStatus\(profile/);
    assert.doesNotMatch(card, /auto\* warning = makeLabel/);
});
