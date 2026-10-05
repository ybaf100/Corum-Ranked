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
        'bool songIdReady(', 'bool songsReady('].map(extract).join('\n');
    const cpp = `
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
struct GJGameLevel {
    int m_levelID=88350599,m_songID=42;
    std::string m_levelString,m_songIDs;
    bool m_levelNotDownloaded=true;
};
struct Button {int requests=0;void activate(){++requests;}};
struct CustomSongWidget {bool m_isRobtopSong=false;int m_customSongID=42;Button* m_downloadBtn=nullptr;};
struct MusicDownloadManager {
    bool ready=false;static MusicDownloadManager* sharedState(){static MusicDownloadManager m;return &m;}
    std::string pathForSong(int)const{return {};}
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
    void adopt(GJGameLevel*,PreparationContext){}
};
}
${helpers}
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
struct CCDirector {static CCDirector* sharedDirector(){static CCDirector d;return &d;}void* getRunningScene(){return nullptr;}};
bool modal=false;bool hasOpenModal(void*){return modal;}
struct LevelInfoLayer {
    GJGameLevel* m_level=nullptr;CustomSongWidget* m_songWidget=nullptr;
    int downloads=0,plays=0;
    bool shouldDownloadLevel()const{return m_level->m_levelNotDownloaded;}
    void downloadLevel(){++downloads;}void onPlay(void*){++plays;}
};
#define schedule_selector(x) 0
struct CorumRankedLevelInfoLayer:LevelInfoLayer {
    struct Fields {bool debugSpectateOwned=true,debugMapRequested=false,debugSongRequested=false,debugPlayRequested=false;} fields;
    Fields* m_fields=&fields;
    void unschedule(int){}
    ${extract('void prepareDebugSpectate(float)')}
};
#endif
int main(){
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
    level.m_levelString="actual-downloaded-geometry";level.m_levelNotDownloaded=false;
    layer.prepareDebugSpectate(0.1f);layer.prepareDebugSpectate(0.1f);
    assert(layer.downloads==1&&layer.plays==0&&button.requests==1);
    MusicDownloadManager::sharedState()->ready=true;modal=true;layer.prepareDebugSpectate(0.1f);assert(layer.plays==0);
    modal=false;corum::ranked::RankedLevelPreparation::get().proof=false;layer.prepareDebugSpectate(0.1f);assert(layer.plays==0);
    corum::ranked::RankedLevelPreparation::get().proof=true;layer.prepareDebugSpectate(0.1f);layer.prepareDebugSpectate(0.1f);
    assert(layer.plays==1&&ranked.current==0);
    debug.admitted=false;assert(!mapReady(&level));
    CorumRankedLevelInfoLayer rejected;rejected.m_level=&level;rejected.m_songWidget=&widget;
    rejected.prepareDebugSpectate(0.1f);assert(rejected.plays==0&&rejected.downloads==0);
    debug.admitted=true;level.m_levelID=123;rejected.prepareDebugSpectate(0.1f);assert(rejected.plays==0);
#endif
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-debug-level-gate-'));
    try {
        const file = join(dir, 'gate.cpp');writeFileSync(file, cpp);
        for (const enabled of [false, true]) {
            const exe = join(dir, enabled ? 'debug' : 'official');
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                ...(enabled ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []), file, '-o', exe], { stdio: 'pipe' });
            execFileSync(exe);
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('actual Debug client builds HTTP requests, drains terminal/ACK retries, and never downloads a local replay', () => {
    // SDK transport/preparation are observable doubles. Entire new production
    // runtime is compiled, not a rewritten recorder policy. Actual HTTP + PG
    // persistence/restart/playback timing is exercised by the server suite.
    const viewerFixture = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model = viewerFixture.slice(viewerFixture.indexOf('namespace matjson {'), viewerFixture.indexOf('namespace corum::ranked::log'))
        .replace('    bool isObject()', '    static Value array(){return Value(Array{});} void push(Value x){std::get<Array>(data).push_back(std::move(x));}\n    bool isObject()');
    const header = read('src/debug/DebugSpectateRuntime.hpp').replace(/^#include.*$|^#pragma once$/gm, '');
    const source = read('src/debug/DebugSpectateRuntime.cpp').replace(/^#include.*$/gm, '');
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
struct LevelInfoLayer {static Scene* scene(GJGameLevel*,bool){static Scene s;return &s;}};
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
    GJGameLevel* ready=nullptr;int polls=0;
    static RankedLevelPreparation& get(){static RankedLevelPreparation p;return p;}
    void poll(){++polls;}GJGameLevel* level()const{return ready;}
    bool authorizes(GJGameLevel* level,LevelPreparationContext const& context)const{return level&&context.valid()&&level->id==context.levelId;}
};
struct SpectatorLiveRuntime {
    std::string context,token,path;int starts=0,stops=0;
    static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime l;return l;}
    void startDebugViewer(std::string c,std::string t,std::string p){context=c;token=t;path=p;++starts;}
    void stopDebugViewer(){++stops;context.clear();}
};
}
${header}
${source}
void respond(int status,matjson::Value data={}) {
    using namespace geode::utils::web;
    assert(!pending.empty());auto item=pending.front();pending.erase(pending.begin());item.callback({status,data});
}
matjson::Value admission(bool playback){
    matjson::Value result;result["levelId"]=88350599;result["formatVersion"]=1;
    result[playback?"playbackId":"recordingId"]=playback?"playback-id":"recording-id";
    result["token"]=playback?"playback-scoped-token":"recording-scoped-token";
    result["contextId"]="debug-spectate:playback-id";return result;
}
int main(){
    using namespace corum::ranked;using namespace corum::ranked::debug;using namespace geode::utils::web;
    auto& runtime=DebugSpectateRuntime::get();auto& ranked=RankedRuntime::get();
    ranked.state.stage=RuntimeStage::Matched;runtime.start(true,"secret");assert(pending.empty());
    ranked.state.stage=RuntimeStage::Queued;runtime.start(false,"secret");assert(pending.empty());
    ranked.state.stage=RuntimeStage::Ready;ranked.ownAlive=true;runtime.start(false,"secret");assert(pending.empty());
    ranked.ownAlive=false;runtime.start(false,"secret");assert(pending.size()==1);
    assert(pending[0].operation.url.ends_with("/recordings"));
    assert(pending[0].operation.body["levelId"].asInt().unwrapOr(0)==88350599);
    assert(pending[0].operation.headers.at("Authorization")=="Bearer ranked-session");
    assert(pending[0].operation.headers.at("x-corum-debug-spectate")=="1");
    respond(201,admission(false));
    GJGameLevel level;RankedLevelPreparation::get().ready=&level;runtime.tick();
    assert(CCDirector::sharedDirector()->pushed==1);
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
