import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){const start=source.indexOf(name);if(start<0)throw Error(name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
function run(source){const dir=mkdtempSync(join(tmpdir(),'collection-faces-'));try{writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('collection order is Rated/Featured/Epic then centered Legendary/Mythic, with disjoint squares and count labels',()=>run(`
#include "domain/SurvivalCollectionPresentation.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
using namespace corum::ranked;
int main(){
 assert((SurvivalCollectionGrades==std::array<std::string_view,5>{"RATED","FEATURED","EPIC","LEGENDARY","MYTHIC"}));
 auto first=survivalCollectionGradeCell(0),middle=survivalCollectionGradeCell(1),last=survivalCollectionGradeCell(2),four=survivalCollectionGradeCell(3),five=survivalCollectionGradeCell(4);
 assert(first.y==middle.y&&middle.y==last.y&&four.y==five.y&&first.y>four.y);
 assert(first.x<middle.x&&middle.x<last.x&&four.x<five.x);
 assert((first.x+last.x)/2==middle.x&&(four.x+five.x)/2==middle.x);
 for(auto screen: {std::pair{426.67f,320.f},std::pair{568.f,320.f},std::pair{854.f,480.f},std::pair{320.f,240.f}}){
  float scale=std::min({1.f,(screen.first-24)/410,(screen.second-28)/292});
  for(unsigned i=0;i<5;i++){auto a=survivalCollectionGradeCell(i);
   assert(a.x-a.side/2>=0&&a.x+a.side/2<=410&&a.y+a.side/2<206);
   assert(a.countY-9>=0&&a.countY+9<a.y-a.side/2);
   assert((a.x+a.side/2)*scale<=screen.first-24&&(a.y+a.side/2)*scale<=screen.second-28);
   for(unsigned j=0;j<i;j++){auto b=survivalCollectionGradeCell(j);assert(std::abs(a.x-b.x)>(a.side+b.side)/2||std::abs(a.y-b.y)>(a.side+b.side)/2);}
  }
 }
}`));
test('actual collection factory uses native GD effects, preserves ornament transforms and fits the complete face into dark square chrome',()=>{
 const source=read('src/ui/SurvivalCollectionBadge.hpp');
 run(`#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};
struct CCRect{CCPoint origin;CCSize size;CCRect(float x=0,float y=0,float w=0,float h=0):origin{x,y},size{w,h}{}float getMinX()const{return origin.x;}float getMaxX()const{return origin.x+size.width;}float getMinY()const{return origin.y;}float getMaxY()const{return origin.y+size.height;}float getMidX()const{return origin.x+size.width/2;}float getMidY()const{return origin.y+size.height/2;}};
struct Transform{float a,b,c,d,tx,ty;};
CCRect CCRectApplyAffineTransform(CCRect r,Transform t){float left=1e9f,bottom=1e9f,right=-1e9f,top=-1e9f;for(float x:{r.getMinX(),r.getMaxX()})for(float y:{r.getMinY(),r.getMaxY()}){float xx=t.a*x+t.c*y+t.tx,yy=t.b*x+t.d*y+t.ty;left=std::min(left,xx);right=std::max(right,xx);bottom=std::min(bottom,yy);top=std::max(top,yy);}return {left,bottom,right-left,top-bottom};}
struct CCNode{virtual~CCNode()=default;CCSize size;CCPoint position,anchor;float scale=1,rotation=0;bool visible=true;std::vector<CCNode*>children;CCSize getContentSize()const{return size;}CCPoint getAnchorPointInPoints()const{return {anchor.x*size.width,anchor.y*size.height};}auto const&getChildrenExt()const{return children;}bool isVisible()const{return visible;}void setScale(float v){scale=v;}void setPosition(CCPoint p){position=p;}void addChild(CCNode*n,int){children.push_back(n);}Transform nodeToParentTransform()const{auto p=getAnchorPointInPoints();float a=std::cos(rotation)*scale,b=std::sin(rotation)*scale,c=-b,d=a;return {a,b,c,d,position.x-a*p.x-c*p.y,position.y-b*p.x-d*p.y};}};
enum class GJDifficultyName{Short};enum class GJFeatureState{None,Featured,Epic,Legendary,Mythic};
struct GJDifficultySprite:CCNode{int code=0,updates=0;GJFeatureState feature=GJFeatureState::None;static GJDifficultySprite*create(int code,GJDifficultyName){auto*p=new GJDifficultySprite;p->code=code;p->size={48,54};p->anchor={.5f,.5f};return p;}void updateFeatureState(GJFeatureState s){feature=s;updates++;if(s!=GJFeatureState::None){auto*n=new CCNode;n->size={70.f+10*static_cast<int>(s),68};n->anchor={.5f,.5f};n->position={31,29};n->rotation=.18f;auto*g=new CCNode;g->size={11,13};g->position={66,54};g->rotation=-.2f;n->addChild(g,1);addChild(n,0);}}};
struct Color{int id;bool operator==(Color const&)const=default;};constexpr Color Blue{1},Red{2},Surface{3};
Color survivalBadgeColor(std::string_view){return {4};}std::vector<std::string>captions;
namespace FontWeight{constexpr int Black=1;}
CCNode*menuLabel(std::string s,float,CCPoint,Color,int){captions.push_back(s);return new CCNode;}
void fitFullMenuLabel(CCNode*,float,float,float){}
Color lastFill{};int lastOpacity=0;bool lastStrong=false;
CCNode*surface(CCSize size,CCPoint position,Color,Color fill,int opacity,bool strong){lastFill=fill;lastOpacity=opacity;lastStrong=strong;auto*p=new CCNode;p->size=size;p->position=position;return p;}
${method(source,'inline GJFeatureState survivalCollectionFeature(')}
${method(source,'inline CCRect survivalCollectionVisualBounds(')}
${method(source,'inline CCNode* survivalCollectionBadge(')}
int main(){std::string_view grades[]={"RATED","FEATURED","EPIC","LEGENDARY","MYTHIC"};
 for(int code:{3,4,5,7,8,6,9})for(unsigned i=0;i<5;i++)for(float side:{44.f,74.f,76.f}){
  auto* card=survivalCollectionBadge(code,grades[i],side);assert(card->size.width==side&&card->size.height==side);assert(lastFill==Surface&&lastOpacity==242&&lastStrong);assert(captions.empty());
  auto*face=dynamic_cast<GJDifficultySprite*>(card->children[0]);assert(face&&face->code==code&&face->updates==1&&face->feature==static_cast<GJFeatureState>(i));
  if(i){assert(face->children[0]->position.x==31&&face->children[0]->position.y==29&&face->children[0]->rotation==.18f);}
  auto bounds=CCRectApplyAffineTransform(survivalCollectionVisualBounds(face),face->nodeToParentTransform());
  assert(std::abs(bounds.getMidX()-side/2)<.001f&&std::abs(bounds.getMidY()-side/2)<.001f);
  assert(bounds.getMinX()>=7.99f&&bounds.getMinY()>=7.99f&&bounds.getMaxX()<=side-7.99f&&bounds.getMaxY()<=side-7.99f);
 }
 auto*cross=survivalCollectionBadge(0,"ELIMINATED",76);assert(cross&&captions==std::vector<std::string>{"X"});
}
`);
});
