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
 assert(hudElements(HudMode::Solo).size()==18);
 // Exactly one element per mode is unhideable: the timer.
 for(auto mode:{HudMode::Solo,HudMode::Survival}){int locked=0;for(auto const& s:hudElements(mode))if(!s.hideable){++locked;assert(s.id=="timer");}assert(locked==1);}
 // A top-right element keeps its distance to the right edge on a wider screen.
 auto at=hudPlacementAt({500,280},{480,270},569,320,1.0f,true);
 assert(at.moved&&at.anchor==HudAnchor::TopRight&&near(at.dx,-69)&&near(at.dy,-40));
 auto wide=resolveHudTransform(HudMode::Solo,"opponent-name",&at,700,320);assert(near(wide.position.x,631)&&near(wide.position.y,280));
 // Center zone uses the screen center; bottom-left uses the origin.
 auto center=hudPlacementAt({300,170},{290,160},569,320,1.0f,true);assert(center.anchor==HudAnchor::Center);
 auto ipad=resolveHudTransform(HudMode::Solo,"state",&center,512,384);assert(near(ipad.position.x,256+300-284.5f)&&near(ipad.position.y,192+10));
 auto low=hudPlacementAt({20,20},{40,30},569,320,1.0f,true);assert(low.anchor==HudAnchor::BottomLeft&&near(low.dx,20)&&near(low.dy,20));
 // Out-of-range offsets are clamped inside the window; scale is 50-200 % in 5 % steps.
 HudPlacement wild{.moved=true,.anchor=HudAnchor::TopLeft,.dx=9000,.dy=-9000,.scale=7.0f,.visible=true};
 auto clamped=resolveHudTransform(HudMode::Solo,"own-name",&wild,569,320);assert(near(clamped.position.x,569)&&near(clamped.position.y,0)&&near(clamped.scale,2.0f));
 assert(near(clampHudScale(.63f),.65f)&&near(clampHudScale(1.02f),1.0f)&&near(clampHudScale(NAN),1.0f)&&near(clampHudScale(.1f),.5f));
 // Opacity: 10-100 % in 10 % steps, independent of hiding.
 assert(near(clampHudOpacity(.34f),.3f)&&near(clampHudOpacity(0),.1f)&&near(clampHudOpacity(2),1)&&near(clampHudOpacity(NAN),1));
 assert(hudOpacityByte(1)==255&&hudOpacityByte(.5f)==128&&hudOpacityByte(0)==26);
 HudPlacement faded{.opacity=.4f};auto fade=resolveHudTransform(HudMode::Solo,"timer",&faded,569,320);
 assert(fade.visible&&near(fade.opacity,.4f)&&!fade.identity());
 assert(snapHud(13)==16&&snapHud(11)==8&&snapHud(-5)==-8&&snapHud(13.4f,1)==13&&snapHud(7,0)==7);
 assert(hudMutuallyExclusive(HudMode::Solo,"own-attempt","own-checks")&&!hudMutuallyExclusive(HudMode::Solo,"own-score","own-checks"));
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
 config.solo["own-score"]=HudPlacement{.opacity=.6f};config.survival["standings"]=hudScaledPlacement(.8f);
 auto text=hudLayoutJson(config).dump();auto back=parseHudLayoutText(text);
 assert(back.solo.size()==3&&back.survival.size()==1);
 assert(back.solo.at("timer").moved&&back.solo.at("timer").anchor==HudAnchor::Top&&near(back.solo.at("timer").dy,-32)&&near(back.solo.at("timer").scale,1.2f));
 assert(!back.solo.at("feed").visible&&near(back.survival.at("standings").scale,.8f)&&near(back.solo.at("own-score").opacity,.6f));
 auto messy=parseHudLayoutText(R"({"version":1,"solo":{"future":{"scale":1.2},"timer":{"visible":false},"own":{"anchor":"nowhere","dx":5,"dy":5},"round":"x","fps":{"dx":"1"}},"survival":[]})");
 assert(messy.solo.empty()&&messy.survival.empty());
 assert(parseHudLayoutText(R"({"version":2,"solo":{"feed":{"visible":false}}})").solo.empty());
 assert(parseHudLayoutText("not json").solo.empty()&&parseHudLayoutText("").solo.empty());
 // beta.150 block saves split into per-label placements with the same result.
 auto legacy=parseHudLayoutText(R"({"version":1,"solo":{"own":{"anchor":"bottom-left","dx":50,"dy":96,"scale":1.5},"opponent":{"visible":false},"qualifying":{"scale":1.2}}})");
 assert(legacy.solo.size()==12&&!legacy.solo.contains("own"));
 auto score=legacy.solo.at("own-score");assert(score.moved&&score.anchor==HudAnchor::BottomLeft&&near(score.dx,50+1.5f*82)&&near(score.dy,96-1.5f*16)&&near(score.scale,1.5f));
 for(float w:{569.f,700.f})for(auto id:{"opponent-name","opponent-caption","opponent-score","opponent-checks","opponent-attempt"}){
  auto t=resolveHudTransform(HudMode::Solo,id,legacy,w,320);assert(!t.visible&&t.position==t.pivot&&t.scale==1.0f);
 }
 auto value=resolveHudTransform(HudMode::Solo,"qualifying-value",legacy,569,320),caption=resolveHudTransform(HudMode::Solo,"qualifying-caption",legacy,569,320);
 assert(caption.position==caption.pivot&&near(value.position.x,14+1.2f*82)&&near(value.position.y,12-1.2f*3)&&near(value.scale,1.2f));
 // A new per-label entry wins over its legacy block.
 auto mixed=parseHudLayoutText(R"({"version":1,"solo":{"own":{"visible":false},"own-name":{"scale":1.1}}})");
 assert(mixed.solo.at("own-name").visible&&!mixed.solo.at("own-score").visible);
 // Presets reference only known elements and keep the timer visible.
 for(auto mode:{HudMode::Solo,HudMode::Survival})for(auto preset:HudPresets)for(auto const& [id,p]:hudPresetPlacements(mode,preset)){
  auto const* spec=hudElement(mode,id);assert(spec);assert(spec->hideable||p.visible);
  auto t=resolveHudTransform(mode,id,&p,569,320);assert(t.position.x>=0&&t.position.x<=569&&t.position.y>=0&&t.position.y<=320);
 }
 assert(hudPresetPlacements(HudMode::Solo,HudPreset::Default).empty());
 assert(hudPresetPlacements(HudMode::Solo,HudPreset::Focus).size()==12);
}
`);
});

test('Solo and Survival HUDs build each customizable element inside a placement group',()=>{
 const play=read('src/PlayLayer.cpp'),survival=read('src/ui/SurvivalHud.cpp'),groups=read('src/ui/HudPlacementGroup.hpp');
 for(const id of ['fps','own-name','own-caption','own-score','own-checks','own-attempt','opponent-name','opponent-caption',
   'opponent-score','opponent-checks','opponent-attempt','round','timer','state','qualifying-caption','qualifying-value','progress','feed'])
  assert.match(play,new RegExp(`hudGroup\\("${id}"`),`solo ${id}`);
 for(const id of ['aux','standings','round','timer','difference','rank','waiting'])
  assert.match(survival,new RegExp(`group\\("${id}"\\)`),`survival ${id}`);
 assert.match(survival,/HudMode::Survival,"cutline"/);
 assert.match(groups,/setAnchorPoint\(\{transform\.pivot\.x\/size\.width,transform\.pivot\.y\/size\.height\}\)/);
 // Opacity reaches labels added after the group (feed rows, standings, icons).
 assert.match(play,/refreshHudGroups\(\);\n        addChild\(m_fields->hudRoot, 1000\);/);
 assert.match(play,/refreshHudOpacity\(hudGroup\("feed", 5\)\)/);
 // Death Match attempt counters live in the editor's attempt groups, not on the PlayLayer root.
 const ux=read('src/GameplayUx.cpp');
 assert.match(ux,/"-solo-own-attempt"/);assert.match(ux,/"-solo-opponent-attempt"/);
 assert.match(ux,/createShadowLabel\(\s*ownAttemptGroup \? ownAttemptGroup : this,/);
 assert.match(ux,/createShadowLabel\(\s*opponentAttemptGroup \? opponentAttemptGroup : this,/);
 // The Solo editor preview shows the full three-row death log and the timer with its score difference.
 const editorSource=read('src/ui/HudEditorPopup.cpp');
 assert.match(editorSource,/std::array<std::pair<std::string, ccColor3B>, 3> rows/);
 assert.match(editorSource,/add\(group\("timer"\), "\+42"/);
 assert.match(survival,/refreshHudOpacity\(group\("standings"\)\)/);
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
// cocos2d-x 2.2 opacity cascade: displayed = real * parent displayed / 255.
struct CCRGBAProtocol{virtual~CCRGBAProtocol()=default;virtual void setOpacity(GLubyte)=0;virtual GLubyte getOpacity()=0;virtual GLubyte getDisplayedOpacity()=0;
 virtual void updateDisplayedOpacity(GLubyte)=0;virtual void setCascadeOpacityEnabled(bool)=0;virtual bool isCascadeOpacityEnabled()=0;};
struct CCNodeRGBA:CCNode,CCRGBAProtocol{GLubyte opacity=255,displayed=255;bool cascade=false;static CCNodeRGBA* create(){return new CCNodeRGBA;}
 void setOpacity(GLubyte o)override{opacity=displayed=o;if(cascade){GLubyte p=255;if(auto*r=dynamic_cast<CCRGBAProtocol*>(parent);r&&r->isCascadeOpacityEnabled())p=r->getDisplayedOpacity();updateDisplayedOpacity(p);}}
 GLubyte getOpacity()override{return opacity;}GLubyte getDisplayedOpacity()override{return displayed;}
 void updateDisplayedOpacity(GLubyte p)override{displayed=static_cast<GLubyte>(opacity*p/255);if(cascade)for(auto*c:children)if(auto*r=dynamic_cast<CCRGBAProtocol*>(c))r->updateDisplayedOpacity(displayed);}
 void setCascadeOpacityEnabled(bool v)override{cascade=v;}bool isCascadeOpacityEnabled()override{return cascade;}
 void setCascadeColorEnabled(bool){}void setColor(ccColor3B){}};
struct CCLabelBMFont:CCNodeRGBA{std::string text;CCLabelBMFont(){anchor=CCPoint{.5f,.5f};}
 void setString(char const* s){text=s;}CCSize getContentSize()const override{return {static_cast<float>(text.size())*12,20};}};
struct CCSprite:CCNodeRGBA{CCSprite(){anchor=CCPoint{.5f,.5f};content=CCSize{20,20};}static CCSprite* create(char const*){return new CCSprite;}static CCSprite* createWithSpriteFrameName(char const*){return new CCSprite;}void setScaleX(float){}void setScaleY(float){}float rotation=0;void setRotation(float r){rotation=r;}};
struct CCLayer:CCNode{CCLayer(){ignoreAnchor=true;}void setTouchEnabled(bool){}virtual void registerWithTouchDispatcher(){}
 virtual bool ccTouchBegan(CCTouch*,CCEvent*){return false;}virtual void ccTouchMoved(CCTouch*,CCEvent*){}virtual void ccTouchEnded(CCTouch*,CCEvent*){}virtual void ccTouchCancelled(CCTouch*,CCEvent*){}
 void setLayout(std::nullptr_t){}};
struct CCLayerColor:CCLayer{static CCLayerColor* create(ccColor4B){return new CCLayerColor;}};
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
struct ccBlendFunc{unsigned src,dst;};
struct CCDrawNode:CCNode{int polygons=0,segments=0;std::vector<ccColor4F> strokes,segmentColors;ccBlendFunc blend{1,GL_ONE_MINUS_SRC_ALPHA};static CCDrawNode* create(){return new CCDrawNode;}void clear(){polygons=0;segments=0;strokes.clear();segmentColors.clear();}
 void setBlendFunc(ccBlendFunc b){blend=b;}
 void drawSegment(CCPoint,CCPoint,float,ccColor4F color){++segments;segmentColors.push_back(color);}void drawPolygon(CCPoint*,unsigned,ccColor4F,float,ccColor4F stroke){++polygons;strokes.push_back(stroke);}};
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
namespace corum::ranked::i18n{inline std::string text(std::string_view,std::string_view english){return std::string(english);}inline char const* literal(std::string_view){return "ATTEMPTS LEFT : ";}}
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
 assert(editor->m_groups.size()==18&&editor->m_listRows.size()==18);
 // Straight-alpha draw nodes (no over-bright grid), corner marks on the
 // selection only, grid hidden until a drag, collapsible tools.
 assert(editor->m_outlines->blend.src==GL_SRC_ALPHA&&editor->m_gridLines->blend.src==GL_SRC_ALPHA);
 assert(editor->m_outlines->polygons==0&&editor->m_outlines->segments==8);
 assert(!editor->m_gridLines->isVisible());
 editor->onCollapse(nullptr);assert(!editor->m_toolbar->isVisible()&&editor->m_expandButton->isVisible());
 for(auto const& entry:editor->m_toolbarItems)assert(!entry.item->isVisible());
 editor->onExpand(nullptr);assert(editor->m_toolbar->isVisible()&&!editor->m_expandButton->isVisible());
 auto const timer=hudDefaultPivot(HudMode::Solo,"timer",569,320).value();
 // Tap the clock, drag it 69,-20 (clear of the round state label): the
 // lower-left corner snaps to the 8 px grid.
 assert(editor->beginTouch({timer.x,timer.y-6}));assert(editor->m_gridLines->isVisible());
 editor->moveTouch({timer.x+69,timer.y-26});editor->endTouch();assert(!editor->m_gridLines->isVisible());
 auto* clock=editor->m_groups.at("timer");
 CCRect box;bool any=false;includeBox(clock,editor->m_canvas,clock,box,any);assert(any);
 assert(std::fmod(box.getMinX(),8.0f)==0&&std::fmod(box.getMinY(),8.0f)==0);
 assert(editor->m_working.at("timer").moved&&editor->m_working.at("timer").anchor==HudAnchor::Top);
 // Grid off: drags move by 1 px; the arrows nudge exactly 1 px.
 editor->onGrid(nullptr);assert(editor->m_grid==1);
 // Grab the clock near its top edge: the score difference widens the group over the state label.
 auto const before=box;assert(editor->beginTouch(box.origin+CCPoint{2,box.size.height-3}));editor->moveTouch(box.origin+CCPoint{5,box.size.height-3});editor->endTouch();
 any=false;includeBox(clock,editor->m_canvas,clock,box,any);assert(std::abs(box.getMinX()-before.getMinX()-3)<.01f);
 auto const nudgeFrom=box.getMinY();
 for(auto* arrow:editor->m_nudgeButtons)if(arrow->getTag()==1)arrow->activate();
 any=false;includeBox(clock,editor->m_canvas,clock,box,any);assert(std::abs(box.getMinY()-nudgeFrom-1)<.01f);
 editor->onGrid(nullptr);assert(editor->m_grid==8);
 // Dragging far off screen keeps the whole element inside the window.
 assert(editor->beginTouch(box.origin+CCPoint{2,box.size.height-3}));editor->moveTouch({-900,-900});editor->endTouch();
 any=false;includeBox(clock,editor->m_canvas,clock,box,any);assert(box.getMinX()>=0&&box.getMinY()>=0);
 assert(editor->m_working.at("timer").anchor==HudAnchor::BottomLeft);
 // The timer cannot be hidden, but can be made transparent (and back).
 auto* hide=button(editor,"HIDE");assert(hide&&!hide->isEnabled());editor->onToggleVisible(nullptr);
 assert(editor->m_working.at("timer").visible);
 editor->m_transparentButton->activate();editor->m_transparentButton->activate();editor->m_transparentButton->activate();
 assert(std::abs(editor->m_working.at("timer").opacity-.7f)<.01f&&editor->m_transparencyLabel->text=="30%");
 assert(clock->getOpacity()==hudOpacityByte(.7f));
 // The group opacity cascades through plain/RGBA rows to every label.
 for(auto* child:clock->children)if(auto*label=dynamic_cast<CCLabelBMFont*>(child))assert(label->getDisplayedOpacity()<=hudOpacityByte(.7f));
 for(int i=0;i<12;i++)editor->m_transparentButton->activate();
 assert(std::abs(editor->m_working.at("timer").opacity-.1f)<.01f&&!editor->m_transparentButton->isEnabled());
 for(int i=0;i<12;i++)editor->m_opaqueButton->activate();
 assert(std::abs(editor->m_working.at("timer").opacity-1)<.01f&&!editor->m_opaqueButton->isEnabled());
 // Next element (round) can be hidden and resized within 50-200 % in 5 % steps.
 editor->onNext(nullptr);assert(editor->m_selected=="round");assert(hide->isEnabled());hide->activate();
 assert(!editor->m_working.at("round").visible&&editor->m_groups.at("round")->isVisible()&&editor->m_groups.at("round")->getOpacity()<100);
 editor->onLarger(nullptr);assert(std::abs(editor->m_working.at("round").scale-1.05f)<.001f&&editor->m_scaleLabel->text=="105%");
 for(int i=0;i<30;i++)editor->onLarger(nullptr);assert(std::abs(editor->m_working.at("round").scale-2.0f)<.001f);
 for(int i=0;i<40;i++)editor->onSmaller(nullptr);assert(std::abs(editor->m_working.at("round").scale-.5f)<.001f);
 // Own score is its own element, separate from the name and checks.
 auto const scorePivot=hudDefaultPivot(HudMode::Solo,"own-score",569,320).value();
 assert(editor->beginTouch({scorePivot.x+8,scorePivot.y-8}));editor->endTouch();assert(editor->m_selected=="own-score");
 // Clear checks and death-match attempts share a spot without an overlap warning.
 while(editor->m_selected!="own-attempt")editor->onNext(nullptr);
 assert(editor->m_tag->text.find("OVERLAP")==std::string::npos);
 // The element list selects and toggles; the canvas is inert while it is open.
 auto* list=button(editor,"LIST");assert(list);list->activate();
 assert(editor->m_listOpen&&editor->m_list->isVisible()&&!editor->m_toolbar->isVisible());
 for(auto* item:editor->m_listItems)assert(item->isVisible());
 assert(editor->beginTouch({timer.x,timer.y-6})&&editor->m_drag==HudEditorPopup::Drag::None);
 auto row=[&](std::string const& id){return std::find_if(editor->m_listRows.begin(),editor->m_listRows.end(),[&](auto const& r){return r.id==id;});};
 assert(!row("timer")->toggle->isEnabled()&&row("feed")->toggle->isEnabled());
 row("feed")->toggle->activate();assert(!editor->m_working.at("feed").visible&&row("feed")->toggleCaption->text=="HIDDEN");
 assert(!editor->beginTouch(row("feed")->toggle->getPosition()));
 row("opponent-score")->select->activate();
 assert(editor->m_selected=="opponent-score"&&!editor->m_listOpen&&editor->m_toolbar->isVisible());
 for(auto* item:editor->m_listItems)assert(!item->isVisible());
 // Toolbar buttons fall through to the modal menu; the toolbar chrome drags.
 auto* save=button(editor,"SAVE");assert(save);
 assert(!editor->beginTouch(save->getPosition()));
 auto const toolbar=editor->m_toolbar->getPosition();
 assert(editor->beginTouch(toolbar+CCPoint{0,82}));editor->moveTouch(toolbar+CCPoint{-500,82});editor->endTouch();
 assert(editor->m_toolbar->getPosition().x==156&&save->getPosition().x==156+68);
 // Presets replace the working layout; cancel never writes.
 editor->onPreset(button(editor,"Minimal"));assert(editor->m_working==hudPresetPlacements(HudMode::Solo,HudPreset::Minimal));
 editor->onCancel(nullptr);assert(editor->closed&&saved.empty());
 // SAVE persists only this mode and keeps the other mode's layout.
 HudLayoutConfig before2;before2.survival["rank"]=hudScaledPlacement(.7f);saveHudLayout(before2);
 auto* second=HudEditorPopup::create(HudMode::Solo);second->onPreset(button(second,"Broadcast"));
 second->m_selected="own-name";second->onMoreTransparent(nullptr);save=button(second,"SAVE");save->activate();
 assert(second->closed);auto const stored=loadHudLayout();
 auto expected=hudPresetPlacements(HudMode::Solo,HudPreset::Focus);expected["own-name"].opacity=.9f;
 assert(stored.solo.size()==expected.size()&&std::abs(stored.solo.at("own-name").opacity-.9f)<.01f&&stored.survival==before2.survival);
 // The survival editor previews every element and the markers are visibility-only.
 auto* survival=HudEditorPopup::create(HudMode::Survival);
 for(auto const& spec:hudElements(HudMode::Survival)){CCRect b;bool found=false;includeBox(survival->m_groups.at(std::string(spec.id)),survival->m_canvas,survival->m_groups.at(std::string(spec.id)),b,found);assert(found);}
 while(survival->m_selected!="cutline")survival->onNext(nullptr);
 assert(!survival->m_smallerButton->isEnabled()&&!survival->m_largerButton->isEnabled());
 for(auto* arrow:survival->m_nudgeButtons)assert(!arrow->isEnabled());
 survival->keyBackClicked();assert(survival->closed);
}
`);
});
