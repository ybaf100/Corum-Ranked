import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');

test('actual native LevelInfo gate downloads VOICE and song without a fabricated Ranked current map', () => {
    const source = read('src/LevelInfoLayer.cpp');
    const extract = signature => {
        const start = source.indexOf(signature);
        assert(start >= 0, signature);
        const open = source.indexOf('{', start);
        let end = open + 1, depth = 1;
        while (depth && end < source.length) {
            if (source[end] === '{') ++depth;
            else if (source[end] === '}') --depth;
            ++end;
        }
        assert.equal(depth, 0);
        return source.slice(start, end);
    };
    const helpers = ['bool mapReady(', 'std::vector<int> collectSongIds(', 'bool songFileExists(',
        'bool songIdReady(', 'bool songsReady(', 'bool debugSpectateSongFilesReady('].map(extract).join('\n');
    const cpp = `
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
struct GJGameLevel {
    int m_levelID=88350599,m_songID=42;
    std::string m_levelString,m_songIDs;
    bool m_levelNotDownloaded=true;
    int m_levelType=3;
};
struct Button {int requests=0;void activate(){++requests;}};
struct CustomSongWidget {bool m_isRobtopSong=false;int m_customSongID=42;Button* m_downloadBtn=nullptr;};
struct MusicDownloadManager {
    bool ready=false;std::string path;static MusicDownloadManager* sharedState(){static MusicDownloadManager m;return &m;}
    std::string pathForSong(int)const{return path;}
    bool isResourceSong(int)const{return false;}bool isSongDownloaded(int)const{return ready;}
};
namespace corum::ranked {
struct RankedRuntime {
    int current=0;static RankedRuntime& get(){static RankedRuntime r;return r;}
    int currentLevelId()const{return current;}
};
struct PreparationContext{int levelId=0;};
namespace debug {
constexpr int kDebugSpectateLevelId=88350599;
struct DebugSpectateRuntime {
    bool admitted=false;static DebugSpectateRuntime& get(){static DebugSpectateRuntime d;return d;}
    bool preparedLevel(int id)const{return admitted&&id==kDebugSpectateLevelId;}
    PreparationContext preparationContext()const{return {admitted?kDebugSpectateLevelId:0};}
};
}
PreparationContext currentPreparationContext(){
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(debug::DebugSpectateRuntime::get().admitted)return debug::DebugSpectateRuntime::get().preparationContext();
#endif
    return {RankedRuntime::get().currentLevelId()};
}
struct RankedLevelPreparation {
    bool proof=true;static RankedLevelPreparation& get(){static RankedLevelPreparation p;return p;}
    bool authorizes(GJGameLevel* l,PreparationContext c)const{return proof&&l&&l->m_levelID==c.levelId;}
    GJGameLevel* adopted=nullptr;
    bool adopt(GJGameLevel* level,PreparationContext context){if(!authorizes(level,context))return false;adopted=level;return true;}
};
void tracePreparedLevel(char const*,GJGameLevel*,CustomSongWidget*,void*){}
}
${helpers}
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
namespace geode::log {template<class... T> void info(T...){} }
using namespace geode;
bool jukeboxLoaded(){return true;}
struct CCDirector {static CCDirector* sharedDirector(){static CCDirector d;return &d;}void* getRunningScene(){return nullptr;}};
bool modal=false;bool hasOpenModal(void*){return modal;}
struct LevelInfoLayer {
    GJGameLevel* m_level=nullptr;CustomSongWidget* m_songWidget=nullptr;
    int m_levelType=4;bool m_isBusy=false;
    int downloads=0,plays=0;
    bool shouldDownloadLevel()const{return m_level->m_levelNotDownloaded;}
    void downloadLevel(){++downloads;}void onPlay(void*){++plays;}
};
#define schedule_selector(x) 0
struct CorumRankedLevelInfoLayer:LevelInfoLayer {
    struct Fields {bool debugSpectateOwned=true,debugMapRequested=false,debugSongRequested=false,debugPlayRequested=false,debugMapReady=false,debugSongReady=false;} fields;
    Fields* m_fields=&fields;
    void unschedule(int){}
    ${extract('void prepareDebugSpectate(float)')}
};
#endif
int main(int, char** argv){
    MusicDownloadManager::sharedState()->path=argv[1];
    std::filesystem::remove(argv[1]);
    GJGameLevel level;CustomSongWidget widget;Button button;widget.m_downloadBtn=&button;
    auto& ranked=corum::ranked::RankedRuntime::get();
    assert(!mapReady(&level));level.m_levelString="actual-downloaded-geometry";level.m_levelNotDownloaded=false;
    ranked.current=88350599;assert(mapReady(&level));assert(!songsReady(&level,&widget));
    MusicDownloadManager::sharedState()->ready=true;assert(songsReady(&level,&widget));
    ranked.current=123;assert(!mapReady(&level)); // Normal Ranked gate remains exact.
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto& debug=corum::ranked::debug::DebugSpectateRuntime::get();
    ranked.current=0;debug.admitted=true; // Debug Ready has NO Ranked map/match.
    level.m_levelString.clear();level.m_levelNotDownloaded=true;MusicDownloadManager::sharedState()->ready=false;
    CorumRankedLevelInfoLayer layer;layer.m_level=&level;layer.m_songWidget=&widget;
    layer.prepareDebugSpectate(0.1f);layer.prepareDebugSpectate(0.1f);
    assert(layer.downloads==1&&layer.plays==0&&button.requests==0);
    GJGameLevel downloaded=level;downloaded.m_levelString="actual-downloaded-geometry";downloaded.m_levelNotDownloaded=false;
    layer.m_level=&downloaded; // Native callback replaces SearchResult with Saved.
    layer.prepareDebugSpectate(0.1f);layer.prepareDebugSpectate(0.1f);
    assert(layer.downloads==1&&layer.plays==0&&button.requests==1);
    MusicDownloadManager::sharedState()->ready=true;
    layer.prepareDebugSpectate(0.1f);assert(layer.plays==0); // Stale downloaded flag, effective iOS/Jukebox file absent.
    std::ofstream(argv[1])<<"native-song";
    modal=true;layer.prepareDebugSpectate(0.1f);assert(layer.plays==0);
    modal=false;corum::ranked::RankedLevelPreparation::get().proof=false;layer.prepareDebugSpectate(0.1f);assert(layer.plays==0);
    corum::ranked::RankedLevelPreparation::get().proof=true;
    layer.m_isBusy=true;layer.prepareDebugSpectate(0.1f);assert(layer.plays==0);
    layer.m_isBusy=false;layer.prepareDebugSpectate(0.1f);layer.prepareDebugSpectate(0.1f);
    assert(layer.plays==1&&ranked.current==0);
    assert(corum::ranked::RankedLevelPreparation::get().adopted==&downloaded&&layer.m_levelType==downloaded.m_levelType);
    auto requests=button.requests;
    MusicDownloadManager::sharedState()->ready=false;
    CorumRankedLevelInfoLayer saved;saved.m_level=&downloaded;saved.m_songWidget=&widget;
    saved.prepareDebugSpectate(0.1f);saved.prepareDebugSpectate(0.1f);
    assert(saved.plays==1&&saved.downloads==0&&button.requests==requests); // Saved geometry + existing effective file.
    debug.admitted=false;assert(!mapReady(&level));
    CorumRankedLevelInfoLayer rejected;rejected.m_level=&level;rejected.m_songWidget=&widget;
    rejected.prepareDebugSpectate(0.1f);assert(rejected.plays==0&&rejected.downloads==0);
    debug.admitted=true;level.m_levelID=123;rejected.prepareDebugSpectate(0.1f);assert(rejected.plays==0);
#endif
    std::filesystem::remove(argv[1]);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-debug-level-gate-'));
    try {
        const file = join(dir, 'gate.cpp');writeFileSync(file, cpp);
        for (const enabled of [false, true]) {
            const exe = join(dir, enabled ? 'debug' : 'official');
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                ...(enabled ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []), file, '-o', exe], { stdio: 'pipe' });
            execFileSync(exe, [join(dir, 'effective-jukebox-song.mp3')]);
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('actual preparation attests online identity before adopting the native downloaded Saved pointer', () => {
    const source = read('src/RankedLevelPreparation.cpp');
    const extract = signature => {
        const start = source.indexOf(signature);
        assert(start >= 0, signature);
        const open = source.indexOf('{', start);
        let end = open + 1, depth = 1;
        while (depth) {
            if (source[end] === '{') ++depth;
            else if (source[end] === '}') --depth;
            ++end;
        }
        return source.slice(start, end);
    };
    const methods = ['LevelMetadata levelMetadata(', 'bool RankedLevelPreparation::authorizes(',
        'GJGameLevel* RankedLevelPreparation::level()', 'bool RankedLevelPreparation::adopt(',
        'bool RankedLevelPreparation::adoptVerifiedSavedLevel(',
        'std::uint64_t RankedLevelPreparation::requestGeneration(', 'bool RankedLevelPreparation::finish()',
        'void RankedLevelPreparation::loadLevelsFinished(CCArray* levels, char const* key)'].map(extract).join('\n');
    const cpp = `
#include "${join(root, 'src/domain/LevelPreparationPolicy.hpp')}"
#include <cassert>
#include <chrono>
#include <functional>
#include <vector>
enum class GJLevelType {Saved=3,SearchResult=4,Local=2};
struct GJGameLevel {
    int m_levelID=88350599,m_userID=7540735,m_accountID=824772,m_songID=1184472,m_audioTrack=0,m_levelVersion=4;
    std::string m_levelName="VOICE",m_creatorName="LiteN1ng",m_levelString;
    GJLevelType m_levelType=GJLevelType::SearchResult;bool m_levelNotDownloaded=false;
};
namespace geode::log {template<class... T> void info(T...){} }
namespace geode {template<class T> struct Ref {
    T* value=nullptr;Ref()=default;Ref(T* p):value(p){}T* data()const{return value;}
    T* operator->()const{return value;}explicit operator bool()const{return value!=nullptr;}
    Ref& operator=(T* p){value=p;return *this;}
};}
using namespace geode;
namespace corum::ranked {
LevelPreparationContext current;
LevelPreparationContext currentPreparationContext(){return current;}
class RankedLevelPreparation {
public:
    LevelLookupTicket m_ticket;PreparedLevelProof m_proof;Ref<GJGameLevel> m_level;
    std::string m_key,m_error;LevelPreparationContext m_waitContext;
    bool authorizes(GJGameLevel*,LevelPreparationContext const&)const;
    GJGameLevel* level()const;
    bool adopt(GJGameLevel*,LevelPreparationContext const&);
    bool adoptVerifiedSavedLevel(LevelPreparationContext const&);
    std::uint64_t requestGeneration(char const*)const;
    bool finish();void loadLevelsFinished(struct CCArray*,char const*);
    void fail(char const*){assert(false&&"unexpected metadata failure");}
};
struct CCArray {std::vector<GJGameLevel*> values;unsigned count()const{return values.size();}GJGameLevel* objectAtIndex(unsigned i){return values.at(i);}};
template<class T> T typeinfo_cast(GJGameLevel* p){return static_cast<T>(p);}
struct GameLevelManager {
    GJGameLevel* saved=nullptr;RankedLevelPreparation* m_levelManagerDelegate=nullptr;int queries=0;
    static GameLevelManager* sharedState(){static GameLevelManager m;return &m;}
    GJGameLevel* getSavedLevel(int id){++queries;return saved&&saved->m_levelID==id?saved:nullptr;}
};
std::string preparationContextText(LevelPreparationContext const& c){return c.matchId;}
void tracePreparedLevel(char const*,GJGameLevel*){}
${methods}
}
int main(){
    using namespace corum::ranked;
    current={"debug-record:server-scope",88350599,1,0,false,0,0};
    auto* manager=GameLevelManager::sharedState();
    GJGameLevel online,saved;saved.m_levelType=GJLevelType::Saved;saved.m_levelString="415604-bytes-native-payload";
    manager->saved=&saved;
    RankedLevelPreparation preparation;
    assert(!preparation.adoptVerifiedSavedLevel(current)&&manager->queries==0); // Cache alone never attests.
    assert(preparation.m_ticket.begin(current));preparation.m_key="online";
    CCArray response{{&online}};preparation.loadLevelsFinished(&response,"online");
    assert(preparation.level()==&online&&online.m_levelString.empty());
    assert(preparation.adoptVerifiedSavedLevel(current)&&preparation.level()==&saved);
    assert(preparation.m_proof.matches(current,levelMetadata(&online))); // Proof unchanged by replacement.
    assert(online.m_levelString.empty()); // No raw geometry copy, Saved is the launch object.
    std::vector<std::function<void(GJGameLevel&)>> mismatches={
        [](auto& l){++l.m_levelID;},[](auto& l){l.m_levelName="other";},[](auto& l){l.m_creatorName="other";},
        [](auto& l){l.m_userID=0;},[](auto& l){++l.m_accountID;},[](auto& l){++l.m_levelVersion;},
        [](auto& l){++l.m_songID;},[](auto& l){++l.m_audioTrack;},
        [](auto& l){l.m_levelType=GJLevelType::SearchResult;},[](auto& l){l.m_levelType=GJLevelType::Local;},
        [](auto& l){l.m_levelString.clear();},[](auto& l){l.m_levelNotDownloaded=true;}};
    for(auto const& mutate:mismatches){
        assert(preparation.adopt(&online,current));auto wrong=saved;mutate(wrong);manager->saved=&wrong;
        assert(!preparation.adoptVerifiedSavedLevel(current)&&preparation.level()==&online);
    }
    manager->saved=nullptr;assert(!preparation.adoptVerifiedSavedLevel(current));
    auto downloaded=saved;assert(preparation.adopt(&downloaded,current)&&preparation.level()==&downloaded); // Native download replacement.
    // Re-query the replacement saved pointer after online proof, not an earlier snapshot.
    auto newer=saved;manager->saved=&newer;assert(preparation.adoptVerifiedSavedLevel(current)&&preparation.level()==&newer);
    auto stale=current;stale.matchId="new-scope";current=stale;
    assert(!preparation.adoptVerifiedSavedLevel(current)&&preparation.level()==nullptr);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-verified-saved-native-'));
    try {
        const file = join(dir, 'preparation.cpp'), exe = join(dir, 'preparation');writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', file, '-o', exe], { stdio: 'pipe' });
        execFileSync(exe);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('actual Debug client builds HTTP requests, drains terminal/ACK retries, and never downloads a local replay', () => {
    // SDK transport/preparation are observable doubles. Entire new production
    // runtime is compiled, not a rewritten recorder policy. Actual HTTP + PG
    // persistence/restart/playback timing is exercised by the server suite.
    const viewerFixture = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model = viewerFixture.slice(viewerFixture.indexOf('namespace matjson {'), viewerFixture.indexOf('${readFileSync(join(root,'))
        .replace('    bool isObject()', '    static Value array(){return Value(Array{});} void push(Value x){std::get<Array>(data).push_back(std::move(x));}\n    bool isObject()');
    const header = read('src/debug/DebugSpectateRuntime.hpp').replace(/^#include.*$|^#pragma once$/gm, '')
        .replace('private:', 'public: // Native-harness state fault injection only.');
    const source = read('src/debug/DebugSpectateRuntime.cpp').replace(/^#include.*$/gm, '');
    const popupSource = read('src/debug/DebugSpectatePopup.cpp');
    const popupClose = popupSource.slice(popupSource.indexOf('void onClose(CCObject* sender)'), popupSource.indexOf('\npublic:'));
    const cpp = `
#define CORUM_RANKED_DEBUG_SPECTATE 1
#include "${join(root, 'src/debug/DebugSpectatePolicy.hpp')}"
#include "${join(root, 'src/domain/LevelPreparationPolicy.hpp')}"
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <variant>
#include <vector>
${model}
${read("src/spectator/SpectatorPoseCodec.hpp").replace(/^#include.*$|^#pragma once$/gm, "")}
namespace geode {
namespace log {template<class... T> void info(T...){} template<class... T> void warn(T...){} }
namespace utils::web {
struct WebResponse {int status=200;matjson::Value data;int code()const{return status;}
    matjson::Result<matjson::Value> json()const{return {data};}};
struct Operation {std::string url;std::map<std::string,std::string> headers;matjson::Value body;};
struct WebRequest {
    std::map<std::string,std::string> headers;matjson::Value body;
    void header(std::string name,std::string value){headers[name]=value;}
    void followRedirects(bool){} void timeout(std::chrono::seconds){}
    void bodyJSON(matjson::Value value){body=std::move(value);}
    Operation post(std::string const& url)const{return {url,headers,body};}
};
struct Pending {Operation operation;std::function<void(WebResponse)> callback;};
inline std::vector<Pending> pending;
}
namespace async {template<class T> struct TaskHolder {
    template<class F> void spawn(utils::web::Operation operation,F callback){utils::web::pending.push_back({std::move(operation),callback});}
};}
}
struct CCNode {virtual ~CCNode()=default;virtual void update(float){} };
struct Scheduler {int scheduled=0;void scheduleUpdateForTarget(CCNode*,int,bool){++scheduled;}};
struct Scene {};
struct CCTransitionFade {static Scene* create(float,Scene* scene){return scene;}};
struct CCDirector {
    Scheduler scheduler;int pushed=0;
    static CCDirector* sharedDirector(){static CCDirector d;return &d;}
    Scheduler* getScheduler(){return &scheduler;} void pushScene(Scene*){++pushed;}
};
struct GJGameLevel {int id=88350599;};
inline std::function<void()> onSceneConstruction;
struct LevelInfoLayer {inline static GJGameLevel* launched=nullptr;static Scene* scene(GJGameLevel* level,bool){launched=level;if(onSceneConstruction)onSceneConstruction();static Scene s;return &s;}};
namespace geode::prelude {using namespace ::geode;namespace web=::geode::utils::web;}
namespace corum::ranked {
enum class RuntimeStage {Ready,Matched,Queued};
struct RankedRuntime {
    struct View{RuntimeStage stage=RuntimeStage::Ready;} state;
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    View const& view()const{return state;}std::string spectatorLiveSessionToken()const{return "ranked-session";}
    bool ownAlive=false;bool hasLocalGameplayAuthority()const{return ownAlive;}
};
struct RankedLevelPreparation {
    GJGameLevel* ready=nullptr;GJGameLevel* saved=nullptr;int polls=0;
    static RankedLevelPreparation& get(){static RankedLevelPreparation p;return p;}
    void poll(){++polls;}GJGameLevel* level()const{return ready;}
    bool adoptVerifiedSavedLevel(LevelPreparationContext const&){if(!saved)return false;ready=saved;return true;}
    bool authorizes(GJGameLevel* level,LevelPreparationContext const& context)const{return level&&context.valid()&&level->id==context.levelId;}
};
void tracePreparedLevel(char const*,GJGameLevel*){}
struct SpectatorLiveRuntime {
    std::string context,token,path;int starts=0,stops=0;
    static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime l;return l;}
    void startDebugViewer(std::string c,std::string t,std::string p){context=c;token=t;path=p;++starts;}
    void stopDebugViewer(){++stops;context.clear();}
};
}
${header}
${source}
struct CCObject {};
struct Popup {int closes=0;virtual ~Popup()=default;virtual void onClose(CCObject*){++closes;}};
struct PopupCloseFixture:Popup {${popupClose}};
void respond(int status,matjson::Value data={}) {
    using namespace geode::utils::web;
    assert(!pending.empty());auto item=pending.front();pending.erase(pending.begin());item.callback({status,data});
}
matjson::Value admission(bool playback){
    matjson::Value result;result["levelId"]=88350599;result["formatVersion"]=2;
    result[playback?"playbackId":"recordingId"]=playback?"playback-id":"recording-id";
    result["token"]=playback?"playback-scoped-token":"recording-scoped-token";
    result["contextId"]="debug-spectate:playback-id";return result;
}
int main(){
    using namespace corum::ranked;using namespace corum::ranked::debug;using namespace geode::utils::web;
    auto& runtime=DebugSpectateRuntime::get();auto& ranked=RankedRuntime::get();
    PopupCloseFixture popup;
    ranked.state.stage=RuntimeStage::Matched;runtime.start(true,"secret");assert(pending.empty());
    ranked.state.stage=RuntimeStage::Queued;runtime.start(false,"secret");assert(pending.empty());
    ranked.state.stage=RuntimeStage::Ready;ranked.ownAlive=true;runtime.start(false,"secret");assert(pending.empty());
    ranked.ownAlive=false;runtime.start(false,"secret");assert(pending.size()==1);
    assert(pending[0].operation.url.ends_with("/recordings"));
    assert(pending[0].operation.body["levelId"].asInt().unwrapOr(0)==88350599);
    assert(pending[0].operation.headers.at("Authorization")=="Bearer ranked-session");
    assert(pending[0].operation.headers.at("x-corum-debug-spectate")=="1");
    respond(201,admission(false));
    GJGameLevel level,online;RankedLevelPreparation::get().ready=&online;RankedLevelPreparation::get().saved=&level;
    onSceneConstruction=[&popup]{popup.onClose(nullptr);};
    runtime.tick();onSceneConstruction={};
    assert(CCDirector::sharedDirector()->pushed==1);
    assert(LevelInfoLayer::launched==&level&&LevelInfoLayer::launched!=&online);
    assert(popup.closes==1);
    runtime.cancelPopupPreparation();assert(runtime.mode()==DebugSpectateMode::PreparingRecord&&pending.empty());
    runtime.sceneEntered(123);assert(runtime.mode()==DebugSpectateMode::PreparingRecord);
    runtime.sceneEntered(88350599);assert(runtime.recordLevel(88350599)&&!runtime.playbackLevel(88350599));
    runtime.recordPose({100,50,0,false,false});assert(pending.size()==1);
    auto first=pending[0].operation.body;
    assert(pending[0].operation.url.ends_with("/frames"));
    assert(pending[0].operation.headers.at("x-debug-spectate-token")=="recording-scoped-token");
    assert(!pending[0].operation.headers.contains("x-match-token"));
    runtime.recordPose({110,55,10,true,false},true); // While first HTTP request in flight.
    assert(runtime.mode()==DebugSpectateMode::FinalizingRecord && pending.size()==1);
    matjson::Value ack;ack["lastSequence"]=0;respond(201,ack);runtime.tick();
    assert(pending.size()==1 && pending[0].operation.body["frames"].asArray().unwrap().size()==1);
    assert(pending[0].operation.body["frames"].asArray().unwrap()[0]["keyframe"]["dead"].asBool().unwrapOr(false));
    ack["lastSequence"]=1;respond(201,ack);runtime.tick();
    assert(pending.size()==1 && pending[0].operation.url.ends_with("/finalize"));
    assert(pending[0].operation.body["completed"].asBool().unwrapOr(false));
    respond(201);assert(runtime.mode()==DebugSpectateMode::None);
    runtime.start(true,"secret");respond(201,admission(true));runtime.sceneEntered(88350599);
    auto& viewer=SpectatorLiveRuntime::get();
    assert(viewer.starts==1 && viewer.context=="debug-spectate:playback-id");
    assert(viewer.path=="/api/ranked/debug/spectate/playbacks/playback-id/spectator-live");
    assert(runtime.playbackLevel(88350599)&&!runtime.playbackLevel(123));
    runtime.viewerResponse(true,false);assert(runtime.playbackComplete());
    runtime.exitScene();assert(viewer.stops==1 && pending[0].operation.url.ends_with("/close"));
    respond(201);assert(runtime.mode()==DebugSpectateMode::None);
    runtime.start(false,"secret");respond(201,admission(false));runtime.sceneEntered(88350599);
    runtime.cancelRecording();assert(pending[0].operation.url.ends_with("/finalize"));
    assert(!pending[0].operation.body["completed"].asBool().unwrapOr(true));respond(201);
    runtime.start(true,"secret");matjson::Value absent;absent["message"]="NO RECORDED SPECTATE DATA";
    respond(404,absent);assert(runtime.mode()==DebugSpectateMode::None && runtime.status()=="NO RECORDED SPECTATE DATA");
    runtime.start(false,"secret");respond(201,admission(false));popup.onClose(nullptr);
    assert(pending[0].operation.url.ends_with("/finalize")&&!pending[0].operation.body["completed"].asBool().unwrapOr(true));respond(201);
    runtime.start(false,"secret");respond(201,admission(false));runtime.tick();
    runtime.cancelPopupPreparation();assert(runtime.mode()==DebugSpectateMode::PreparingRecord&&pending.empty());
    runtime.cancelPreparation();assert(pending[0].operation.url.ends_with("/finalize")&&!pending[0].operation.body["completed"].asBool().unwrapOr(true));respond(201); // Explicit LevelInfo Back still cancels.
    // A previous expired/retried session must not poison the new HTTP admission.
    runtime.m_expireAt=std::chrono::steady_clock::now()-std::chrono::seconds(1);
    runtime.m_nextUploadAt=std::chrono::steady_clock::now()+std::chrono::minutes(1);
    runtime.start(false,"secret");runtime.tick();
    assert(runtime.mode()==DebugSpectateMode::PreparingRecord&&pending.size()==1);
    assert(runtime.m_expireAt.time_since_epoch().count()==0 && runtime.m_nextUploadAt.time_since_epoch().count()==0);
    respond(201,admission(false));runtime.sceneEntered(88350599);runtime.cancelRecording();respond(201);
    assert(runtime.mode()==DebugSpectateMode::None);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-debug-runtime-native-'));
    try {
        writeFileSync(join(dir, 'runtime.cpp'), cpp);
        const binary = join(dir, 'runtime');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'runtime.cpp'), '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
