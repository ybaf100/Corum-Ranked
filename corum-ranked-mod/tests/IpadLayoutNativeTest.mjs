import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,signature){
    const start=source.indexOf(signature);assert.ok(start>=0,signature);
    let depth=0;for(let i=source.indexOf('{',start);i<source.length;i++){
        if(source[i]==='{')depth++;else if(source[i]==='}'&&--depth===0)return source.slice(start,i+1);}
    assert.fail(signature);
}
function run(cpp) {
    const directory=mkdtempSync(join(tmpdir(),'corum-ipad-'));
    try {
        writeFileSync(join(directory,'test.cpp'),cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(directory,'test.cpp'),'-o',join(directory,'test')],{stdio:'pipe'});
        execFileSync(join(directory,'test'),[],{stdio:'pipe'});
    } finally {rmSync(directory,{recursive:true,force:true});}
}

test('badge showcase lays every card inside the board without clipping or dragging, for any difficulty count',()=>{
    const source=read('src/ui/SurvivalBadgeCollection.cpp');
    assert.doesNotMatch(source,/CCClippingNode|BadgeStrip|ccTouchMoved/);
    run(`#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};
struct ccColor3B{unsigned char r,g,b;};struct ccColor4B{unsigned char r,g,b,a;};
struct CCObject{virtual ~CCObject()=default;};
struct CCNode:CCObject{CCSize size;CCPoint pos;int tag=-1;std::vector<CCNode*> children;CCNode* parent=nullptr;
 static CCNode* create(){return new CCNode;}void setContentSize(CCSize s){size=s;}void setPosition(CCPoint p){pos=p;}
 void addChild(CCNode* c,int=0){children.push_back(c);c->parent=this;}void setTag(int t){tag=t;}
 void removeFromParentAndCleanup(bool){if(parent)std::erase(parent->children,this);parent=nullptr;}};
struct CCLayerColor:CCNode{static CCLayerColor* create(ccColor4B,float w,float h){auto* n=new CCLayerColor;n->size={w,h};return n;}};
struct CCLabelBMFont:CCNode{};
struct CCMenuItemSpriteExtra:CCNode{static CCMenuItemSpriteExtra* create(CCNode* face,CCObject*,int){auto* n=new CCMenuItemSpriteExtra;n->size=face->size;n->addChild(face);return n;}};
#define menu_selector(x) 0
constexpr ccColor3B Surface{1,2,3},SurfaceSoft{4,5,6},Red{7,8,9},Text{1,1,1};
namespace FontWeight{constexpr int ExtraBold=1;}
CCLabelBMFont* menuLabel(std::string const&,float,CCPoint p,ccColor3B,int){auto* l=new CCLabelBMFont;l->pos=p;return l;}
void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
CCNode* survivalCollectionBadge(int,std::string_view,float side,float=0){auto* n=new CCNode;n->size={side,side};return n;}
namespace fmt{std::string format(char const*,std::int64_t v){return std::to_string(v);}}
namespace matjson{struct Value{struct R{std::int64_t unwrapOr(std::int64_t v)const{return v;}};
 Value operator[](std::string const&)const{return {};}R asInt()const{return {};}};}
std::int64_t survivalBadgeCollectionTotal(matjson::Value const&){return 3;}
struct BadgeCollection:CCObject{CCNode menu;CCNode* m_buttonMenu=&menu;std::vector<CCMenuItemSpriteExtra*> m_cards;
 void onCard(CCObject*){}
 ${method(source,'void clearCards(')}
 ${method(source,'void layoutCards(')}
};
int main(){
 for(std::size_t difficulties=0;difficulties<=17;++difficulties){
  BadgeCollection board;std::vector<matjson::Value> rows(difficulties);board.layoutCards(rows,2);
  assert(board.m_cards.size()==difficulties+1&&board.menu.children.size()==difficulties+1);
  for(std::size_t i=0;i<board.m_cards.size();++i){auto* a=board.m_cards[i];assert(a->tag==static_cast<int>(i));
   // Inside the 540 x 196 board area between the title row and the hint.
   assert(a->pos.x-a->size.width/2>=14-.01f&&a->pos.x+a->size.width/2<=554+.01f);
   assert(a->pos.y-a->size.height/2>=78-.01f&&a->pos.y+a->size.height/2<=274+.01f);
   assert(a->size.width>=40&&a->size.height>=40);
   for(std::size_t j=i+1;j<board.m_cards.size();++j){auto* b=board.m_cards[j];
    bool apart=std::abs(a->pos.x-b->pos.x)>=(a->size.width+b->size.width)/2||std::abs(a->pos.y-b->pos.y)>=(a->size.height+b->size.height)/2;assert(apart);}
  }
  // A refresh replaces the cards instead of stacking them.
  board.layoutCards(rows,2);assert(board.menu.children.size()==difficulties+1);
 }
}`);
});

test('popup content keeps the native close button on the panel corner when a narrow screen shrinks the canvas',()=>{
    const helper=read('src/ui/RankedPopupLayout.hpp').split('\n').filter(l=>!l.startsWith('#include')&&!l.startsWith('#pragma')).join('\n');
    run(`#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};
struct CCNode{CCSize size;CCPoint pos,anchor;float scale=1;bool ignore=true;std::vector<CCNode*> children;
 static CCNode* create(){return new CCNode;}void setContentSize(CCSize s){size=s;}void setAnchorPoint(CCPoint p){anchor=p;}
 void setPosition(CCPoint p){pos=p;}CCPoint getPosition()const{return pos;}void setScale(float s){scale=s;}void ignoreAnchorPointForPosition(bool v){ignore=v;}
 void addChild(CCNode* c,int=0){children.push_back(c);}
 CCPoint toParent(CCPoint p)const{auto const ax=ignore?0:anchor.x*size.width,ay=ignore?0:anchor.y*size.height;return {pos.x+(p.x-ax)*scale,pos.y+(p.y-ay)*scale};}};
struct CCArray{std::vector<CCNode*> items;unsigned count()const{return static_cast<unsigned>(items.size());}CCNode* objectAtIndex(unsigned i)const{return items[i];}};
struct CCMenu:CCNode{CCArray array;CCArray* getChildren(){array.items=children;return &array;}};
struct CCLayer:CCNode{void setLayout(void*){}};
namespace cocos2d{using ::CCNode;using ::CCMenu;using ::CCLayer;using ::CCSize;}
${helper}
int main(){using namespace corum::ranked::ui;
 for(auto viewport:{CCSize{568,320},CCSize{458,320},CCSize{427,320}}){
  auto layout=rankedPopupLayout({568,320},viewport);
  CCLayer main;CCMenu menu;menu.size=layout.size;CCNode close;close.pos={3,layout.size.height-3};menu.addChild(&close);
  auto const before=menu.toParent(close.pos);
  rankedPopupContent(&main,&menu,layout);
  auto const after=menu.toParent(close.pos);
  assert(std::abs(before.x-after.x)<.01f&&std::abs(before.y-after.y)<.01f);
 }
}`);
});

test('achievements keep the pager and the last card row clear of the floating SETTINGS button',()=>{
    const popup=read('src/RankedPopup.cpp');
    const constants=popup.match(/static constexpr float kSettingsButtonWidth = ([\d.]+)f, kSettingsButtonInset = ([\d.]+)f, kSettingsButtonTop = ([\d.]+)f;/);
    assert.ok(constants);const [,width,inset,top]=constants.map(Number);
    // The floating button is 92 x 28 centred 27 points up: its top is 41.
    assert.equal(top,27+28/2);assert.equal(width,92);
    const settingsInset=Number(popup.match(/settings->setPosition\(\{size\.width-(\d+)-settings->getContentSize\(\)\.width\/2,27\}\)/)[1]);
    assert.equal(settingsInset,inset);
    const achievements=method(popup,'    void renderAchievements(');
    assert.match(achievements,/gridBottom = kSettingsButtonTop \+ 5\.0f;/);
    assert.match(achievements,/pagerRight = size\.width - kSettingsButtonInset - kSettingsButtonWidth - 8\.0f;/);
    assert.match(achievements,/\{pagerRight - 12\.0f, 20\.0f\}/);
    assert.ok(inset+width+8<=568-28);
});

test('the Solo radar fits between the profile section and the search column on narrow screens',()=>{
    const popup=read('src/RankedPopup.cpp');
    assert.match(popup,/radarRadius = std::min\(\{100\.0f, \(top - bottom\) \/ 2\.0f - 8\.0f, \(win\.width - rightW - x1\) \/ 2\.0f - 6\.0f\}\);/);
    for(const width of [427,458,568]){
        const x1=165,rightW=150,radius=Math.max(20,Math.min(100,(284-38)/2-8,(width-rightW-x1)/2-6));
        const centre=(x1+width-rightW)/2;
        assert.ok(centre-radius>=x1&&centre+radius<=width-rightW,String(width));
    }
});

test('page changes dissolve from a still of the old page and user navigation skips the settle delay',()=>{
    const popup=read('src/RankedPopup.cpp');
    const prepare=method(popup,'    bool prepareUiTransition(');
    assert.match(prepare,/immediate = enteringMatch \|\| localUi != m_renderedLocalUi;/);
    const signature=method(popup,'    std::string localUiSignature(');
    for(const field of ['m_page','m_achievementPage','m_achievementScope','m_historyScope','m_historyIndex','m_historyTabPage','m_historyOrder','m_historyResult','m_titleSelectorAchievement'])
        assert.ok(signature.includes(field),field);
    assert.match(prepare,/m_fadeInNextRender = !beginCrossfade\(crossfadeSeconds\(\)\);/);
    assert.doesNotMatch(prepare,/CCFadeTo::create\(static_cast<float>\(fadeOut\), 0\)/);
    const crossfade=method(popup,'    bool beginCrossfade(');
    assert.match(crossfade,/CCRenderTexture::create/);
    assert.match(crossfade,/glColorMask\(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE\);\s*m_fadeRoot->visit\(\);\s*glColorMask\(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE\);/);
    assert.match(crossfade,/CCRemoveSelf::create\(\)/);
});
