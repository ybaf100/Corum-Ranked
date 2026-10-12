// Real updater callbacks and package replacement; network/archive/SDK calls
// are doubles. This does not certify installation/relaunch on an iPad.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function run(code,flags=[]){const dir=mkdtempSync(join(tmpdir(),'corum-update-'));try{
 writeFileSync(join(dir,'test.cpp'),code);
 execFileSync('g++',['-std=c++23','-pthread','-Wall','-Wextra','-Wpedantic','-Werror',...flags,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[join(dir,'cache')],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
for(const debug of [false,true])test(`channel pin and immutable package URL (${debug?'Debug':'Release'})`,()=>run(`
#include "${join(root,'src/domain/ModUpdatePolicy.hpp')}"
#include <cassert>
int main(){using namespace corum::ranked;assert(rankedBuildChannel()=="${debug?'debug':'release'}");
 ModUpdateMetadata m{"v0.5.0-beta.56",std::string(64,'a'),rankedBuildChannel(),100};assert(m.valid());
 assert(m.downloadUrl()=="https://github.com/ybaf100/corum-ranked-downloads/releases/download/${debug?'debug':'release'}-v0.5.0-beta.56/Corum-Ranked-v0.5.0-beta.56-${debug?'debug':'release'}.geode");
 m.url="https://downloads.example.org/ranked/${debug?'debug':'release'}-v0.5.0-beta.56/Corum-Ranked-v0.5.0-beta.56-${debug?'debug':'release'}.geode";
 assert(m.valid() && m.downloadUrl()==m.url);
 for(auto bad:{"http://downloads.example.org/mod.geode","https://user:password@example.org/mod.geode","https://example.org/mod.geode#fragment","https://example.org/mod.geode\\n"}){m.url=bad;assert(!m.valid());}
 m.url.clear();
 for(auto bad:{"../bad","v0.5.0?other","v0.5.0/x"}){m.version=bad;assert(!m.valid());}
 m.version="v0.5.0-beta.56";m.sha256="wrong";assert(!m.valid());m.sha256=std::string(64,'a');
 m.sizeBytes=0;assert(!m.valid());m.sizeBytes=kMaxModUpdateBytes+1;assert(!m.valid());
}`,debug?['-DCORUM_RANKED_DEBUG_BOT_MATCH=1']:[]));

test('actual updater rejects wrong packages/stale callbacks, preserves rollback, and restarts only on player request',()=>{
 const header=read('src/ui/RankedModUpdateManager.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('private:','public:');
 const actual=read('src/ui/RankedModUpdateManager.cpp').replace(/^#include.*$/gm,'');
 run(`
#include "${join(root,'src/domain/ModUpdatePolicy.hpp')}"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>
template<class T>struct Result{std::optional<T> value;bool isErr()const{return !value;}T&unwrap(){return *value;}
 T unwrapOr(T fallback)const{return value?*value:fallback;}};
struct VersionInfo{std::string text="v0.5.0-beta.55";static Result<VersionInfo>parse(std::string s){
 if(!s.starts_with("v0.5.0-beta.")){return {};}return {VersionInfo{s}};}
 std::string toVString()const{return text;}bool operator==(VersionInfo const&)const=default;
 bool operator<(VersionInfo const&o)const{return std::stoi(text.substr(text.rfind('.')+1))<std::stoi(o.text.substr(o.text.rfind('.')+1));}};
std::filesystem::path cacheRoot;bool compatible=true,hasBinary=true;std::string manifestId="hwanhee1.corum_ranked";
std::string markerChannel="release",markerVersion="v0.5.0-beta.56";
struct Mod{static Mod*get(){static Mod m;return &m;}std::string getID(){return "hwanhee1.corum_ranked";}
 VersionInfo getVersion(){return {};}std::filesystem::path getSaveDir(){return cacheRoot;}
 std::filesystem::path getPackagePath(){return cacheRoot/"installed.geode";}};
struct ModMetadata{static ModMetadata createFromGeodeFile(std::filesystem::path const&){return {};}
 bool hasErrors(){return false;}std::string getID(){return manifestId;}VersionInfo getVersion(){return {markerVersion};}
 Result<int>checkTargetVersions(){return compatible?Result<int>{1}:Result<int>{};}};
namespace matjson{struct Value{std::string key;Value operator[](std::string k)const{return {k};}
 Result<std::string>asString()const{return {key=="channel"?markerChannel:markerVersion};}};
Result<Value>parse(std::string const&){return {Value{}};}}
namespace fmt{template<class...T>std::string format(std::string s,T const&...values){
 auto put=[&](auto const&v){std::ostringstream o;o<<v;auto i=s.find("{}");if(i!=std::string::npos)s.replace(i,2,o.str());};
 (put(values),...);return s;}}
namespace geode::utils::web{
struct WebResponse{int status=200;std::string bytes="PKgood";bool ok()const{return status==200;}int code()const{return status;}
 Result<int>into(std::filesystem::path const&p)const{std::ofstream(p,std::ios::binary)<<bytes;return {1};}};
struct WebProgress{std::size_t downloaded()const{return 1;}};
struct Request{std::string url;};struct WebRequest{void timeout(std::chrono::seconds){}void followRedirects(bool){}
 void onProgress(std::function<void(WebProgress const&)>){}Request get(std::string url){return {url};}};
struct Pending{Request request;std::function<void(WebResponse)>callback;};std::vector<Pending>pending;
}
namespace geode::async{template<class T>struct TaskHolder{void cancel(){}
 void spawn(geode::utils::web::Request r,std::function<void(T)>cb){geode::utils::web::pending.push_back({r,cb});}};}
namespace geode{
std::mutex mutex;std::condition_variable queued;std::vector<std::function<void()>>callbacks;
struct Loader{static Loader*get(){static Loader l;return &l;}void queueInMainThread(std::function<void()>cb){
 {std::lock_guard lock(mutex);callbacks.push_back(std::move(cb));}queued.notify_one();}};
void drain(){std::unique_lock lock(mutex);assert(queued.wait_for(lock,std::chrono::seconds(5),[]{return !callbacks.empty();}));
 auto cb=std::move(callbacks.front());callbacks.erase(callbacks.begin());lock.unlock();cb();}
namespace utils::file{struct Archive{Result<std::vector<unsigned char>>extract(std::filesystem::path const&){return {std::vector<unsigned char>{'P','K'}};}
 bool hasEntry(std::filesystem::path const&){return hasBinary;}};
struct Unzip{static Result<Archive>create(std::filesystem::path const&){return {Archive{}};}};}
namespace utils::game{int restarts=0;void restart(bool save){assert(save);++restarts;}}
namespace prelude{using ::Mod;using ::ModMetadata;using ::VersionInfo;using geode::Loader;namespace web=geode::utils::web;}
}
namespace corum::ranked::ui{struct RankAssetPackManager{static bool hasZipSignature(std::filesystem::path const&p){
 std::ifstream f(p);std::string s;f>>s;return s.starts_with("PK");}
 static std::string sha256File(std::filesystem::path const&p){std::ifstream f(p);std::string s;f>>s;return std::string(64,s=="PKgood"?'a':'b');}};}
#define GEODE_PLATFORM_EXTENSION ".dll"
${header}
${actual}
std::string installed(){std::ifstream f(Mod::get()->getPackagePath());std::string s;f>>s;return s;}
void complete(geode::utils::web::WebResponse response={}){auto cb=geode::utils::web::pending.back().callback;cb(response);}
int main(int argc,char**argv){assert(argc==2);cacheRoot=argv[1];std::filesystem::create_directories(cacheRoot);
 std::ofstream(Mod::get()->getPackagePath())<<"OLD";
 using namespace corum::ranked;using namespace corum::ranked::ui;RankedModUpdateManager manager;
 ModUpdateMetadata m{"v0.5.0-beta.56",std::string(64,'a'),"release",6};
 manager.configure({"v0.5.0-beta.55","","release",0},false);assert(manager.ready());
 manager.configure({"v0.5.0-beta.54",m.sha256,"release",6},true);assert(!manager.canDownload());manager.download();assert(geode::utils::web::pending.empty());
 manager.configure(m,false);assert(!manager.canDownload()&&!manager.ready());
 auto wrong=m;wrong.channel="debug";manager.configure(wrong,true);assert(!manager.canDownload());
 manager.configure(m,true);manager.restart();assert(geode::utils::game::restarts==0);
 manager.download();manager.download();assert(geode::utils::web::pending.size()==1);
 assert(geode::utils::web::pending.back().request.url==m.downloadUrl());complete({404,""});assert(installed()=="OLD");
 manager.download();complete({200,"PKbad!"});geode::drain();assert(manager.view().state==ModUpdateState::Failed&&installed()=="OLD");
 markerChannel="debug";manager.download();complete();geode::drain();assert(installed()=="OLD"&&!manager.ready());
 markerChannel="release";hasBinary=false;manager.download();complete();geode::drain();assert(installed()=="OLD");hasBinary=true;
 compatible=false;manager.download();complete();geode::drain();assert(installed()=="OLD");compatible=true;
 manifestId="other.mod";manager.download();complete();geode::drain();assert(installed()=="OLD");manifestId="hwanhee1.corum_ranked";
 manager.download();complete();manager.configure(m,true);geode::drain();assert(installed()=="OLD");
 manager.download();complete();geode::drain();assert(installed()=="PKgood");
 assert(std::filesystem::exists(cacheRoot/"installed.geode.ranked-backup"));
 assert(manager.view().state==ModUpdateState::RestartRequired&&geode::utils::game::restarts==0);
 manager.configure(m,true);assert(!manager.ready());manager.restart();assert(geode::utils::game::restarts==1);
}
`);
});
