import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname,read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){let a=source.indexOf(name);assert(a>=0,name);let b=source.indexOf('{',a)+1,n=1;while(n){n+=(source[b]==='{')-(source[b]==='}');++b;}return source.slice(a,b);}
test('actual filter popup selects directly once, cancels without requests and rejects expired owner/scope/session callbacks',()=>{
 const dir=mkdtempSync(join(tmpdir(),'history-filter-')),popup=read('src/ui/RankedHistoryFilterPopup.hpp'),screen=read('src/RankedPopup.cpp');
 try{
 writeFileSync(join(dir,'test.cpp'),`
#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "${root}/src/domain/RankedMode.hpp"
struct CCObject{int tag=0;int getTag()const{return tag;}};
template<class T>struct Ref{T*p;Ref(T*v):p(v){++p->refs;}~Ref(){--p->refs;}};
template<class T>struct WeakRef{T*p;WeakRef(T*v):p(v){}struct Locked{T*p;T*operator->(){return p;}T*data(){return p;}explicit operator bool()const{return p;}};Locked lock()const{return {p};}};
struct CCTouchDispatcher{int unregistrations=0;static CCTouchDispatcher*get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(void*){++unregistrations;}};
struct Popup{void*parent=nullptr;int closes=0;virtual ~Popup()=default;virtual void onExit(){}void onClose(void*){++closes;parent=nullptr;onExit();}void*getParent(){return parent;}void show(){parent=this;}};
namespace corum::ranked{
${method(read('src/domain/HistoryPresentation.hpp'),'inline std::vector<std::string> historyResultFilters(')}
namespace ui{
struct RankedHistoryFilterChoice{std::string value,caption;};
struct RankedHistoryFilterPopup:Popup{
 std::vector<RankedHistoryFilterChoice>m_choices;std::function<void(std::string)>m_choose;bool m_closed=false,m_exited=false;int refs=0;
 static inline RankedHistoryFilterPopup*last=nullptr;static inline int creates=0;std::string selected;
 static auto*create(std::vector<RankedHistoryFilterChoice> choices,std::string const&selected,std::function<void(std::string)> action){
  auto*p=new RankedHistoryFilterPopup;p->m_choices=std::move(choices);p->m_choose=std::move(action);p->selected=selected;++creates;return last=p;}
 ${method(popup,'    void onSelect(')}
 ${method(popup,'    void dismiss()')}
 ${method(popup,'    void onExit()')}
};}}
struct RankedRuntime{std::string token="session-A";static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken(){return token;}};
struct Layer{
 enum class Page{Live,HistoryList,HistoryDetail};Page m_page=Page::HistoryList;bool running=true;
 corum::ranked::HistoryScope m_historyScope=corum::ranked::HistoryScope::Survival;
 std::optional<WeakRef<corum::ranked::ui::RankedHistoryFilterPopup>>m_historyFilterPopup;
 std::string m_historyResult="ALL";std::optional<float>m_historyListOffset=-80;int reads=0;
 bool isRunning()const{return running;}std::string historyFilterText(std::string const&s){return s;}void onHistory(void*){++reads;}
 ${method(screen,'    void onHistoryFilter(')}
 ${method(screen,'    void closeHistoryFilter()')}
};
int main(){using Filter=corum::ranked::ui::RankedHistoryFilterPopup;using Scope=corum::ranked::HistoryScope;
 Layer layer;layer.onHistoryFilter(nullptr);auto*p=Filter::last;assert(p->getParent()&&p->m_choices.size()==5&&p->selected=="ALL");
 auto count=Filter::creates;layer.onHistoryFilter(nullptr);assert(Filter::creates==count&&layer.reads==0);
 CCObject eliminated{3};assert(p->m_choices[3].value=="ELIMINATED");p->onSelect(&eliminated);
 assert(layer.m_historyResult=="ELIMINATED"&&layer.reads==1&&!layer.m_historyListOffset&&!p->getParent()&&p->refs==0);
 p->onSelect(&eliminated);assert(layer.reads==1);auto unregistered=CCTouchDispatcher::get()->unregistrations;p->onExit();assert(CCTouchDispatcher::get()->unregistrations==unregistered);
 layer.m_historyListOffset=-40;layer.onHistoryFilter(nullptr);p=Filter::last;p->onSelect(&eliminated);assert(layer.reads==1&&layer.m_historyListOffset==-40);
 layer.onHistoryFilter(nullptr);p=Filter::last;p->dismiss();assert(layer.reads==1&&!p->m_choose);
 for(int stale=0;stale<4;++stale){
  Layer next;next.onHistoryFilter(nullptr);auto*old=Filter::last;
  if(stale==0)next.running=false;if(stale==1)next.m_page=Layer::Page::Live;
  if(stale==2)next.m_historyScope=Scope::SoloRanked;if(stale==3)RankedRuntime::get().token="session-B";
  old->onSelect(&eliminated);assert(next.reads==0&&next.m_historyResult=="ALL");RankedRuntime::get().token="session-A";
 }
 for(auto scope:{Scope::SoloRanked,Scope::Survival,Scope::All}){
  Layer modes;modes.m_historyScope=scope;modes.onHistoryFilter(nullptr);auto*current=Filter::last;
  assert(current->m_choices.size()==(scope==Scope::SoloRanked?3u:scope==Scope::Survival?5u:7u));
  CCObject bad{99};current->onSelect(&bad);assert(!current->m_closed&&modes.reads==0);modes.closeHistoryFilter();assert(current->m_closed&&!modes.m_historyFilterPopup);
 }
 Layer offline;RankedRuntime::get().token.clear();auto before=Filter::creates;offline.onHistoryFilter(nullptr);assert(Filter::creates==before);
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
 assert.match(popup,/rankedPopupContent\(m_mainLayer,m_buttonMenu,layout\)/);
 assert.match(popup,/m_buttonMenu->addChild\(item\)/);
 assert.doesNotMatch(popup,/schedule\(|CCMenu::create/);
 assert.match(method(screen,'    void updateHistoryReconnect('),/if\(expired\)closeHistoryFilter\(\)/);
});
