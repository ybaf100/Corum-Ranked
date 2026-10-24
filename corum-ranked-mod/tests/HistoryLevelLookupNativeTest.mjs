import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const root=new URL('..',import.meta.url).pathname,read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){let start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
test('actual exact-ID history lookup retains the level through popup closure and routes cancelled responses without touching foreign delegates',()=>{
 const dir=mkdtempSync(join(tmpdir(),'history-level-'));
 const header=read('src/HistoryLevelLookup.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('bool canOpenHistoryLevel();','').replace('void showHistoryLevel(int levelId);','');
 const source=read('src/HistoryLevelLookup.cpp'),hook=method(read('src/RankedLevelPreparation.cpp'),'void onGetOnlineLevelsCompleted(');
 try{
 writeFileSync(join(dir,'test.cpp'),`
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <cstddef>
struct GJGameLevel {int m_levelID=0,refs=0;void retain(){++refs;}void release(){--refs;}};
namespace geode {template<class T>struct Ref {T*p=nullptr;Ref()=default;Ref(std::nullptr_t){}Ref(T*v):p(v){if(p)p->retain();}Ref(Ref const&v):Ref(v.p){}Ref(Ref&&v):p(v.p){v.p=nullptr;}~Ref(){if(p)p->release();}Ref&operator=(Ref v){std::swap(p,v.p);return *this;}T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};}
using geode::Ref;
namespace cocos2d {struct CCArray {std::vector<GJGameLevel*>values;unsigned count()const{return values.size();}GJGameLevel*objectAtIndex(unsigned i){return values.at(i);}};}
using cocos2d::CCArray;template<class T>T typeinfo_cast(GJGameLevel*p){return static_cast<T>(p);}namespace gd{using string=std::string;}
struct LevelManagerDelegate {virtual ~LevelManagerDelegate()=default;virtual void loadLevelsFinished(CCArray*,char const*){}virtual void loadLevelsFinished(CCArray*,char const*,int){}virtual void loadLevelsFailed(char const*){}virtual void loadLevelsFailed(char const*,int){}virtual void setupPageInfo(gd::string,char const*){}};
enum class SearchType{Type19};struct GJSearchObject {std::string query;static GJSearchObject*create(SearchType,std::string q){static GJSearchObject s;s.query=std::move(q);return &s;}std::string getKey()const{return query;}};
namespace fmt{std::string format(char const*,int id,std::uint64_t generation){return std::to_string(id)+"&gameVersion=22&corumHistory="+std::to_string(generation);}}
struct GameLevelManager {LevelManagerDelegate*m_levelManagerDelegate=nullptr;GJGameLevel*saved=nullptr;std::string requested;CCArray results;static GameLevelManager&instance(){static GameLevelManager g;return g;}static GameLevelManager*sharedState(){return &instance();}GJGameLevel*getSavedLevel(int){return saved;}void getOnlineLevels(GJSearchObject*s){requested=s->query;}void onGetOnlineLevelsCompleted(gd::string,gd::string tag){if(m_levelManagerDelegate)m_levelManagerDelegate->loadLevelsFinished(&results,tag.c_str());}};
namespace corum::ranked {struct RankedMapDetails:LevelManagerDelegate{static RankedMapDetails&get(){static RankedMapDetails d;return d;}void remember(GJGameLevel*){}std::uint64_t requestGeneration(char const*)const{return 0;}void responseCompleted(char const*,std::uint64_t){}};struct RankedLevelPreparation:LevelManagerDelegate{static RankedLevelPreparation&get(){static RankedLevelPreparation p;return p;}std::uint64_t requestGeneration(char const*)const{return 0;}void responseCompleted(char const*,std::uint64_t){}};}
namespace log{template<class...T>void warn(T const&...){} }
${header}
namespace corum::ranked {
${['HistoryLevelLookup& HistoryLevelLookup::get(','void HistoryLevelLookup::cancel(','std::uint64_t HistoryLevelLookup::begin(','bool HistoryLevelLookup::ownsResponse(','geode::Ref<GJGameLevel> HistoryLevelLookup::level(','std::string HistoryLevelLookup::error(','void HistoryLevelLookup::loadLevelsFinished(','void HistoryLevelLookup::loadLevelsFailed(','void HistoryLevelLookup::responseCompleted('].map(n=>method(source,n)).join('\n')}
}
struct Router:GameLevelManager{${hook}};
struct Foreign:LevelManagerDelegate{int calls=0;void loadLevelsFinished(CCArray*,char const*)override{++calls;}};
int main(){using namespace corum::ranked;auto& lookup=HistoryLevelLookup::get();auto& manager=GameLevelManager::instance();Router routed;Foreign foreign;routed.m_levelManagerDelegate=&foreign;
 auto a=lookup.begin(123);auto keyA=manager.requested;assert(keyA.starts_with("123&"));assert(!lookup.level(a));
 GJGameLevel wrong{999},exact{123};routed.results.values={&wrong,&exact};routed.onGetOnlineLevelsCompleted("ok",keyA);assert(routed.m_levelManagerDelegate==&foreign&&foreign.calls==0);
 {auto kept=lookup.level(a);assert(kept&&kept->m_levelID==123&&exact.refs==2);lookup.cancel(a);assert(kept->m_levelID==123&&exact.refs==1);}assert(exact.refs==0);
 auto b=lookup.begin(456);auto keyB=manager.requested;routed.onGetOnlineLevelsCompleted("stale",keyA);assert(!lookup.level(b)&&foreign.calls==0);
 routed.results.values={&wrong};routed.onGetOnlineLevelsCompleted("wrong exact ID",keyB);assert(!lookup.level(b)&&!lookup.error(b).empty());lookup.cancel(b);
 manager.saved=&exact;auto saved=lookup.begin(123);assert(lookup.level(saved));lookup.cancel(saved);manager.saved=nullptr;
 auto c=lookup.begin(123);auto keyC=manager.requested;lookup.cancel(c);routed.results.values={&exact};routed.onGetOnlineLevelsCompleted("cancelled",keyC);assert(foreign.calls==0&&!lookup.level(c));
 routed.onGetOnlineLevelsCompleted("foreign","other-key");assert(foreign.calls==1&&routed.m_levelManagerDelegate==&foreign);
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-builtin-declaration-mismatch',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
