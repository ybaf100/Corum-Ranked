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

test('actual toolbar keeps dark gold treatment, scaled button geometry and positions through channel entry/Back; footer follows language and mode',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-lobby-toolbar-'));
 try {
  let boundary=read('tests/helpers/ResultPresentationNativeDouble.hpp').replace(/^#pragma once$/gm,'')
   .replace('CCNode* getParent()',`CCNode*getChildByID(std::string const&s){for(auto*n:children)if(n->id==s)return n;return nullptr;}
    CCSize getScaledContentSize()const{return {size.width*scaleX,size.height*scaleY};} CCNode*getParent()`)
   .replace('bool enabled=true; void setEnabled',`void updateSprite(){auto* sprite=getNormalImage();auto size=sprite->getScaledContentSize();sprite->setPosition({size.width/2,size.height/2});sprite->setAnchorPoint({.5f,.5f});setContentSize(size);}
    void setNormalImage(CCNode*face){removeAllChildrenWithCleanup(true);addChild(face);} bool enabled=true; void setEnabled`)
   .replace('n->addChild(face); return n;', 'n->addChild(face);n->updateSprite();return n;');
  const popup=read('src/RankedPopup.cpp');
  writeFileSync(join(dir,'test.cpp'),`#include <array>
#include <string_view>
${boundary}
#include "domain/RankedMode.hpp"
namespace geode {struct Mod {
 std::string saved="en";static Mod*get(){static Mod m;return &m;}
 template<class T>T getSavedValue(std::string const&,T const&)const{return saved;}
 void setSavedValue(std::string const&,std::string const& s){saved=s;}
};}
${strip(read('src/ui/RankedTranslations.hpp'))}
std::string tr(std::string_view k,std::string_view e){return corum::ranked::i18n::text(k,e);}
constexpr ccColor3B kGold{255,207,74},kRed{255,91,111},kCyan{64,224,255};
namespace corum::ranked {struct SurvivalRuntime {struct View{std::string error;}v;static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}View const&view()const{return v;}};}
namespace corum::ranked::ui {
 constexpr ccColor3B SurfaceRaised{16,27,49},Text{242,247,255};
 CCNode*surface(CCSize s,CCPoint p,ccColor3B accent,ccColor3B fill,int,bool){auto*n=CCNode::create();n->size=s;n->position=p;n->color=fill;auto*f=CCNode::create();f->id="frame";f->color=accent;n->addChild(f);return n;}
 CCLabelBMFont*menuLabel(std::string const&s,float scale,CCPoint p,ccColor3B c,bool){auto*n=new CCLabelBMFont;n->setString(s.c_str());n->setScale(scale);n->position=p;n->color=c;return n;}
 void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}
}
${method(popup,'CCNode* makeActionButtonVisual(')}
enum class RuntimeStage{Ready,Queued};struct RuntimeView{RuntimeStage stage=RuntimeStage::Ready;bool soloEnabled=true,survivalEnabled=true;std::string error,status="Ranked session ready. Join the queue when prepared.";struct{bool active=false;}placement;struct{bool onlineAvailable=false;}survival;};
template<class T>T typeinfo_cast(CCNode*n){return dynamic_cast<T>(n);}
struct Owner{
 corum::ranked::ModeLobbyTransition m_modeTransition;CCMenu*m_lobbyToolbar=CCMenu::create();CCNode*m_root=CCNode::create();CCLabelBMFont*m_mainStatusLabel=nullptr;std::string m_localMessage;
 ${method(popup,'    void updateModeLobbyHeader(')}
 ${method(popup,'    std::string lobbyMessage(')}
};
int main(){using namespace corum::ranked;using namespace corum::ranked::ui;
 for(auto language:{i18n::Language::English,i18n::Language::Korean}){
  i18n::setLanguage(language);Owner o;
  auto*face=makeActionButtonVisual(tr("history","HISTORY"));face->setScale(.66f);
  auto*history=CCMenuItemSpriteExtra::create(face,nullptr,0);history->id="ranked-lobby-history";o.m_lobbyToolbar->addChild(history);
  auto*trophyFace=makeActionButtonVisual(tr("achievements","ACHIEVEMENTS"));trophyFace->setScale(.66f);
  auto*trophy=CCMenuItemSpriteExtra::create(trophyFace,nullptr,0);trophy->id="ranked-achievement-trophy";o.m_lobbyToolbar->addChild(trophy);
  auto*label=new CCLabelBMFont;label->id="ranked-screen-eyebrow";o.m_root->addChild(label);
  for(auto size:{CCSize{480,320},CCSize{568,320},CCSize{800,450}}){
   CCDirector::sharedDirector()->size=size;
   for(int repeat=0;repeat<3;repeat++)for(auto mode:{RankedMode::Survival,RankedMode::SoloRanked}){
    assert(o.m_modeTransition.select(mode));o.m_modeTransition.finish();o.updateModeLobbyHeader();
    auto*normal=history->getNormalImage();auto*panel=normal->children[0];
    assert(panel->color==SurfaceRaised&&panel->children[0]->color==(mode==RankedMode::SoloRanked?kGold:kCyan));
    assert(normal->children[1]->color==(mode==RankedMode::SoloRanked?kGold:Text));
    assert(std::abs(history->size.width-normal->size.width*.66f)<.001f);
    assert(normal->anchor.x==.5f&&std::abs(normal->position.x-history->size.width/2)<.001f);
    assert(std::abs(history->position.x+history->size.width/2-(size.width-20))<.001f);
    assert(history->position.y==size.height-28&&trophy->position.y==history->position.y);
    assert(std::abs(history->position.x-history->size.width/2-(trophy->position.x+trophy->size.width/2)-8)<.001f);
    auto position=history->position;assert(o.m_modeTransition.back());o.m_modeTransition.finish();o.updateModeLobbyHeader();
    assert(history->position.x==position.x&&history->position.y==position.y);
   }
  }
  RuntimeView view;auto text=o.lobbyMessage(view);
  assert(text==tr("click-mode","CLICK THE MODE YOU WANT TO ENTER"));
  view.survivalEnabled=false;assert(o.lobbyMessage(view).find(tr("mode-disabled","MODE DISABLED"))!=std::string::npos);
  assert(o.m_modeTransition.select(RankedMode::Survival));o.m_modeTransition.finish();
  assert(o.lobbyMessage(view)==tr("mode-off-browse","MODE DISABLED. HISTORY, LEADERBOARD AND ACHIEVEMENTS ARE AVAILABLE."));
  view.survivalEnabled=true;view.survival.onlineAvailable=true;assert(o.lobbyMessage(view)==tr("survival-online-ready","2-16 PLAYERS · UP TO 180s"));
  view.survival.onlineAvailable=false;assert(o.lobbyMessage(view)==tr("mode-off-browse","MODE DISABLED. HISTORY, LEADERBOARD AND ACHIEVEMENTS ARE AVAILABLE."));
  view.stage=RuntimeStage::Queued;view.status="Searching for an opponent...";
  assert(o.lobbyMessage(view)==(language==i18n::Language::Korean?"상대를 찾고 있습니다...":"Searching for an opponent..."));
  assert(i18n::text("Ranked session ready. Join the queue when prepared.",view.status)==(language==i18n::Language::Korean?"준비되면 매칭을 시작하세요.":view.status));
 }
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}
 finally{rmSync(dir,{recursive:true,force:true});}
});
