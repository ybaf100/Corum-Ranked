import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const noIncludes=s=>s.replace(/^\s*#(?:include|pragma once).*$/gm,'');
function run(body){const dir=mkdtempSync(join(tmpdir(),'survival-thumbnails-'));try{
 writeFileSync(join(dir,'test.cpp'),body);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-misleading-indentation','-pthread','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[dir],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
const fixture=String.raw`
#include "domain/SurvivalThumbnailPolicy.hpp"
#include <vector>
#include <cassert>
using namespace corum::ranked;
std::vector<std::uint8_t> webp(unsigned w=640,unsigned h=360){
 std::vector<std::uint8_t> b{'R','I','F','F',22,0,0,0,'W','E','B','P','V','P','8',' ',10,0,0,0,0,0,0,0x9d,1,0x2a,0,0,0,0};
 b[26]=w&255;b[27]=w>>8;b[28]=h&255;b[29]=h>>8;return b;
}
`;
test('canonical thumbnail redirects reject foreign hosts, query credentials, invalid IDs and malformed paths',()=>run(fixture+String.raw`
int main(){for(auto const* path:{"/thumbnail/148827436/small","https://levelthumbs.prevter.me/thumbnail/148827436/small"})assert(survivalThumbnailRedirect(path)==148827436ULL);
 for(auto const* path:{"","https://evil.test/thumbnail/42/small","https://levelthumbs.prevter.me.evil.test/thumbnail/42/small","//evil.test/thumbnail/42/small","http://levelthumbs.prevter.me/thumbnail/42/small","/thumbnail/0/small","/thumbnail/01/small","/thumbnail/-1/small","/thumbnail/10000000000/small","/thumbnail/42/small?token=x","/thumbnail/42/medium","/thumbnail/../small","/thumbnail/%34%32/small","/thumbnail/42/small#x"})assert(!survivalThumbnailRedirect(path));
 assert(survivalThumbnailUrl(148827436,true)=="https://levelthumbs.prevter.me/thumbnail/148827436/medium");assert(survivalThumbnailUrl(0,false).empty());assert(survivalThumbnailUrl(10000000000ULL,false).empty());
}`));
test('WebP limits validate every RIFF dimension, bitstream, chunk boundary and animation flag before decoding',()=>run(fixture+String.raw`
void size(std::vector<std::uint8_t>& b){auto n=b.size()-8;for(int i=0;i<4;i++)b[4+i]=(n>>(8*i))&255;}
int main(){assert(survivalWebpSize(webp())==std::pair(640U,360U));assert(survivalWebpSize(webp(1280,720))==std::pair(1280U,720U));
 for(auto [w,h]:{std::pair{0U,1U},std::pair{1281U,720U},std::pair{640U,721U},std::pair{16000U,16000U}})assert(!survivalWebpSize(webp(w,h)));
 auto b=webp();b.pop_back();assert(!survivalWebpSize(b));b=webp();b.push_back(0);size(b);assert(!survivalWebpSize(b));
 b=webp();b[16]=255;assert(!survivalWebpSize(b));b=webp();b[0]='x';assert(!survivalWebpSize(b));
 b=webp();auto extra=webp();b.insert(b.end(),extra.begin()+12,extra.end());size(b);assert(!survivalWebpSize(b));
 auto extended=[&](unsigned w,unsigned h,unsigned flags){auto b=webp();std::vector<std::uint8_t> x{'V','P','8','X',10,0,0,0,static_cast<std::uint8_t>(flags),0,0,0,0,0,0,0,0,0};--w;--h;for(int i=0;i<3;i++){x[12+i]=(w>>(8*i))&255;x[15+i]=(h>>(8*i))&255;}b.insert(b.begin()+12,x.begin(),x.end());size(b);return b;};
 assert(survivalWebpSize(extended(640,360,0)));assert(!survivalWebpSize(extended(1280,720,0)));assert(!survivalWebpSize(extended(640,360,2)));
 b=webp();b.insert(b.end(),{'A','N','I','M',0,0,0,0});size(b);assert(!survivalWebpSize(b));
 b=webp();b.resize(SurvivalThumbnailMaxBytes+1);size(b);assert(!survivalWebpSize(b));
 // Truncated/adversarial headers must be a normal empty result, never an OOB read.
 b=webp();for(std::size_t n=0;n<b.size();n++)assert(!survivalWebpSize(std::span(b).first(n)));
}`));

function runProvider(windowsRpcMacro=false,debug=false){
 const provider=noIncludes(read('src/SurvivalThumbnailProvider.cpp')).replace(/std::int64_t SurvivalThumbnailProvider::now\(\)\{[^\n]+\}/,'std::int64_t SurvivalThumbnailProvider::now(){return fakeNow;}');
 run(String.raw`
#include "domain/SurvivalMapSelection.hpp"
#include "domain/SurvivalThumbnailPolicy.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cassert>
using GLubyte=unsigned char;
std::int64_t fakeNow=100;
std::filesystem::path saveRoot;
auto mainThread=std::this_thread::get_id();
struct RefCount {int refs=1;virtual ~RefCount()=default;void retain(){++refs;}void release(){if(!--refs)delete this;}};
namespace cocos2d {
struct CCNode:RefCount {bool init(){return true;}virtual void update(float){} };
struct CCImage {unsigned w=0,h=0;static inline std::atomic_int decoded=0;static inline bool reject=false;
 bool initWithImageData(void* data,int length){assert(std::this_thread::get_id()!=mainThread);++decoded;
  auto size=corum::ranked::survivalWebpSize(std::span(static_cast<std::uint8_t*>(data),length));if(!size||reject)return false;w=size->first;h=size->second;return true;}
 unsigned getWidth()const{return w;}unsigned getHeight()const{return h;}};
struct CCTexture2D:RefCount {static inline int uploads=0;bool initWithImage(CCImage*){assert(std::this_thread::get_id()==mainThread);++uploads;return true;}};
}
namespace geode {
template<class T>struct Ref {T* ptr=nullptr;Ref()=default;Ref(T* p):ptr(p){if(ptr)ptr->retain();}Ref(Ref const& r):Ref(r.ptr){}Ref(Ref&& r):ptr(std::exchange(r.ptr,nullptr)){}~Ref(){if(ptr)ptr->release();}
 Ref& operator=(Ref r){std::swap(ptr,r.ptr);return *this;}T* operator->()const{return ptr;}explicit operator bool()const{return ptr;}T* data()const{return ptr;}};
struct Loader {bool thumbnails=false,imageplus=false;static Loader* get(){static Loader l;return &l;}bool isModLoaded(std::string const& id){return id=="cdc.level_thumbnails"?thumbnails:id=="prevter.imageplus"?imageplus:false;}};
struct Mod {static Mod* get(){static Mod m;return &m;}auto getSaveDir(){return saveRoot;}};
namespace log {template<class...Args>void info(Args const&...){} }
namespace utils::web {
struct WebProgress {std::size_t count=0,total=0;auto downloaded()const{return count;}auto downloadTotal()const{return total;}};
struct WebResponse {int status=200;std::vector<std::uint8_t> bytes;std::map<std::string,std::string> values;
 int code()const{return status;}auto header(std::string const& key)const->std::optional<std::string>{auto it=values.find(key);return it==values.end()?std::nullopt:std::optional(it->second);}
 auto headers()const{std::vector<std::string> names;for(auto const& [k,v]:values)names.push_back(k);return names;}
 auto const& data()const&{return bytes;}auto data()&&{return std::move(bytes);}};
struct Ticket {std::string url,agent;bool redirect=true,tls=false;int timeout=0;std::function<void(WebProgress const&)> progress;};
struct WebRequest {Ticket request;auto& userAgent(std::string value){request.agent=value;return *this;}auto& timeout(std::chrono::seconds value){request.timeout=value.count();return *this;}
 auto& followRedirects(bool value){request.redirect=value;return *this;}auto& certVerification(bool value){request.tls=value;return *this;}
 template<class F>void onProgress(F f){request.progress=f;}Ticket get(std::string url){request.url=url;return request;}};
}
struct Flight {utils::web::Ticket ticket;bool cancelled=false,done=false;std::function<void(utils::web::WebResponse)> callback;};
std::vector<Flight> flights;
namespace async {template<class T>struct TaskHolder {std::size_t index=static_cast<std::size_t>(-1);void cancel(){if(index<flights.size())flights[index].cancelled=true;}
 template<class F>void spawn(utils::web::Ticket ticket,F callback){index=flights.size();flights.push_back({ticket,false,false,callback});}};}
struct Scheduler {void scheduleUpdateForTarget(cocos2d::CCNode*,int,bool){} };
struct CCDirector {static CCDirector* sharedDirector(){static CCDirector d;return &d;}Scheduler* getScheduler(){static Scheduler s;return &s;}};
namespace prelude {using namespace cocos2d;using geode::Ref;using geode::Loader;using geode::Mod;using geode::CCDirector;namespace web=geode::utils::web;namespace log=geode::log;}
}
`+noIncludes(read('src/SurvivalThumbnailProvider.hpp'))+
// Windows RPC headers define this macro through the native SDK's PCH.
(windowsRpcMacro?'\n#define small char\n':'')+
(debug?'\n#define CORUM_RANKED_DEBUG_BOT_MATCH 1\n':'')+provider+fixture+String.raw`
using namespace geode;using namespace geode::prelude;
void pump(SurvivalThumbnailProvider& p,int count=40){for(int i=0;i<count;i++){p.update(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
void reply(std::size_t i,int code,std::vector<std::uint8_t> bytes={},std::map<std::string,std::string> headers={}){auto callback=flights.at(i).callback;flights[i].done=true;callback({code,std::move(bytes),std::move(headers)});}
std::size_t active(){return std::count_if(flights.begin(),flights.end(),[](auto const& f){return !f.done&&!f.cancelled;});}
int main(int argc,char**argv){assert(argc==2);saveRoot=argv[1];auto dir=saveRoot/"survival-thumbnails-v1";std::filesystem::create_directories(dir);
 for(int i=0;i<40;i++){std::ofstream out(dir/(std::to_string(i)+".webp"));out<<"x";}
 auto& p=SurvivalThumbnailProvider::get();assert(std::distance(std::filesystem::directory_iterator(dir),std::filesystem::directory_iterator{})<=32);
 SurvivalSelectionContext a{"a","R1",1,100,1};p.activate(a);pump(p);assert(flights.empty());
 Loader::get()->thumbnails=true;p.activate(a);pump(p);assert(flights.empty()); // dependency absent/disabled
 Loader::get()->imageplus=true;p.activate(a);pump(p);assert(active()==2);
 for(auto const& f:flights){assert(!f.ticket.redirect&&f.ticket.tls&&f.ticket.timeout==4);assert(f.ticket.agent=="CorumRanked-SurvivalThumbnails/1");assert(f.ticket.url.starts_with(SurvivalThumbnailOrigin));}
 auto old=flights.size()-1;auto b=a;b.levelId=200;b.sceneGeneration=2;p.activate(b);assert(flights[old].cancelled);
 reply(old,200,webp(),{{"Content-Type","image/webp"}});pump(p);assert(!p.selected());assert(p.previews().empty());assert(active()<=2);
 // Current authoritative medium succeeds; a missing small is normal negative cache.
 bool medium=false;for(std::size_t i=0;i<flights.size();i++)if(!flights[i].cancelled&&!flights[i].done){if(flights[i].ticket.url.ends_with("/200/medium")){reply(i,200,webp(1280,720),{{"content-type","image/webp"}});medium=true;}else reply(i,404);}
 assert(medium);pump(p);assert(p.selected());assert(CCImage::decoded>0&&CCTexture2D::uploads==1);
 // The disk cache is reusable without a new HTTP request for the selected medium.
 auto before=flights.size();p.deactivate();auto c=b;c.sceneGeneration=3;p.activate(c);pump(p);assert(p.selected());
 for(std::size_t i=before;i<flights.size();i++)assert(!flights[i].ticket.url.ends_with("/200/medium")&&!flights[i].ticket.url.ends_with("/200/small"));
 // Foreign redirects are never followed. Valid 302s re-enter the canonical host.
 bool sentForeign=false,sentValid=false,retried=false;std::size_t transient=0;
 for(int round=0;round<55;++round){auto end=flights.size();for(std::size_t i=0;i<end;i++)if(!flights[i].done&&!flights[i].cancelled){auto url=flights[i].ticket.url;
  if(url.ends_with("/random/small")){if(!sentForeign){reply(i,302,{},{{"location","https://evil.test/thumbnail/500/small"}});sentForeign=true;}else{reply(i,302,{},{{"lOcAtIoN","/thumbnail/501/small"}});sentValid=true;}}
  else if(url.ends_with("/501/small")){if(!transient){transient=i+1;reply(i,503);}else{retried=true;reply(i,404);}}
  else reply(i,404);
 }fakeNow+=1001;pump(p,3);assert(active()<=2);}
 assert(sentForeign&&sentValid&&retried);assert(flights.size()-before<=24);
 for(auto const& f:flights)assert(f.ticket.url.starts_with("https://levelthumbs.prevter.me/thumbnail/"));
 // Late response after app pause has no texture/IO publication, and a stale scene cannot cancel its successor.
 auto d=c;d.levelId=300;d.sceneGeneration=4;p.activate(d);pump(p);auto inFlight=flights.size()-1;
 auto epoch=SurvivalThumbnailProvider::applicationEpoch();SurvivalThumbnailProvider::onApplicationPause();assert(SurvivalThumbnailProvider::applicationEpoch()==epoch+1);
 auto uploads=CCTexture2D::uploads;reply(inFlight,200,webp(),{{"Content-Type","image/webp"}});pump(p);assert(CCTexture2D::uploads==uploads&&!p.selected());
 auto e=d;e.sceneGeneration=5;p.activate(e);pump(p);auto live=active();p.release(d);assert(active()==live);p.release(e);assert(active()==0);
}`);
}
test('actual provider bounds anonymous requests, cancels obsolete replies, handles 302/404/retry/cache and decodes away from GL thread',()=>runProvider());
test('actual provider compiles and runs with the Windows RPC small macro in Debug ON and OFF',()=>{
 for(const debug of [false,true])runProvider(true,debug);
});
