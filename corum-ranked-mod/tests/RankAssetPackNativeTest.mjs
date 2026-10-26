// Real cache verification/activation bodies; HTTP, JSON decoding and Geode are
// boundary doubles. This does not substitute for native/device installation.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function body(source,marker){const start=source.indexOf(marker);assert(start>=0,marker);
let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
function run(source){const dir=mkdtempSync(join(tmpdir(),'corum-asset-native-'));
try{writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-pthread','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
execFileSync(join(dir,'test'),[join(dir,'cache')],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}}

test('exact-tag metadata rejects traversal, wrong hashes and oversized ZIPs; inventory covers all 61 PNGs',()=>{
    const spec=JSON.parse(read('asset-pack.json'));
    const policy=read('src/domain/RankAssetPolicy.hpp');
    assert.equal(spec.files.length,61);
    for(const name of spec.files) assert(policy.includes('"'+name+'"'));
    run(`#include "${join(root,'src/domain/RankAssetPolicy.hpp')}"
#include <cassert>
int main(){using namespace corum::ranked;RankAssetMetadata m{true,"visuals-v1",std::string(64,'a'),100};
assert(m.configured());assert(m.downloadUrl()=="https://github.com/ybaf100/corum-ranked-assets/releases/download/visuals-v1/Corum-Ranked-Assets.zip");
for(auto v:{".","..","../other","a/b","bad?tag"}){m.assetPackVersion=v;assert(!m.configured()&&m.downloadUrl().empty());}
m.assetPackVersion="visuals-v2";assert(m.configured());m.sha256="bad";assert(!m.configured());
m.sha256=std::string(64,'a');m.sizeBytes=67108865;assert(!m.configured());m.sizeBytes=0;assert(!m.configured());
assert(!rankAssetEntryAllowed("../rank-gold-body.png"));assert(!rankAssetEntryAllowed("resources/rank-gold-body.png"));
assert(!rankAssetEntryAllowed("mod.json"));assert(rankAssetEntryAllowed("corum-fire-perfect.png"));
assert(rankAssetEntryAllowed("corum-fire-promotion.png"));assert(kRankVisualFiles.size()==61);}
`);
});

test('GitHub redirect headers cannot reject a verified ZIP; HTTP, payload and stale responses remain gated',()=>{
    const source=read('src/ui/RankAssetPackManager.cpp');
    const header=read('src/ui/RankAssetPackManager.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('private:','public:');
    const methods=[
        'std::filesystem::path RankAssetPackManager::rootDir()',
        'bool RankAssetPackManager::hasZipSignature(', 'std::string RankAssetPackManager::sha256File(',
        'void RankAssetPackManager::fail(', 'void RankAssetPackManager::downloadConfiguredPack()',
        'void RankAssetPackManager::finishDownloadedPack(',
    ].map(n=>body(source,n)).join('\n');
    // Execute the real request callback and size/signature/SHA verifier. HTTP,
    // archive decoding, manifest decoding and the main-thread queue are doubles.
    // Geode 5.10.1 appends headers across redirects and header() returns front().
    run(`
#include "${join(root,'src/domain/RankAssetPolicy.hpp')}"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <vector>
template<class T>struct Result {std::optional<T> value;bool isErr()const{return !value;}T& unwrap(){return *value;}};
std::filesystem::path cacheRoot;
struct Mod {static Mod* get(){static Mod m;return &m;}std::filesystem::path getSaveDir(){return cacheRoot;}};
namespace fmt {template<class T>std::string format(std::string text,T const& value){std::ostringstream out;out<<value;
auto pos=text.find("{}");if(pos!=std::string::npos)text.replace(pos,2,out.str());return text;}}
namespace geode::utils::web {
struct WebResponse {
    int status=200;std::string bytes;std::vector<std::string> contentTypes;bool failSave=false;
    bool ok()const{return status>=200&&status<300;}int code()const{return status;}
    std::optional<std::string> header(std::string_view name)const{
        if(name=="content-type"&&!contentTypes.empty()){return contentTypes.front();}return {};}
    Result<int> into(std::filesystem::path const& path)const{if(failSave)return {};
        std::ofstream(path,std::ios::binary).write(bytes.data(),static_cast<std::streamsize>(bytes.size()));return {1};}
};
struct WebProgress {std::size_t bytes=0;std::size_t downloaded()const{return bytes;}};
struct Request {std::string url;bool redirects;std::chrono::seconds timeout;std::function<void(WebProgress const&)> progress;};
struct WebRequest {Request request;
    void timeout(std::chrono::seconds duration){request.timeout=duration;}
    void followRedirects(bool value){request.redirects=value;}
    void onProgress(std::function<void(WebProgress const&)> cb){request.progress=std::move(cb);}
    Request get(std::string url){request.url=std::move(url);return request;}
};
struct Pending {Request request;std::function<void(WebResponse)> callback;};std::vector<Pending> pending;
}
namespace geode::async {template<class T>struct TaskHolder {
void spawn(geode::utils::web::Request request,std::function<void(T)> cb){geode::utils::web::pending.push_back({request,cb});}};}
namespace geode {
std::mutex mutex;std::condition_variable queued;std::vector<std::function<void()>> callbacks;
void queueInMainThread(std::function<void()> cb){{std::lock_guard lock(mutex);callbacks.push_back(std::move(cb));}queued.notify_one();}
void drainWorker(){std::unique_lock lock(mutex);
assert(queued.wait_for(lock,std::chrono::seconds(5),[]{return !callbacks.empty();}));
auto cb=std::move(callbacks.front());callbacks.erase(callbacks.begin());lock.unlock();cb();}
namespace utils::file {
int opens=0,extractions=0;
struct Archive {
std::vector<std::filesystem::path> getEntries()const{std::vector<std::filesystem::path> entries;
for(auto name:corum::ranked::kRankVisualFiles)entries.emplace_back(name);
entries.emplace_back("GLOBED-LICENSE.txt");entries.emplace_back("asset-manifest.json");return entries;}
Result<int> extractTo(std::filesystem::path const&,std::filesystem::path const& path){++extractions;std::ofstream(path)<<"fixture";return {1};}
};
struct Unzip {static Result<Archive> create(std::filesystem::path const&){++opens;return {Archive{}};}};
}
namespace prelude {using ::Mod;namespace web=geode::utils::web;
namespace log {template<class...T>void warn(T...){}template<class...T>void info(T...){}}}
}
${header}
namespace corum::ranked::ui {
using namespace geode::prelude;
constexpr auto kDownloadTimeout=std::chrono::seconds(120);
${body(source,'std::string lower(')}
${body(read('src/domain/Sha256.hpp'),'class Sha256 final')};
RankAssetPackManager::RankAssetPackManager(){}
bool RankAssetPackManager::verifyDirectory(std::filesystem::path const&,std::string const&){return true;}
void RankAssetPackManager::activateDownloadedPack(std::filesystem::path const& temp,
    std::filesystem::path const& zip,RankAssetPackMetadata const&,std::uint64_t generation,std::string error){
std::filesystem::remove_all(temp);std::filesystem::remove(zip);if(generation!=m_downloadGeneration)return;
if(!error.empty())fail(std::move(error));else{m_view.state=RankAssetPackState::Ready;m_view.verifying=false;}}
${methods}
}
using namespace corum::ranked;using namespace corum::ranked::ui;
namespace web=geode::utils::web;
void complete(RankAssetPackManager& manager,web::WebResponse response){
assert(!web::pending.empty());auto pending=std::move(web::pending.back());web::pending.pop_back();
pending.callback(std::move(response));if(manager.m_view.verifying)geode::drainWorker();}
int main(int argc,char** argv){assert(argc==2);cacheRoot=argv[1];std::filesystem::create_directories(cacheRoot);
std::string zipBytes="PK";zipBytes.push_back(3);zipBytes.push_back(4);zipBytes+="verified ZIP boundary payload";
auto fixture=cacheRoot/"fixture.zip";std::ofstream(fixture,std::ios::binary)<<zipBytes;
RankAssetMetadata metadata{true,"visuals-v1",RankAssetPackManager::sha256File(fixture),zipBytes.size()};
web::WebResponse redirected{200,zipBytes,{"text/html; charset=utf-8","application/octet-stream"},false};
assert(redirected.header("content-type")->find("text/html")!=std::string::npos);
RankAssetPackManager manager;manager.m_metadata=metadata;manager.downloadConfiguredPack();manager.downloadConfiguredPack();
assert(web::pending.size()==1);assert(web::pending.back().request.redirects);
assert(web::pending.back().request.timeout==std::chrono::seconds(120));
assert(web::pending.back().request.url==metadata.downloadUrl());
complete(manager,redirected);
assert(manager.m_view.state==RankAssetPackState::Ready);assert(geode::utils::file::opens==1&&geode::utils::file::extractions==63);
// Missing or misleading Content-Type cannot replace integrity validation.
manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();
auto noHeader=redirected;noHeader.contentTypes.clear();complete(manager,noHeader);assert(manager.m_view.state==RankAssetPackState::Ready);
auto bad=redirected;bad.bytes=std::string(zipBytes.size(),' ');bad.bytes.replace(0,6,"<html>");
manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();complete(manager,bad);
assert(manager.m_view.state==RankAssetPackState::Failed&&manager.m_view.error=="asset download is not a ZIP");
bad=redirected;bad.bytes.pop_back();manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();complete(manager,bad);
assert(manager.m_view.error=="asset ZIP size mismatch");
bad=redirected;bad.bytes.back()='X';manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();complete(manager,bad);
assert(manager.m_view.error=="asset ZIP SHA-256 mismatch");assert(geode::utils::file::opens==2);
bad=redirected;bad.status=404;manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();complete(manager,bad);
assert(manager.m_view.state==RankAssetPackState::Failed&&manager.m_view.error=="asset release HTTP 404");
bad=redirected;bad.failSave=true;manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();complete(manager,bad);
assert(manager.m_view.error=="failed to save asset ZIP");
manager.m_view.state=RankAssetPackState::Disabled;manager.downloadConfiguredPack();auto staleGeneration=manager.m_downloadGeneration;
++manager.m_downloadGeneration;manager.m_view.state=RankAssetPackState::Disabled;complete(manager,redirected);
assert(manager.m_view.state==RankAssetPackState::Disabled&&geode::utils::file::opens==2);
assert(!std::filesystem::exists(manager.rootDir()/fmt::format("download-{}.zip.tmp",staleGeneration)));
}
`);
});

test('actual cache hashes catch corruption; atomic update rolls back, stale completion cannot activate, and failed pack blocks Continue',()=>{
    const source=read('src/ui/RankAssetPackManager.cpp');
    const header=read('src/ui/RankAssetPackManager.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('private:','public:');
    const jsonSource=read('tests/SpectatorLiveViewerNativeTest.mjs');
    let json=jsonSource.slice(jsonSource.indexOf('namespace matjson {'),jsonSource.indexOf('${readFileSync(join(root,'));
    json=json.replace('bool isErr()const','bool isOk()const{return value.has_value();} bool isErr()const');
    json=json.replace('std::string dump()const{return "{}";}', '').replace('bool contains(std::string const& k)const',
        'std::string dump()const{if(auto p=std::get_if<std::string>(&data))return *p;if(auto p=std::get_if<Object>(&data)){std::string out;for(auto const& [k,v]:*p)out+=k+v.dump();return out;}return "fixture";} bool contains(std::string const& k)const');
    const methods=[
        'void RankAssetPackManager::configure(', 'void RankAssetPackManager::prepare()', 'void RankAssetPackManager::retry()',
        'RankAssetPackView const& RankAssetPackManager::view()', 'bool RankAssetPackManager::ready()',
        'std::filesystem::path RankAssetPackManager::rootDir()', 'std::filesystem::path RankAssetPackManager::activeDirFor(',
        'bool RankAssetPackManager::validSha256Text(', 'bool RankAssetPackManager::validVersionText(',
        'bool RankAssetPackManager::hasZipSignature(', 'bool RankAssetPackManager::requiredAssetsPresent(',
        'std::string RankAssetPackManager::sha256File(', 'bool RankAssetPackManager::verifyDirectory(',
        'void RankAssetPackManager::loadCurrent()', 'bool RankAssetPackManager::activateExisting(',
        'void RankAssetPackManager::fail(', 'void RankAssetPackManager::activateDownloadedPack(',
        'std::filesystem::path RankAssetPackManager::resourcePath(',
    ].map(n=>body(source,n)).join('\n');
    run(`
#include "${join(root,'src/domain/RankAssetPolicy.hpp')}"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <variant>
#include <vector>
${json}
std::filesystem::path cacheRoot;
struct Mod {static Mod* get(){static Mod m;return &m;}std::filesystem::path getSaveDir(){return cacheRoot;}
std::filesystem::path getResourcesDir(){return cacheRoot/"bundled";}};
namespace geode {namespace utils::web {struct WebResponse{};}
namespace async {template<class T>struct TaskHolder {int cancels=0;void cancel(){++cancels;}};}
namespace utils::file {
bool failWrite=false;std::map<std::filesystem::path,matjson::Value> documents;
matjson::Result<matjson::Value> readJson(std::filesystem::path const& path){auto it=documents.find(path);if(it!=documents.end())return {it->second};
std::ifstream in(path);std::string version;in>>version;
for(auto const& [p,v]:documents){(void)p;if(v["assetPackVersion"].asString().unwrapOr("")==version)return {v};}
return {};}
matjson::Result<int> writeStringSafe(std::filesystem::path const& path,std::string text){if(failWrite)return {};std::ofstream(path)<<text;return {1};}
} namespace prelude {using ::Mod;namespace log {template<class...T>void warn(T...){}template<class...T>void info(T...){}}}}
${header}
namespace corum::ranked::ui {
using namespace geode::prelude;
${body(source,'std::string lower(')}
${body(read('src/domain/Sha256.hpp'),'class Sha256 final')} ;
RankAssetPackManager::RankAssetPackManager() {}
int networkRequests=0;
void RankAssetPackManager::downloadConfiguredPack(){if(m_view.state==RankAssetPackState::Downloading||!m_metadata.configured())return;
++networkRequests;++m_downloadGeneration;m_view.state=RankAssetPackState::Downloading;}
${methods}
}
using namespace corum::ranked;using namespace corum::ranked::ui;
void verifiedFiles(std::filesystem::path dir,std::string version){
std::filesystem::create_directories(dir);matjson::Value doc;doc["schema"]=1;doc["assetPackVersion"]=version;matjson::Value::Array files;
for(auto name:kRankVisualFiles){auto path=dir/std::string(name);std::ofstream(path)<<"PNG-"<<version;matjson::Value item;
item["name"]=std::string(name);item["sizeBytes"]=static_cast<std::int64_t>(std::filesystem::file_size(path));item["sha256"]=RankAssetPackManager::sha256File(path);files.push_back(item);}
auto license=dir/"GLOBED-LICENSE.txt";std::ofstream(license)<<"MIT";matjson::Value item;item["name"]="GLOBED-LICENSE.txt";
item["sizeBytes"]=3;item["sha256"]=RankAssetPackManager::sha256File(license);files.push_back(item);doc["files"]=files;
std::ofstream(dir/"asset-manifest.json")<<version;geode::utils::file::documents[dir/"asset-manifest.json"]=doc;
}
int main(int argc,char** argv){assert(argc==2);cacheRoot=argv[1];std::filesystem::create_directories(cacheRoot);
auto abc=cacheRoot/"abc";std::ofstream(abc)<<"abc";
assert(RankAssetPackManager::sha256File(abc)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
auto empty=cacheRoot/"empty";std::ofstream(empty)<<"";assert(RankAssetPackManager::sha256File(empty)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
RankAssetPackManager manager;RankAssetMetadata v1{true,"visuals-v1",std::string(64,'a'),100};
RankAssetMetadata v2{true,"visuals-v2",std::string(64,'b'),110};
manager.configure(v1);assert(manager.requiresDownload()&&!manager.view().update);
auto cancels=manager.m_downloadRequest.cancels;manager.configure(v1);assert(manager.m_downloadRequest.cancels==cancels);
manager.prepare();manager.prepare();assert(networkRequests==1);
auto temp=manager.rootDir()/"one.tmp";verifiedFiles(temp,"visuals-v1");
assert(RankAssetPackManager::verifyDirectory(temp,"visuals-v1"));assert(!RankAssetPackManager::verifyDirectory(temp,"visuals-v2"));
manager.activateDownloadedPack(temp,cacheRoot/"one.zip",v1,manager.m_downloadGeneration,"");
assert(manager.ready()&&!manager.requiresDownload()&&manager.view().activeVersion=="visuals-v1");auto installed=manager.m_activeDir;
manager.configure(v2);assert(manager.requiresDownload()&&manager.view().update&&manager.m_activeDir==installed);
temp=manager.rootDir()/"two.tmp";verifiedFiles(temp,"visuals-v2");
geode::utils::file::failWrite=true;
manager.activateDownloadedPack(temp,cacheRoot/"two.zip",v2,manager.m_downloadGeneration,"");
assert(manager.view().state==RankAssetPackState::Failed&&manager.requiresDownload());
assert(manager.m_activeDir==installed&&RankAssetPackManager::verifyDirectory(installed,"visuals-v1"));
assert(!std::filesystem::exists(manager.activeDirFor(v2)));
geode::utils::file::failWrite=false;manager.retry();auto gen=manager.m_downloadGeneration;
temp=manager.rootDir()/"stale.tmp";verifiedFiles(temp,"visuals-v1");
manager.activateDownloadedPack(temp,cacheRoot/"stale.zip",v1,gen-1,"");
assert(!std::filesystem::exists(temp)&&manager.m_activeDir==installed&&manager.requiresDownload());
temp=manager.rootDir()/"bad.tmp";verifiedFiles(temp,"visuals-v2");
manager.activateDownloadedPack(temp,cacheRoot/"bad.zip",v2,gen,"asset ZIP SHA-256 mismatch");
assert(manager.m_activeDir==installed&&manager.requiresDownload()&&!std::filesystem::exists(temp));
manager.retry();temp=manager.rootDir()/"good.tmp";verifiedFiles(temp,"visuals-v2");
manager.activateDownloadedPack(temp,cacheRoot/"good.zip",v2,manager.m_downloadGeneration,"");
assert(manager.ready()&&manager.view().progress==1&&manager.view().activeVersion=="visuals-v2");
assert(!std::filesystem::exists(installed));auto active=manager.m_activeDir;
auto image=active/"rank-gold-body.png";std::ofstream(image,std::ios::app)<<"corruption";
assert(!RankAssetPackManager::verifyDirectory(active,"visuals-v2"));
manager.configure({});manager.configure(v2);assert(manager.requiresDownload()&&manager.view().update);
assert(manager.resourcePath("../outside.png").empty());
std::filesystem::remove(image);std::filesystem::create_symlink(abc,image);assert(!RankAssetPackManager::requiredAssetsPresent(active));
}
`);
    const screen=read('src/RankedPopup.cpp');
    assert.match(body(screen,'void onContinueResources('),/requiresDownload\(\)/);
    assert.match(body(screen,'bool resourceWorkMissing()'),/requiresDownload\(\)/);
    assert.match(body(screen,'void syncResources()'),/assets.configure/);
    assert.doesNotMatch(body(screen,'void syncResources()'),/assets.prepare/);
    assert.match(source,/request.get\(m_metadata.downloadUrl\(\)\)/);
    assert.doesNotMatch(source,/releases\/latest|drive.google/);
});
