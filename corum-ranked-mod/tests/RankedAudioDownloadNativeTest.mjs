import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
function method(source, signature) {
    const start = source.indexOf(signature); assert(start >= 0, signature);
    const open = source.indexOf('{', start); let end = open + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}

for (const debug of [false, true]) test(`production song downloads: Library, Newgrounds, cache and cancellation (Debug ${debug ? 'ON' : 'OFF'})`, () => {
    const source = read('src/RankedAudioManager.cpp');
    const methods = [
        'std::string RankedAudioManager::configSignature(',
        'void RankedAudioManager::configure(',
        'bool RankedAudioManager::enabled()',
        'std::vector<int> RankedAudioManager::uniqueSongIds()',
        'std::filesystem::path RankedAudioManager::cachedSongPath(',
        'std::filesystem::path RankedAudioManager::preferredDownloadPath(',
        'std::string RankedAudioManager::readySongPath(',
        'bool RankedAudioManager::songReady(',
        'bool RankedAudioManager::resourcesReady()',
        'bool RankedAudioManager::requiresResourceDownload()',
        'RankedResourceDownloadView RankedAudioManager::downloadView()',
        'void RankedAudioManager::cancelActiveDownload()',
        'void RankedAudioManager::downloadAll()',
        'void RankedAudioManager::retryFailed()',
        'void RankedAudioManager::startNextDownload()',
        'void RankedAudioManager::fetchSongInfo(',
        'void RankedAudioManager::updateLibraryDownload()',
        'void RankedAudioManager::downloadSongFile(',
        'void RankedAudioManager::finishDownloadSuccess(',
        'void RankedAudioManager::finishDownloadFailure(',
        'void RankedAudioManager::synchronizeSongCaches()',
    ];
    // Execute the production download portion of tick; playback has its own
    // FMOD fixture. Clock, HTTP and GD downloads are controlled boundaries.
    const tick = method(source, 'void RankedAudioManager::tick()');
    const downloadTick = tick.slice(0, tick.indexOf('    if (resourcesReady()')) + '\n}';
    const helpers = source.slice(source.indexOf('constexpr auto kSongInfoTimeout'), source.indexOf('enum class PreparedStreamState'));
    const configSource = read('src/RankedRuntime.hpp');
    const config = configSource.slice(configSource.indexOf('struct RankedAudioResourceView {'),
        configSource.indexOf('struct EnvironmentModIssueView {'));
    const header = read('src/RankedAudioManager.hpp').replace('#include "RankedRuntime.hpp"', '#include "AudioConfigDouble.hpp"').replace('private:', 'public:');
    const cpp = `
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
struct AudioClock {
    static inline auto current=std::chrono::steady_clock::time_point(std::chrono::seconds(100));
    static auto now(){return current;}
};
namespace fmt {
template<class... T> std::string format(std::string pattern,T const&... values){
    auto append=[&](auto const& value){auto at=pattern.find("{}");assert(at!=std::string::npos);
        std::ostringstream stream;stream<<value;pattern.replace(at,2,stream.str());};
    (append(values),...);return pattern;
}}
namespace geode::log {template<class... T> void info(T...){} template<class... T> void warn(T...){} }
namespace geode::utils::web {
struct WebProgress {float value=0;std::optional<float> downloadProgress()const{return value;}};
struct Result {bool error=false;std::string value;bool isErr()const{return error;}std::string unwrap()const{return value;}};
struct WebResponse {
    int status=200;std::string body;bool bodyError=false,writeError=false;
    bool ok()const{return status>=200&&status<300;}int code()const{return status;}
    Result string()const{return {bodyError,body};}
    Result into(std::filesystem::path const& path)const{
        if(writeError)return {true,{}};
        std::ofstream out(path,std::ios::binary);out.write(body.data(),body.size());return {!out,{}};
    }
};
struct Request {std::string verb,url,body;std::function<void(WebProgress const&)> progress;};
struct Job {Request request;std::function<void(WebResponse)> callback;bool cancelled=false;};
inline std::vector<std::shared_ptr<Job>> jobs;
struct WebRequest {
    Request request;
    void timeout(std::chrono::seconds){}void header(char const*,char const*){}
    void followRedirects(bool){}void bodyString(std::string value){request.body=std::move(value);}
    template<class F> void onProgress(F callback){request.progress=callback;}
    Request post(std::string url){request.verb="POST";request.url=std::move(url);return request;}
    Request get(std::string url){request.verb="GET";request.url=std::move(url);return request;}
};
}
namespace web=geode::utils::web;
namespace geode::async {
template<class T> struct TaskHolder {
    std::shared_ptr<web::Job> job;
    void cancel(){if(job)job->cancelled=true;}
    template<class F> void spawn(web::Request request,F callback){
        job=std::make_shared<web::Job>(web::Job{request,callback,false});web::jobs.push_back(job);
    }
};
}
struct Mod {
    std::filesystem::path save;static inline Mod* instance=nullptr;
    static Mod* get(){return instance;}auto getSaveDir()const{return save;}
};
struct MusicDownloadManager {
    static inline MusicDownloadManager* instance=nullptr;
    static auto sharedState(){return instance;}
    std::filesystem::path root;std::string m_customContentURL,key;
    std::set<int> actions,downloads,resources,staleDownloaded;
    std::vector<int> requests;int urlRequests=0,progress=0;bool startActive=true;
    void getCustomContentURL(){++urlRequests;}
    void downloadSong(int id){requests.push_back(id);if(startActive)downloads.insert(id);}
    bool isRunningActionForSongID(int id)const{return actions.contains(id);}
    char const* getSongDownloadKey(int id){key=std::to_string(id);return key.c_str();}
    bool isDLActive(char const* value)const{return downloads.contains(std::stoi(value));}
    int getDownloadProgress(int)const{return progress;}
    bool isResourceSong(int id)const{return resources.contains(id);}
    bool isSongDownloaded(int id)const{return staleDownloaded.contains(id);}
    std::string pathForSong(int id)const{return (root/(std::to_string(id)+".ogg")).string();}
};
#include "AudioManagerUnderTest.hpp"
using namespace geode;
namespace corum::ranked {
${helpers}
${(methods.map(n => method(source, n)).join('\n') + '\n' + downloadTick).replaceAll('std::chrono::steady_clock::now()', 'AudioClock::now()')}
// Playback boundaries only; no download/cache decisions are doubled.
void RankedAudioManager::cancelPendingAudioStart(){}
void RankedAudioManager::stopOwnedAudio(){}
void RankedAudioManager::releasePreparedSounds(){}
void RankedAudioManager::prewarmReadySounds(){}
void RankedAudioManager::updateOwnedFades(std::chrono::steady_clock::time_point){}
void RankedAudioManager::reapRetainedSounds(){}
void RankedAudioManager::updateStreamHealth(){}
}
using namespace corum::ranked;
std::string audioBytes(){return "OggS"+std::string(2044,'x');}
void writeFile(std::filesystem::path const& path,std::string const& bytes){
    std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<bytes;assert(out);
}
struct Fixture {
    static inline int sequence=0;Mod mod;MusicDownloadManager native;RankedAudioManager audio;
    Fixture(std::filesystem::path root,std::vector<int> ids){
        root/=std::to_string(++sequence);mod.save=root/"save";native.root=root/"gd";
        Mod::instance=&mod;MusicDownloadManager::instance=&native;web::jobs.clear();
        AudioClock::current=std::chrono::steady_clock::time_point(std::chrono::seconds(100));
        RankedClientPresentationView config;config.audio.enabled=true;
        for(int id:ids)config.audio.resources.push_back({std::to_string(id),"",id,42.5,true});
        audio.configure(config);
    }
    ~Fixture(){audio.cancelActiveDownload();web::jobs.clear();}
    void step(int milliseconds=20){AudioClock::current+=std::chrono::milliseconds(milliseconds);audio.tick();}
    void complete(int id,std::string bytes=audioBytes()){
        writeFile(native.pathForSong(id),bytes);native.downloads.erase(id);native.actions.erase(id);
    }
};
int main(int argc,char** argv){assert(argc==2);std::filesystem::path root=argv[1];
    // Library URL preparation is once-only. The native in-flight file is
    // neither played nor copied, even after its audio header exists.
    {Fixture f(root,{10000001});f.audio.downloadAll();assert(f.native.urlRequests==1&&web::jobs.empty());
        f.step();f.step();assert(f.native.urlRequests==1&&f.native.requests.empty());
        f.native.m_customContentURL="https://native-content.example";f.step();
        assert(f.native.requests==std::vector<int>{10000001});f.step();assert(f.native.requests.size()==1);
        writeFile(f.native.pathForSong(10000001),audioBytes());f.audio.synchronizeSongCaches();
        assert(!f.audio.songReady(10000001)&&!std::filesystem::exists(f.audio.cachedSongPath(10000001)));
        f.native.progress=150;f.step();assert(f.audio.downloadView().activeProgress==99);
        f.complete(10000001);f.step();assert(f.audio.resourcesReady());
        assert(f.audio.downloadView().state==RankedResourceDownloadState::Ready);
        assert(f.audio.readySongPath(10000001)==f.audio.cachedSongPath(10000001).string());
        std::filesystem::remove(f.native.pathForSong(10000001));
        RankedAudioManager reopened;reopened.configure(f.audio.m_config);assert(reopened.resourcesReady());
        reopened.downloadAll();assert(f.native.requests.size()==1&&web::jobs.empty());}
    // Full Library IDs are used unchanged, including the source boundary.
    {Fixture f(root,{10000000});f.native.m_customContentURL="native";f.audio.downloadAll();
        assert(f.native.requests==std::vector<int>{10000000}&&web::jobs.empty());
        f.complete(10000000);f.step();assert(f.audio.resourcesReady());}
    // Adopt requests owned by another caller, with either active API set.
    for(bool action:{false,true}){Fixture f(root,{10000002});
        (action?f.native.actions:f.native.downloads).insert(10000002);f.audio.downloadAll();
        assert(f.native.requests.empty()&&f.native.urlRequests==0);f.complete(10000002);f.step();
        assert(f.audio.resourcesReady()&&f.native.requests.empty());}
    // A private cache can be played while a foreign request continues, but
    // synchronizing it must not overwrite that request's destination.
    {Fixture f(root,{10000003});writeFile(f.audio.cachedSongPath(10000003),audioBytes());
        f.native.downloads.insert(10000003);writeFile(f.native.pathForSong(10000003),"in-progress");
        f.audio.synchronizeSongCaches();assert(f.audio.songReady(10000003));
        assert(std::filesystem::file_size(f.native.pathForSong(10000003))==11);}
    // Metadata flags and error pages are not proof of playable audio.
    {Fixture f(root,{10000004});f.native.staleDownloaded.insert(10000004);
        assert(!f.audio.songReady(10000004));writeFile(f.native.pathForSong(10000004),std::string(4096,'<'));
        assert(!f.audio.songReady(10000004));f.native.m_customContentURL="native";f.audio.downloadAll();
        f.complete(10000004,"OggS");f.step();f.step(2000);
        assert(f.audio.downloadView().state==RankedResourceDownloadState::Failed);
        f.audio.retryFailed();assert(f.native.requests.size()==2);f.complete(10000004);f.step();
        assert(f.audio.resourcesReady()&&f.audio.downloadView().failed==0);}
    // URL stalls, silent native refusal and long-running native requests are
    // bounded, with no repeated requests or cancellation of GD ownership.
    {Fixture f(root,{10000005});f.audio.downloadAll();f.step(20000);
        assert(f.audio.downloadView().state==RankedResourceDownloadState::Failed&&f.native.urlRequests==1);
        f.audio.retryFailed();assert(f.native.urlRequests==2);}
    {Fixture f(root,{10000006});f.native.m_customContentURL="native";f.native.startActive=false;
        f.audio.downloadAll();f.step(2000);assert(f.audio.downloadView().state==RankedResourceDownloadState::Failed);
        assert(f.native.requests.size()==1);}
    {Fixture f(root,{10000007});f.native.m_customContentURL="native";f.audio.downloadAll();f.step(105000);
        assert(f.audio.downloadView().state==RankedResourceDownloadState::Failed);
        assert(f.native.downloads.contains(10000007));f.audio.retryFailed();assert(f.native.requests.size()==1);
        f.complete(10000007);f.step();assert(f.audio.resourcesReady());}
    // A changed spreadsheet config retires local observation. Finishing the
    // old native request cannot finish or cache the new configured song.
    {Fixture f(root,{10000008});f.native.m_customContentURL="native";f.audio.downloadAll();
        auto config=f.audio.m_config;config.audio.resources[0].songId=10000009;f.audio.configure(config);
        assert(f.native.downloads.contains(10000008)&&f.audio.m_activeDownloadSongId==0);
        f.audio.downloadAll();f.complete(10000008);f.step();
        assert(!f.audio.resourcesReady()&&f.audio.m_activeDownloadSongId==10000009);
        assert(!std::filesystem::exists(f.audio.cachedSongPath(10000008)));
        f.complete(10000009);f.step();assert(f.audio.resourcesReady());}
    // Existing Newgrounds metadata/URL/progress/download path is exercised
    // unchanged, in the same queue as Library IDs (duplicate roles share IDs).
    {Fixture f(root,{9999999,10000010,9999999});f.native.m_customContentURL="native";f.audio.downloadAll();
        assert(f.audio.downloadView().total==2&&web::jobs.size()==1&&f.native.requests.empty());
        assert(web::jobs[0]->request.verb=="POST"&&web::jobs[0]->request.body.starts_with("songID=9999999&"));
        web::jobs[0]->callback({200,"1~|~9999999~|~10~|~https%3A%2F%2Faudio.example%2Fsong.ogg"});
        assert(web::jobs.size()==2&&web::jobs[1]->request.url=="https://audio.example/song.ogg");
        web::jobs[1]->request.progress({37.6f});assert(f.audio.downloadView().activeProgress==38);
        web::jobs[1]->callback({200,audioBytes()});assert(f.native.requests==std::vector<int>{10000010});
        f.complete(10000010);f.step();assert(f.audio.resourcesReady());}
    // A failed Library song must not stall subsequent Newgrounds resources.
    {Fixture f(root,{10000011,12345});f.native.m_customContentURL="native";f.native.startActive=false;
        f.audio.downloadAll();f.step(2000);assert(web::jobs.size()==1&&f.audio.m_activeDownloadSongId==12345);
        web::jobs[0]->callback({200,"10~|~https://audio.example/song.mp3"});
        web::jobs[1]->callback({200,audioBytes()});assert(f.audio.downloadView().ready==1&&f.audio.downloadView().failed==1);}
    // Retired Newgrounds callbacks (including progress and late file writes)
    // cannot affect a Library request started by a new config generation.
    for(bool filePhase:{false,true}){Fixture f(root,{23456});f.audio.downloadAll();
        if(filePhase)web::jobs[0]->callback({200,"10~|~https://audio.example/song.mp3"});
        auto old=web::jobs.back();auto config=f.audio.m_config;config.audio.resources[0].songId=10000012;
        f.native.m_customContentURL="native";f.audio.configure(config);f.audio.downloadAll();
        assert(old->cancelled);if(old->request.progress)old->request.progress({99});
        old->callback({200,filePhase?audioBytes():"10~|~https://stale.example/song.mp3"});
        assert(!std::filesystem::exists(f.audio.cachedSongPath(23456))&&f.audio.m_activeDownloadSongId==10000012);
        assert(f.audio.m_activeDownloadProgress==0);f.complete(10000012);f.step();assert(f.audio.resourcesReady());}
    // Invalid NG responses still fail the resource gate without caching HTML.
    for(std::string response:{"-1","10~|~CUSTOMURL","10~|~javascript:bad"}){
        Fixture f(root,{34567});f.audio.downloadAll();web::jobs[0]->callback({200,response});
        assert(f.audio.downloadView().state==RankedResourceDownloadState::Failed&&web::jobs.size()==1);}
    {Fixture f(root,{45678});f.audio.downloadAll();web::jobs[0]->callback({200,"10~|~https://audio.example/file"});
        web::jobs[1]->callback({200,std::string(2048,'<')});assert(!f.audio.resourcesReady());
        assert(!std::filesystem::exists(f.audio.cachedSongPath(45678)));}
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-audio-download-'));
    try {
        writeFileSync(join(dir, 'AudioConfigDouble.hpp'), `#pragma once\n#include "${join(root, 'src/domain/RankAssetPolicy.hpp')}"\nnamespace corum::ranked {${config}}\n`);
        writeFileSync(join(dir, 'AudioManagerUnderTest.hpp'), header);
        writeFileSync(join(dir, 'test.cpp'), cpp);
        const binary = join(dir, 'test');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
            ...(debug ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []), join(dir, 'test.cpp'), '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [join(dir, 'files')], {stdio:'pipe'});
    } finally {rmSync(dir, {recursive:true, force:true});}
});
