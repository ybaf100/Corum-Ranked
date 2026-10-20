import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const read=p=>readFileSync(join(root,p),'utf8');
function run(cpp){const dir=mkdtempSync(join(tmpdir(),'corum-ipad-'));try{
 writeFileSync(join(dir,'test.cpp'),cpp);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
function cls(src,name){let a=src.indexOf('class '+name+' ');assert(a>=0,name);let e=src.indexOf('\n};',a)+3;return src.slice(a,e);}

test('podium tier/LP group has no fill and stays centered on its own pedestal throughout stamp scaling',()=>{
 const src=read('src/ui/SurvivalOutcomePanel.hpp');
 const setup=src.slice(src.indexOf('auto* stamp='),src.indexOf('\n',src.indexOf('auto* stamp=')));
 const fade=src.slice(src.indexOf('p.stamp->setVisible'),src.indexOf('\n',src.indexOf('p.stamp->setVisible')));
 const opacity=read('src/ui/ModeLobbyMotion.hpp');
 const prop=opacity.slice(opacity.indexOf('inline void propagateModeOpacity'),opacity.indexOf('\nclass ModeLobbyFade'));
 run(`
#include <cassert>
#include <cmath>
#include <vector>
using GLubyte=unsigned char;struct P{float x=0,y=0;};struct S{float width=0,height=0;};struct Color{GLubyte r,g,b,a;};
struct CCRGBAProtocol{virtual~CCRGBAProtocol()=default;virtual void updateDisplayedOpacity(GLubyte)=0;virtual bool isCascadeOpacityEnabled()=0;virtual GLubyte getDisplayedOpacity()=0;};
struct CCNode{virtual~CCNode()=default;P pos,anchor;S size;bool ignore=false;float scale=1;std::vector<CCNode*>children;
 static CCNode*create(){return new CCNode;}void setContentSize(S s){size=s;}void setAnchorPoint(P p){anchor=p;}void ignoreAnchorPointForPosition(bool b){ignore=b;}void setPosition(P p){pos=p;}void setScale(float s){scale=s;}void setVisible(bool){}void setCascadeOpacityEnabled(bool){}void addChild(CCNode*n,int){children.push_back(n);}auto*getChildren(){return &children;}
 P world(P p)const{return {pos.x+(ignore?anchor.x*size.width:0)+(p.x-anchor.x*size.width)*scale,pos.y+(ignore?anchor.y*size.height:0)+(p.y-anchor.y*size.height)*scale};}
};
struct CCLayerColor:CCNode,CCRGBAProtocol{GLubyte opacity=0,displayed=0;bool cascade=false;CCLayerColor(){ignore=true;anchor={.5,.5};}static auto*create(Color c,float w,float h){auto*n=new CCLayerColor;n->opacity=c.a;n->size={w,h};return n;}void setOpacity(GLubyte p){opacity=displayed=p;}void updateDisplayedOpacity(GLubyte p)override{displayed=opacity*p/255;}void setCascadeOpacityEnabled(bool b){cascade=b;}bool isCascadeOpacityEnabled()override{return cascade;}GLubyte getDisplayedOpacity()override{return displayed;}};
namespace cocos2d{using ::CCNode;using ::CCLayerColor;using ::CCRGBAProtocol;}
namespace geode::cocos{template<class T>struct CCArrayExt{std::vector<T>*p;CCArrayExt(std::vector<T>*p):p(p){}auto begin(){return p->begin();}auto end(){return p->end();}};}
${prop}
void addChild(CCNode*,int){}
int main(){for(float width:{96.f,138.f})for(float h:{50.f,70.f,94.f}){float x=240,base=80;
 ${setup}
 struct Frame{float stamp=1,stampScale=1,stampOpacity=1;}frame;
 using Stamp=decltype(stamp);struct Group{Stamp stamp;}p{stamp};
 for(float s:{1.18f,1.09f,1.f}){frame.stampScale=s;frame.stampOpacity=.5f;
 ${fade}
 auto center=stamp->world({width/2,h/2});assert(std::abs(center.x-x)<.001&&std::abs(center.y-(base+h/2))<.001);
 if(auto*fill=dynamic_cast<CCLayerColor*>(stamp))assert(fill->opacity==0);
 }
 delete stamp;
}}
`);
});

test('leaderboard scroll owns the original finger gesture even when it starts on a card; tap opens exactly once',()=>{
 const src=read('src/ui/RankedLeaderboardPopup.cpp');const fixed=src.includes('class LeaderboardScrollLayer ');
 const implementation=cls(src,fixed?'LeaderboardScrollLayer':'LeaderboardRowMenu');
 run(`
#include <cassert>
#include <cmath>
#include <functional>
#include <utility>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};struct CCRect{float x,y,width,height;};
struct CCTouch{CCPoint p;int id=1;CCPoint getLocation(){return p;}int getID(){return id;}};struct CCEvent{};
struct Node{float y=0;float getPositionY(){return y;}};
struct ScrollLayer{Node content;Node*m_contentLayer=&content;CCSize size;int began=0,moved=0,ended=0;CCPoint start;bool active=false;virtual~ScrollLayer()=default;
 ScrollLayer(CCRect r,bool,bool):size{r.width,r.height}{}virtual bool ccTouchBegan(CCTouch*t,CCEvent*){++began;start=t->p;active=true;return true;}
 virtual void ccTouchMoved(CCTouch*t,CCEvent*){if(active){++moved;content.y=t->p.y-start.y;}}
 virtual void ccTouchEnded(CCTouch*,CCEvent*){if(active)++ended;active=false;}virtual void ccTouchCancelled(CCTouch*,CCEvent*){active=false;}
 void autorelease(){}CCSize getContentSize(){return size;}CCPoint convertToNodeSpace(CCPoint p){return p;}
};
struct CCMenu{int clicks=0;virtual~CCMenu()=default;bool init(){return true;}void autorelease(){}virtual bool ccTouchBegan(CCTouch*,CCEvent*){return true;}virtual void ccTouchMoved(CCTouch*,CCEvent*){}virtual void ccTouchEnded(CCTouch*,CCEvent*){++clicks;}virtual void ccTouchCancelled(CCTouch*,CCEvent*){}};
${implementation}
int main(){CCTouch t{{30,30}};int opens=0;
${fixed?`auto* scroll=LeaderboardScrollLayer::create({300,180});scroll->setTapHandler([&](CCPoint,CCPoint){++opens;});
 auto begin=[&]{assert(scroll->ccTouchBegan(&t,nullptr));};auto move=[&]{scroll->ccTouchMoved(&t,nullptr);};auto end=[&]{scroll->ccTouchEnded(&t,nullptr);};`
:`ScrollLayer native({0,0,300,180},true,true);LeaderboardRowMenu card;auto*scroll=&native;
 // Faithful targeted dispatcher: accepted swallowing CCMenu prevents the ScrollLayer began.
 auto begin=[&]{if(!card.ccTouchBegan(&t,nullptr))scroll->ccTouchBegan(&t,nullptr);};auto move=[&]{card.ccTouchMoved(&t,nullptr);if(scroll->active)scroll->ccTouchMoved(&t,nullptr);};auto end=[&]{card.ccTouchEnded(&t,nullptr);if(scroll->active)scroll->ccTouchEnded(&t,nullptr);opens=card.clicks;};`}
 begin();t.p={30,80};move();end();assert(scroll->began==1&&scroll->moved==1&&scroll->m_contentLayer->y==50&&opens==0);
 t.p={30,30};begin();end();end();assert(opens==1);
 begin();t.p={30,65};move();t.p={30,30};move();end();assert(opens==1);
 ${fixed?`begin();scroll->cancelTap();end();assert(opens==1);begin();scroll->ccTouchCancelled(&t,nullptr);end();assert(opens==1);
 begin();scroll->m_contentLayer->y+=20;end();assert(opens==1);
 begin();CCTouch other{{32,32},2};scroll->ccTouchEnded(&other,nullptr);end();assert(opens==1);
 t.p={30,-1};assert(!scroll->ccTouchBegan(&t,nullptr));t.p={301,30};assert(!scroll->ccTouchBegan(&t,nullptr));
 t.p={30,30};begin();t.p={30,181};end();assert(opens==1);delete scroll;`:''}
}
`);
});

test('Settings and Credits remain within the viewport after native popup appearance resets the outer scale',()=>{
 for(const [file,marker] of [['src/ui/RankedSettingsPopup.hpp','        auto add='],['src/ui/RankedCreditsPopup.cpp','        auto add=']]){
  const src=read(file),start=src.indexOf('bool init() override{')>=0?src.indexOf('bool init() override{'):src.indexOf('bool init() override {');
  const open=src.indexOf('{',start);const prefix=src.slice(open+1,src.indexOf(marker,open));
  const helper=src.includes('RankedPopupLayout.hpp')?read('src/ui/RankedPopupLayout.hpp').split('\n').filter(l=>!l.startsWith('#include')&&!l.startsWith('#pragma')).join('\n'):'';
  run(`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};
struct CCNode{CCSize size;CCPoint pos,anchor;float scale=1;CCNode*parent=nullptr;std::vector<CCNode*>children;virtual~CCNode()=default;
 static CCNode*create(){return new CCNode;}void setContentSize(CCSize s){size=s;}CCSize getContentSize(){return size;}void setAnchorPoint(CCPoint p){anchor=p;}void setPosition(CCPoint p){pos=p;}void setScale(float s){scale=s;}void setVisible(bool){}void ignoreAnchorPointForPosition(bool){}void setLayout(std::nullptr_t){}void setZOrder(int){}void addChild(CCNode*n,int=0){n->parent=this;children.push_back(n);}
 CCPoint world(CCPoint p){p={pos.x+(p.x-anchor.x*size.width)*scale,pos.y+(p.y-anchor.y*size.height)*scale};return parent?parent->world(p):p;}
};struct CCMenu:CCNode{};struct CCLayer:CCNode{};
struct CCDirector{CCSize size;static auto*sharedDirector(){static CCDirector d;return &d;}CCSize getWinSize(){return size;}};
std::string operator""_spr(char const*s,std::size_t n){return {s,n};}
constexpr int Cyan=1,Surface=1;
CCNode*surface(CCSize s,CCPoint p,int,int,int,bool){auto*n=CCNode::create();n->size=s;n->anchor={.5,.5};n->pos=p;return n;}
namespace geode::prelude{}namespace cocos2d{using ::CCNode;using ::CCMenu;using ::CCLayer;using ::CCSize;}
${helper}
struct Popup{bool m_noElasticity=false;CCLayer layer;CCMenu menu;CCNode bg,close;CCLayer*m_mainLayer=&layer;CCMenu*m_buttonMenu=&menu;CCNode*m_bgSprite=&bg,*m_closeBtn=&close;
 bool init(float w,float h){layer.size={w,h};layer.anchor={.5,.5};auto win=CCDirector::sharedDirector()->size;layer.pos={win.width/2,win.height/2};layer.addChild(&menu);return true;}void setID(std::string){}
 void nativeShow(){layer.scale=1;}
};
struct Subject:Popup{bool setup(){using namespace corum::ranked::ui;
${prefix}
return true;}};
int main(){for(auto win:{CCSize{432,324},CCSize{480,335},CCSize{568,320},CCSize{640,480},CCSize{400,280}}){CCDirector::sharedDirector()->size=win;Subject s;assert(s.setup());s.nativeShow();
 // Check every visible panel corner after native appearance, not just the calculated scale.
 auto* panel=s.m_mainLayer->children.back();if(panel==s.m_buttonMenu)panel=s.m_mainLayer->children.front();
 CCNode* surface=panel;if(panel->children.size() && panel->children.back()!=s.m_buttonMenu)surface=panel->children.back();
 auto lo=surface->world({0,0}),hi=surface->world({surface->size.width,surface->size.height});
 assert(lo.x>=11.9&&lo.y>=11.9&&hi.x<=win.width-11.9&&hi.y<=win.height-11.9);
 }}
` .replace('using namespace corum::ranked::ui;',src.includes('RankedPopupLayout.hpp')?'using namespace corum::ranked::ui;':''));
 }
});
