// Compile the actual modal and channel callbacks with native UI boundaries.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const strip=s=>s.replace(/^#include.*$|^#pragma once$/gm,'');
function method(source,signature){const start=source.indexOf(signature);assert(start>=0,signature);let end=source.indexOf('{',start)+1,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(start,end);}

test('disabled channel modal uses our bilingual font, gates entry, and retires repeated/stale callbacks safely',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-disabled-mode-'));
 try{
  const boundary=read('tests/helpers/ResultPresentationNativeDouble.hpp').replace(/^#pragma once$/gm,'')
   .replace('struct CCObject { virtual', 'struct CCObject { int tag=0;int getTag()const{return tag;} virtual')
   .replace('CCNode* getParent()', 'auto*getChildren(){return &children;} CCNode* getParent()');
  const popup=read('src/RankedPopup.cpp');
  writeFileSync(join(dir,'test.cpp'),`#include <array>
#include <string_view>
#include <tuple>
${boundary}
#include "domain/RankedMode.hpp"
namespace geode{struct Mod{std::string saved="en";static Mod*get(){static Mod m;return &m;}template<class T>T getSavedValue(std::string const&,T const&)const{return saved;}void setSavedValue(std::string const&,std::string const&s){saved=s;}};}
${strip(read('src/domain/TranslationCatalog.generated.hpp'))}\n${strip(read('src/domain/TranslationCatalog.hpp'))}\n${strip(read('src/ui/RankedTranslations.hpp'))}
std::string tr(std::string_view k,std::string_view e){return corum::ranked::i18n::text(k,e);}
using CCMenuItem=CCMenuItemSpriteExtra;
template<class T>T typeinfo_cast(CCObject*o){return dynamic_cast<T>(o);}
template<class T>auto const&CCArrayExt(std::vector<CCNode*>*v){return *v;}
struct CCTouchDispatcher{int releases=0;static auto*get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){++releases;}};
struct Popup:CCNode{CCNode*m_bgSprite=CCNode::create(),*m_closeBtn=CCNode::create(),*m_mainLayer=CCNode::create();
 inline static CCNode scene;inline static int shows=0;bool init(float,float){return true;}void show(){++shows;scene.addChild(this);}virtual void onClose(CCObject*){removeFromParentAndCleanup(true);}};
namespace corum::ranked::ui{
 enum class FontWeight{SemiBold,Black};constexpr ccColor3B Gold{255,207,74},Surface{8,14,26},SurfaceRaised{16,27,49},Text{242,247,255};
 inline std::vector<std::pair<std::string,FontWeight>> labels;
 CCNode*surface(CCSize s,CCPoint p,ccColor3B,ccColor3B fill,int,bool){auto*n=CCNode::create();n->size=s;n->position=p;n->color=fill;return n;}
 CCLabelBMFont*menuLabel(std::string const&s,float,CCPoint,ccColor3B,FontWeight w){auto*n=new CCLabelBMFont;n->setString(s.c_str());labels.emplace_back(s,w);return n;}
 void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
 void cancelMenuTouch(CCMenu*m){++m->cancellations;}
}
${strip(read('src/ui/RankedModeDisabledPopup.hpp')).replace('class RankedModeDisabledPopup final : public Popup {','class RankedModeDisabledPopup final : public Popup { public:')}
enum class RuntimeStage{Ready,Matched,Loading};struct RuntimeView{RuntimeStage stage=RuntimeStage::Ready;bool survivalEnabled=false,soloEnabled=false;struct{bool loaded=false;}survival;};
struct RankedRuntime{RuntimeView v;int fetches=0;static RankedRuntime&get(){static RankedRuntime r;return r;}RuntimeView const&view()const{return v;}void fetchSurvivalProfile(){++fetches;}};
struct Owner:CCNode{
 struct Hover{void press(CCMenuItemSpriteExtra*){}};Hover*m_lobbyHover=nullptr;
 enum class Page{Live,History};Page m_page=Page::Live;bool m_uiFadingOut=false,m_modeNoticeOpen=false;unsigned m_modeNoticeGeneration=0;
 corum::ranked::ModeLobbyTransition m_modeTransition;std::optional<WeakRef<corum::ranked::ui::RankedModeDisabledPopup>>m_modeNotice;
 CCLayerColor a,b,c;CCLayerColor*m_lobbySurvival=&a,*m_lobbySolo=&b,*m_lobbyPanel=&c;
 CCMenu menu,toolbar,settings;CCMenu*m_menu=&menu,*m_lobbyToolbar=&toolbar,*m_settingsMenu=&settings;std::string m_localMessage;int animations=0,renders=0;
 void renderModePanel(RuntimeView const&,corum::ranked::RankedMode){++renders;}void animateModeLobby(corum::ranked::RankedMode,bool){++animations;}
 ${method(popup,'    void setLobbyNodeInput(')}
 ${method(popup,'    void syncLobbyInput(')}
 ${method(popup,'    void enterMode(')}
 ${method(popup,'    void onSelectMode(')}
 ${method(popup,'    void closeModeNotice(')}
};
int main(){using namespace corum::ranked;using namespace corum::ranked::ui;
 for(auto language:{i18n::Language::English,i18n::Language::Korean})for(int tag:{0,1}){
  i18n::setLanguage(language);Owner o;CCNode ownerScene;ownerScene.addChild(&o);CCObject item;item.tag=tag;
  RankedRuntime::get().v={};auto before=Popup::shows;
  o.onSelectMode(&item);auto notice=o.m_modeNotice->lock();assert(notice&&notice->getParent());
  assert(o.m_modeNoticeOpen&&!o.m_modeTransition.selected()&&!o.menu.touch&&!o.toolbar.touch&&!o.settings.touch);
  for(int i=0;i<20;++i){o.onSelectMode(&item);}assert(Popup::shows==before+1&&o.animations==0);
  auto detail=tr("mode-disabled-notice","This mode is currently disabled.\\nContinue to the view-only lobby.");
  assert(std::find(labels.begin(),labels.end(),std::pair{detail,FontWeight::SemiBold})!=labels.end());
  if(language==i18n::Language::Korean)assert(detail=="이 모드는 운영 중지 상태입니다.\\n확인을 누르면 조회 화면으로 이동합니다.");
  notice->onBrowse(nullptr);notice->onBrowse(nullptr);assert(o.animations==0&&mainQueue.size()==1);
  for(auto*n:notice->m_actions->children)assert(!dynamic_cast<CCMenuItem*>(n)->enabled);
  drainMainQueue();assert(!notice->getParent()&&!o.m_modeNoticeOpen&&o.animations==1);
  assert(o.m_modeTransition.selected()==(tag==0?RankedMode::Survival:RankedMode::SoloRanked));
  auto released=CCTouchDispatcher::get()->releases;notice->onExit();assert(CCTouchDispatcher::get()->releases==released);
 }
 // ON enters normally, without a notice. Recovery while a notice is pending
 // prevents its confirmation from entering an obsolete chooser.
 Owner on;CCNode scene;scene.addChild(&on);CCObject item;item.tag=1;RankedRuntime::get().v.soloEnabled=true;
 auto before=Popup::shows;on.onSelectMode(&item);assert(on.animations==1&&Popup::shows==before);
 RankedRuntime::get().v={};Owner recovered;scene.addChild(&recovered);recovered.onSelectMode(&item);
 recovered.m_modeNotice->lock()->onBrowse(nullptr);RankedRuntime::get().v.stage=RuntimeStage::Matched;
 drainMainQueue();assert(recovered.animations==0&&!recovered.m_modeTransition.selected());
 // Back during dispatch cancels the queued entry; an old callback cannot
 // reset a newly opened notice. Leaving the owner also prevents entry.
 RankedRuntime::get().v={};Owner cancelled;scene.addChild(&cancelled);cancelled.onSelectMode(&item);
 auto old=cancelled.m_modeNotice->lock();old->onBrowse(nullptr);cancelled.closeModeNotice();cancelled.syncLobbyInput();
 cancelled.onSelectMode(&item);auto current=cancelled.m_modeNotice->lock();assert(current.data()!=old.data());
 drainMainQueue();assert(cancelled.m_modeNoticeOpen&&cancelled.animations==0&&current->getParent());
 current->onClose(nullptr);current->onClose(nullptr);drainMainQueue();assert(!cancelled.m_modeNoticeOpen&&!cancelled.m_modeTransition.selected());
 Owner exited;scene.addChild(&exited);exited.onSelectMode(&item);exited.m_modeNotice->lock()->onBrowse(nullptr);
 exited.removeFromParentAndCleanup(true);drainMainQueue();assert(exited.animations==0);
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}
 finally{rmSync(dir,{recursive:true,force:true});}
});

test('new Korean lobby/status/popup strings fit the packaged SemiBold charset',()=>{
 const font=JSON.parse(read('mod.json')).resources.fonts.CorumSansSemiBold;
 const glyphs=new Set();for(const range of font.charset.split(',')){const[a,b=a]=range.split('-').map(Number);for(let n=a;n<=b;n++)glyphs.add(n);}
 const translations=JSON.parse(read('../ranked/translations/ui.json'));
 for(const {ko:literal} of Object.values(translations))
  for(const c of literal.replaceAll('\\n',''))if(!['\n','\r','\t'].includes(c))assert(glyphs.has(c.codePointAt(0)),`Unpackaged font glyph ${c} in ${literal}`);
 assert.match(read('src/ui/RankedModeDisabledPopup.hpp'),/menuLabel\(i18n::text\("mode-disabled-notice"/);
 assert.match(read('src/RankedPopup.cpp'),/noticeView\.stage != RuntimeStage::Ready.*noticeView\.accountActiveElsewhere/);
});
