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
try{writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
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

test('actual cache hashes catch corruption; atomic update rolls back, stale completion cannot activate, and failed pack blocks Continue',()=>{
    const source=read('src/ui/RankAssetPackManager.cpp');
    const header=read('src/ui/RankAssetPackManager.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('private:','public:');
    const jsonSource=read('tests/SpectatorLiveViewerNativeTest.mjs');
    let json=jsonSource.slice(jsonSource.indexOf('namespace matjson {'),jsonSource.indexOf('${readFileSync(join(root,'));
    json=json.replace('bool isErr()const','bool isOk()const{return value.has_value();} bool isErr()const');
    json=json.replace('bool contains(std::string const& k)const',
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
${body(source,'class Sha256 final')} ;
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
