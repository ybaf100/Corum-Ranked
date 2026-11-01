import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname,read=p=>readFileSync(join(root,p),'utf8');
function run(source){
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;
 assert(json&&result,'Pinned matjson/Result dependencies are required');
 const dir=mkdtempSync(join(tmpdir(),'hud-layout-'));
 try{
  writeFileSync(join(dir,'test.cpp'),source);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-isystem',join(json,'include'),'-isystem',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.stdout?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
}

test('HUD layout resolves anchors per window size, clamps, ignores unknown keys and never hides the timer',()=>{
 run(`
#include "${root}/src/domain/HudLayout.hpp"
#include <cassert>
#include <cmath>
using namespace corum::ranked;
static bool near(float a,float b){return std::abs(a-b)<.01f;}
int main(){
 // Default placement is an identity transform on every element.
 for(auto mode:{HudMode::Solo,HudMode::Survival})for(auto const& spec:hudElements(mode)){
  assert(hudDefaultPivot(mode,spec.id,569,320));
  auto t=resolveHudTransform(mode,spec.id,HudLayoutConfig{},569,320);assert(t.identity());
 }
 // Exactly one element per mode is unhideable: the timer.
 for(auto mode:{HudMode::Solo,HudMode::Survival}){int locked=0;for(auto const& s:hudElements(mode))if(!s.hideable){++locked;assert(s.id=="timer");}assert(locked==1);}
 // A top-right element keeps its distance to the right edge on a wider screen.
 auto at=hudPlacementAt({500,280},{480,270},569,320,1.0f,true);
 assert(at.moved&&at.anchor==HudAnchor::TopRight&&near(at.dx,-69)&&near(at.dy,-40));
 auto wide=resolveHudTransform(HudMode::Solo,"opponent",&at,700,320);assert(near(wide.position.x,631)&&near(wide.position.y,280));
 // Center zone uses the screen center; bottom-left uses the origin.
 auto center=hudPlacementAt({300,170},{290,160},569,320,1.0f,true);assert(center.anchor==HudAnchor::Center);
 auto ipad=resolveHudTransform(HudMode::Solo,"state",&center,512,384);assert(near(ipad.position.x,256+300-284.5f)&&near(ipad.position.y,192+10));
 auto low=hudPlacementAt({20,20},{40,30},569,320,1.0f,true);assert(low.anchor==HudAnchor::BottomLeft&&near(low.dx,20)&&near(low.dy,20));
 // Out-of-range offsets are clamped inside the window; scale snaps to 10% steps.
 HudPlacement wild{.moved=true,.anchor=HudAnchor::TopLeft,.dx=9000,.dy=-9000,.scale=7.0f,.visible=true};
 auto clamped=resolveHudTransform(HudMode::Solo,"own",&wild,569,320);assert(near(clamped.position.x,569)&&near(clamped.position.y,0)&&near(clamped.scale,1.6f));
 assert(near(clampHudScale(.63f),.6f)&&near(clampHudScale(1.04f),1.0f)&&near(clampHudScale(NAN),1.0f)&&near(clampHudScale(.1f),.6f));
 assert(snapHud(13)==16&&snapHud(11)==8&&snapHud(-5)==-8);
 // The timer cannot be hidden, even by a hand-edited save.
 HudPlacement hidden{.visible=false};
 assert(resolveHudTransform(HudMode::Solo,"timer",&hidden,569,320).visible);
 assert(resolveHudTransform(HudMode::Survival,"timer",&hidden,569,320).visible);
 assert(!resolveHudTransform(HudMode::Solo,"feed",&hidden,569,320).visible);
 // Progress-bar markers are visibility only.
 HudPlacement moved{.moved=true,.anchor=HudAnchor::Center,.dx=40,.dy=40,.scale=1.5f,.visible=false};
 auto cut=resolveHudTransform(HudMode::Survival,"cutline",&moved,569,320);assert(cut.identity()==false&&!cut.visible&&cut.scale==1.0f&&cut.position==cut.pivot);
 // Round trip; unknown ids/anchors/versions fall back to the defaults.
 HudLayoutConfig config;config.solo["timer"]=hudMovedPlacement(HudAnchor::Top,0,-32,1.2f);config.solo["feed"]=hudHiddenPlacement();
 config.survival["standings"]=hudScaledPlacement(.8f);
 auto text=hudLayoutJson(config).dump();auto back=parseHudLayoutText(text);
 assert(back.solo.size()==2&&back.survival.size()==1);
 assert(back.solo.at("timer").moved&&back.solo.at("timer").anchor==HudAnchor::Top&&near(back.solo.at("timer").dy,-32)&&near(back.solo.at("timer").scale,1.2f));
 assert(!back.solo.at("feed").visible&&near(back.survival.at("standings").scale,.8f));
 auto messy=parseHudLayoutText(R"({"version":1,"solo":{"future":{"scale":1.2},"timer":{"visible":false},"own":{"anchor":"nowhere","dx":5,"dy":5},"round":"x","fps":{"dx":"1"}},"survival":[]})");
 assert(messy.solo.empty()&&messy.survival.empty());
 assert(parseHudLayoutText(R"({"version":2,"solo":{"feed":{"visible":false}}})").solo.empty());
 assert(parseHudLayoutText("not json").solo.empty()&&parseHudLayoutText("").solo.empty());
 // Presets reference only known elements and keep the timer visible.
 for(auto mode:{HudMode::Solo,HudMode::Survival})for(auto preset:HudPresets)for(auto const& [id,p]:hudPresetPlacements(mode,preset)){
  auto const* spec=hudElement(mode,id);assert(spec);assert(spec->hideable||p.visible);
  auto t=resolveHudTransform(mode,id,&p,569,320);assert(t.position.x>=0&&t.position.x<=569&&t.position.y>=0&&t.position.y<=320);
 }
 assert(hudPresetPlacements(HudMode::Solo,HudPreset::Default).empty());
}
`);
});

test('Solo and Survival HUDs build each customizable element inside a placement group',()=>{
 const play=read('src/PlayLayer.cpp'),survival=read('src/ui/SurvivalHud.cpp'),groups=read('src/ui/HudPlacementGroup.hpp');
 for(const id of ['fps','own','opponent','round','timer','state','qualifying','progress','feed'])
  assert.match(play,new RegExp(`hudGroup\\("${id}"`),`solo ${id}`);
 for(const id of ['aux','standings','round','timer','difference','rank','waiting'])
  assert.match(survival,new RegExp(`group\\("${id}"\\)`),`survival ${id}`);
 assert.match(survival,/HudMode::Survival,"cutline"/);
 assert.match(groups,/setAnchorPoint\(\{transform\.pivot\.x\/size\.width,transform\.pivot\.y\/size\.height\}\)/);
 const popup=read('src/RankedPopup.cpp');
 assert.match(popup,/ranked-hud-editor-button/);assert.match(popup,/survival-hud-editor-button/);
 assert.match(popup,/HudEditorPopup::create\(corum::ranked::HudMode::Solo\)/);
 assert.match(popup,/HudEditorPopup::create\(corum::ranked::HudMode::Survival\)/);
});

// The real editor source, compiled against a small transform-correct cocos
// stand-in: selection, 8 px snapped drags, window clamping, the locked timer,
// presets, cancel and save through the real saved-value path.
test('actual HUD editor drags, snaps, clamps, keeps the timer visible and saves only on SAVE',()=>{
 const strip=p=>read(p).replace(/^#pragma once\s*|^#include .*$/gm,'');
 const domain=['HudLayout.hpp','CompetitiveHudLayout.hpp','HudPresentation.hpp','SurvivalHudPresentation.hpp'].map(f=>`#include "${root}/src/domain/${f}"`).join('\n');
 run(`
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <vector>
${domain}
#include "${root}/src/domain/HudPresentation.cpp"
namespace fmt{using std::format;}
inline char const* operator""_spr(char const* s,std::size_t){return s;}
namespace cocos2d{
struct CCSize;
// Real cocos CCPoint/CCSize cross-assign, so a braced assignment is ambiguous.
struct CCPoint{float x=0,y=0;CCPoint()=default;CCPoint(float a,float b):x(a),y(b){}CCPoint(CCPoint const&)=default;CCPoint& operator=(CCPoint const&)=default;CCPoint& operator=(CCSize const&);
 CCPoint operator+(CCPoint o)const{return {x+o.x,y+o.y};}CCPoint operator-(CCPoint o)const{return {x-o.x,y-o.y};}};
struct CCSize{float width=0,height=0;CCSize()=default;CCSize(float w,float h):width(w),height(h){}CCSize(CCSize const&)=default;CCSize& operator=(CCSize const&)=default;CCSize& operator=(CCPoint const& p){width=p.x;height=p.y;return *this;}};
inline CCPoint& CCPoint::operator=(CCSize const& s){x=s.width;y=s.height;return *this;}
struct CCRect{CCPoint origin;CCSize size;CCRect()=default;CCRect(float x,float y,float w,float h):origin{x,y},size{w,h}{}
 float getMinX()const{return origin.x;}float getMaxX()const{return origin.x+size.width;}float getMidX()const{return origin.x+size.width/2;}
 float getMinY()const{return origin.y;}float getMaxY()const{return origin.y+size.height;}float getMidY()const{return origin.y+size.height/2;}
 bool containsPoint(CCPoint p)const{return p.x>=getMinX()&&p.x<=getMaxX()&&p.y>=getMinY()&&p.y<=getMaxY();}};
struct ccColor3B{unsigned char r=0,g=0,b=0;};struct ccColor4B{unsigned char r=0,g=0,b=0,a=0;};struct ccColor4F{float r=0,g=0,b=0,a=0;};
inline ccColor3B ccc3(unsigned char r,unsigned char g,unsigned char b){return {r,g,b};}
using GLubyte=unsigned char;
struct CCObject{int tag=0;virtual~CCObject()=default;int getTag()const{return tag;}void setTag(int t){tag=t;}void autorelease(){}};
using SEL_MenuHandler=void(CCObject::*)(CCObject*);
#define menu_selector(x) (SEL_MenuHandler)(&x)
struct CCEvent{};struct CCTouch{CCPoint at;int id=0;CCPoint getLocation(){return at;}int getID(){return id;}};
struct CCNode:CCObject{
 CCNode* parent=nullptr;std::vector<CCNode*> children;CCPoint position,anchor;CCSize content;float scale=1;bool visible=true,ignoreAnchor=false;int z=0;std::string id;
 static CCNode* create(){return new CCNode;}bool init(){return true;}
 void addChild(CCNode* c,int order=0){c->parent=this;c->z=order;children.push_back(c);}
 std::vector<CCNode*>& getChildrenExt(){return children;}
 void setPosition(CCPoint p){position=p;}CCPoint getPosition()const{return position;}float getPositionX()const{return position.x;}
 void setAnchorPoint(CCPoint p){anchor=p;}CCPoint getAnchorPoint()const{return anchor;}
 void setContentSize(CCSize s){content=s;}virtual CCSize getContentSize()const{return content;}
 void setScale(float s){scale=s;}float getScale()const{return scale;}
 void setVisible(bool v){visible=v;}bool isVisible()const{return visible;}
 void setID(std::string const& v){id=v;}void setZOrder(int v){z=v;}void ignoreAnchorPointForPosition(bool v){ignoreAnchor=v;}
 CCSize getScaledContentSize()const{auto c=getContentSize();return {c.width*scale,c.height*scale};}
 CCPoint toParent(CCPoint p)const{auto c=getContentSize();CCPoint a=ignoreAnchor?CCPoint{}:CCPoint{anchor.x*c.width,anchor.y*c.height};return {position.x+scale*(p.x-a.x),position.y+scale*(p.y-a.y)};}
 CCPoint fromParent(CCPoint p)const{auto c=getContentSize();CCPoint a=ignoreAnchor?CCPoint{}:CCPoint{anchor.x*c.width,anchor.y*c.height};return {(p.x-position.x)/scale+a.x,(p.y-position.y)/scale+a.y};}
 CCPoint convertToWorldSpace(CCPoint p)const{auto q=toParent(p);return parent?parent->convertToWorldSpace(q):q;}
 CCPoint convertToNodeSpace(CCPoint p)const{return fromParent(parent?parent->convertToNodeSpace(p):p);}
 CCRect boundingBox()const{auto c=getContentSize();auto a=toParent({0,0}),b=toParent({c.width,c.height});return {std::min(a.x,b.x),std::min(a.y,b.y),std::abs(b.x-a.x),std::abs(b.y-a.y)};}
 virtual void onExit(){}
};
struct CCNodeRGBA:CCNode{GLubyte opacity=255;static CCNodeRGBA* create(){return new CCNodeRGBA;}void setOpacity(GLubyte o){opacity=o;}GLubyte getOpacity()const{return opacity;}
 void setCascadeOpacityEnabled(bool){}void setCascadeColorEnabled(bool){}void setColor(ccColor3B){}};
struct CCLabelBMFont:CCNodeRGBA{std::string text;CCLabelBMFont(){anchor=CCPoint{.5f,.5f};}
 void setString(char const* s){text=s;}CCSize getContentSize()const override{return {static_cast<float>(text.size())*12,20};}};
struct CCSprite:CCNodeRGBA{CCSprite(){anchor=CCPoint{.5f,.5f};content=CCSize{20,20};}static CCSprite* create(char const*){return new CCSprite;}static CCSprite* createWithSpriteFrameName(char const*){return new CCSprite;}void setScaleX(float){}void setScaleY(float){}};
struct CCLayer:CCNode{CCLayer(){ignoreAnchor=true;}void setTouchEnabled(bool){}virtual void registerWithTouchDispatcher(){}
 virtual bool ccTouchBegan(CCTouch*,CCEvent*){return false;}virtual void ccTouchMoved(CCTouch*,CCEvent*){}virtual void ccTouchEnded(CCTouch*,CCEvent*){}virtual void ccTouchCancelled(CCTouch*,CCEvent*){}
 void setLayout(std::nullptr_t){}};
struct CCLayerColor:CCLayer{static CCLayerColor* create(ccColor4B){return new CCLayerColor;}};
struct CCDrawNode:CCNode{int polygons=0,segments=0;std::vector<ccColor4F> strokes;static CCDrawNode* create(){return new CCDrawNode;}void clear(){polygons=0;strokes.clear();}
 void drawSegment(CCPoint,CCPoint,float,ccColor4F){++segments;}void drawPolygon(CCPoint*,unsigned,ccColor4F,float,ccColor4F stroke){++polygons;strokes.push_back(stroke);}};
struct CCMenu:CCLayer{};
struct CCMenuItemSpriteExtra:CCNodeRGBA{CCObject* target=nullptr;SEL_MenuHandler selector=nullptr;bool enabled=true;
 static CCMenuItemSpriteExtra* create(CCNode* face,CCObject* t,SEL_MenuHandler s){auto*i=new CCMenuItemSpriteExtra;i->target=t;i->selector=s;i->anchor=CCPoint{.5f,.5f};i->content=face->getContentSize();i->addChild(face);return i;}
 void setEnabled(bool v){enabled=v;}bool isEnabled()const{return enabled;}void activate(){if(enabled)(target->*selector)(this);}};
struct TouchDispatcher{void addPrioTargetedDelegate(CCLayer*,int,bool){}};
struct CCDirector{TouchDispatcher dispatcher;static CCDirector* sharedDirector(){static CCDirector d;return &d;}CCSize getWinSize(){return {569,320};}TouchDispatcher* getTouchDispatcher(){return &dispatcher;}};
}
using namespace cocos2d;
namespace geode{
 template<class T>struct WeakRef{T* pointer=nullptr;WeakRef()=default;explicit WeakRef(T* p):pointer(p){}struct Locked{T* ptr;explicit operator bool()const{return ptr;}T* operator->(){return ptr;}};Locked lock()const{return {pointer};}};
 struct Mod{std::map<std::string,std::string> saved;int saves=0;static Mod* get(){static Mod m;return &m;}
  template<class T>T getSavedValue(std::string const& k,T fallback){auto f=saved.find(k);return f==saved.end()?fallback:f->second;}
  template<class T>void setSavedValue(std::string const& k,T v){saved[k]=v;}bool saveData(){++saves;return true;}};
 namespace log{template<class...A>void info(A&&...){}}
 struct Popup:CCLayerColor{CCLayer* m_mainLayer=nullptr;CCMenu* m_buttonMenu=nullptr;CCSprite* m_bgSprite=nullptr;CCMenuItemSpriteExtra* m_closeBtn=nullptr;bool m_noElasticity=false;bool closed=false;
  bool init(float w,float h){m_mainLayer=new CCLayer;m_mainLayer->ignoreAnchor=false;m_mainLayer->anchor=CCPoint{.5f,.5f};m_mainLayer->content=CCSize{w,h};m_mainLayer->position=CCPoint{w/2,h/2};addChild(m_mainLayer);
   m_buttonMenu=new CCMenu;m_mainLayer->addChild(m_buttonMenu);m_bgSprite=new CCSprite;m_closeBtn=new CCMenuItemSpriteExtra;return true;}
  virtual void onClose(CCObject*){closed=true;}virtual void keyBackClicked(){onClose(nullptr);}void show(){}};
 namespace prelude{using namespace geode;}
}
using geode::WeakRef;
namespace corum::ranked::i18n{inline std::string text(std::string_view,std::string_view english){return std::string(english);}}
namespace corum::ranked::ui{
 enum class FontWeight{Medium,SemiBold,ExtraBold,Black};
 constexpr ccColor3B Text{242,247,255},TextMuted{164,181,205},Cyan{64,224,255},Blue{77,135,255},Green{90,238,151},Red{255,91,111},Gold{255,207,74},Orange{255,145,68},Surface{10,19,36},SurfaceRaised{15,28,50};
 inline CCLabelBMFont* label(std::string const& t,float s,CCPoint p,ccColor3B,FontWeight,char const* =nullptr){auto*l=new CCLabelBMFont;l->text=t;l->scale=s;l->position=p;return l;}
 inline CCLabelBMFont* playerNameLabel(std::string const& t,float s,CCPoint p,ccColor3B c,FontWeight w){return label(t,s,p,c,w);}
 inline CCLabelBMFont* menuLabel(std::string const& t,float s,CCPoint p,ccColor3B c,FontWeight w){return label(t,s,p,c,w);}
 inline void fitLabelWidth(CCLabelBMFont*,float,float,float){}
 inline void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}
 inline void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
 inline CCNode* surface(CCSize s,CCPoint p,ccColor3B,ccColor3B,GLubyte,bool){auto*n=new CCNode;n->content=s;n->anchor=CCPoint{.5f,.5f};n->position=p;return n;}
 struct PlayerIcon:CCNode{static PlayerIcon* createLocal(){return new PlayerIcon;}};
}
namespace corum::ranked::ui{
${strip('src/ui/RankedHudLabel.hpp').replace(/namespace corum::ranked::ui \{|\} \/\/ namespace corum::ranked::ui/g,'')}
}
${strip('src/ui/HudPlacementGroup.hpp')}
${strip('src/ui/HudEditorPopup.hpp').replace(/private:/g,'public:')}
${strip('src/ui/HudEditorPopup.cpp')}
using namespace corum::ranked;
using namespace corum::ranked::ui;
static CCMenuItemSpriteExtra* button(HudEditorPopup* p,std::string const& text){
 for(auto* child:p->m_buttonMenu->children){auto*item=static_cast<CCMenuItemSpriteExtra*>(child);
  for(auto* face:item->children)for(auto* l:face->children)if(auto*label=dynamic_cast<CCLabelBMFont*>(l);label&&label->text==text)return item;}
 return nullptr;
}
int main(){
 auto& saved=geode::Mod::get()->saved;
 auto* editor=HudEditorPopup::create(HudMode::Solo);assert(editor);
 auto const timer=hudDefaultPivot(HudMode::Solo,"timer",569,320).value();
 // Tap the clock, drag it 37,-41: the lower-left corner snaps to the 8 px grid.
 assert(editor->beginTouch({timer.x,timer.y-6}));editor->moveTouch({timer.x+37,timer.y-47});editor->endTouch();
 auto* clock=editor->m_groups.at("timer");
 CCRect box;bool any=false;includeBox(clock,editor->m_canvas,clock,box,any);assert(any);
 assert(std::fmod(box.getMinX(),8.0f)==0&&std::fmod(box.getMinY(),8.0f)==0);
 assert(editor->m_working.at("timer").moved&&editor->m_working.at("timer").anchor==HudAnchor::Top);
 // Dragging far off screen keeps the whole element inside the window.
 assert(editor->beginTouch(box.origin+CCPoint{2,2}));editor->moveTouch({-900,-900});editor->endTouch();
 any=false;includeBox(clock,editor->m_canvas,clock,box,any);assert(box.getMinX()>=0&&box.getMinY()>=0);
 assert(editor->m_working.at("timer").anchor==HudAnchor::BottomLeft);
 // The timer cannot be hidden; the HIDE button is disabled and ignored.
 auto* hide=button(editor,"HIDE");assert(hide&&!hide->isEnabled());editor->onToggleVisible(nullptr);
 assert(editor->m_working.at("timer").visible);
 // Next element (round) can be hidden and resized within 60-160 %.
 editor->onNext(nullptr);assert(editor->m_selected=="round");assert(hide->isEnabled());hide->activate();
 assert(!editor->m_working.at("round").visible&&editor->m_groups.at("round")->isVisible()&&editor->m_groups.at("round")->getOpacity()<100);
 for(int i=0;i<12;i++)editor->onLarger(nullptr);assert(std::abs(editor->m_working.at("round").scale-1.6f)<.001f);
 for(int i=0;i<12;i++)editor->onSmaller(nullptr);assert(std::abs(editor->m_working.at("round").scale-.6f)<.001f);
 // Toolbar buttons fall through to the modal menu; the toolbar chrome drags.
 auto* save=button(editor,"SAVE");assert(save);
 assert(!editor->beginTouch(save->getPosition()));
 auto const toolbar=editor->m_toolbar->getPosition();
 assert(editor->beginTouch(toolbar+CCPoint{0,52}));editor->moveTouch(toolbar+CCPoint{-500,52});editor->endTouch();
 assert(editor->m_toolbar->getPosition().x==152&&save->getPosition().x==152+66);
 // Presets replace the working layout; cancel never writes.
 editor->onPreset(button(editor,"Minimal"));assert(editor->m_working==hudPresetPlacements(HudMode::Solo,HudPreset::Minimal));
 editor->onCancel(nullptr);assert(editor->closed&&saved.empty());
 // SAVE persists only this mode and keeps the other mode's layout.
 HudLayoutConfig before;before.survival["rank"]=hudScaledPlacement(.7f);saveHudLayout(before);
 auto* second=HudEditorPopup::create(HudMode::Solo);second->onPreset(button(second,"Broadcast"));save=button(second,"SAVE");save->activate();
 assert(second->closed);auto const stored=loadHudLayout();
 assert(stored.solo==hudPresetPlacements(HudMode::Solo,HudPreset::Focus)&&stored.survival==before.survival);
 // The survival editor previews every element and the markers are visibility-only.
 auto* survival=HudEditorPopup::create(HudMode::Survival);
 for(auto const& spec:hudElements(HudMode::Survival)){CCRect b;bool found=false;includeBox(survival->m_groups.at(std::string(spec.id)),survival->m_canvas,survival->m_groups.at(std::string(spec.id)),b,found);assert(found);}
 while(survival->m_selected!="cutline")survival->onNext(nullptr);
 assert(!button(survival,"-")->isEnabled()&&!button(survival,"+")->isEnabled());
 survival->keyBackClicked();assert(survival->closed);
}
`);
});
