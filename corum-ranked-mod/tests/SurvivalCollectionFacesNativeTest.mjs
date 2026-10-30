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
test('square badge factory keeps equal modest body heights across all native grades, reserves every ornament, and supports result caption margins',()=>{
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
struct GJDifficultySprite:CCNode{int code=0,updates=0;GJFeatureState feature=GJFeatureState::None;static GJDifficultySprite*create(int code,GJDifficultyName){auto*p=new GJDifficultySprite;p->code=code;p->size={48.f+code,54.f+code};p->anchor={.5f,.5f};return p;}void updateFeatureState(GJFeatureState s){feature=s;updates++;if(s!=GJFeatureState::None){auto*n=new CCNode;n->size={70.f+10*static_cast<int>(s),68};n->anchor={.5f,.5f};n->position={31,29};n->rotation=.18f;auto*g=new CCNode;g->size={11,13};g->position={66,54};g->rotation=-.2f;n->addChild(g,1);addChild(n,0);}}};
struct Color{int id;bool operator==(Color const&)const=default;};constexpr Color Blue{1},Red{2},Surface{3};
Color survivalBadgeColor(std::string_view){return {4};}std::vector<std::string>captions;
namespace FontWeight{constexpr int Black=1;}
CCNode*menuLabel(std::string s,float,CCPoint,Color,int){captions.push_back(s);return new CCNode;}
void fitFullMenuLabel(CCNode*,float,float,float){}
Color lastFill{};int lastOpacity=0;bool lastStrong=false;
CCNode*surface(CCSize size,CCPoint position,Color,Color fill,int opacity,bool strong){lastFill=fill;lastOpacity=opacity;lastStrong=strong;auto*p=new CCNode;p->size=size;p->position=position;return p;}
${method(source,'inline GJFeatureState survivalCollectionFeature(')}
${method(source,'inline CCRect survivalCollectionVisualBounds(')}
struct SurvivalBadgeFaceEnvelope {float halfWidth=0,halfHeight=0;};
${method(source,'inline SurvivalBadgeFaceEnvelope survivalBadgeFaceEnvelope(')}
${method(source,'inline CCNode* survivalCollectionBadge(')}
int main(){std::string_view grades[]={"RATED","FEATURED","EPIC","LEGENDARY","MYTHIC"};
 // 44 is the caption-free header preview; result cards start at 53 points.
 for(auto const& [side,reserved]:{std::pair{44.f,0.f},std::pair{53.f,22.f},std::pair{62.f,22.f},std::pair{74.f,0.f},std::pair{76.f,0.f},std::pair{78.f,22.f}}){
 float referenceHeight=-1;auto area=side-reserved;
 for(int code:{3,4,5,7,8,6,9})for(unsigned i=0;i<5;i++){
  auto* card=survivalCollectionBadge(code,grades[i],side,area);assert(card->size.width==side&&card->size.height==side);assert(lastFill==Surface&&lastOpacity==242&&lastStrong);assert(captions.empty());
  auto*face=dynamic_cast<GJDifficultySprite*>(card->children[0]);assert(face&&face->code==code&&face->updates==1&&face->feature==static_cast<GJFeatureState>(i));
  if(i){assert(face->children[0]->position.x==31&&face->children[0]->position.y==29&&face->children[0]->rotation==.18f);}
  auto const height=face->size.height*face->scale;if(referenceHeight<0)referenceHeight=height;
  assert(std::abs(height-referenceHeight)<.001f&&height<=area*.4601f&&height>area*.20f);
  auto const body=CCRectApplyAffineTransform({0,0,face->size.width,face->size.height},face->nodeToParentTransform());
  assert(std::abs(body.getMidX()-side/2)<.001f&&std::abs(body.getMidY()-side/2)<.001f);
  auto bounds=CCRectApplyAffineTransform(survivalCollectionVisualBounds(face),face->nodeToParentTransform());
  auto margin=reserved/2+6;
  assert(bounds.getMinX()>=margin-.01f&&bounds.getMinY()>=margin-.01f&&bounds.getMaxX()<=side-margin+.01f&&bounds.getMaxY()<=side-margin+.01f);
 }}
 auto*cross=survivalCollectionBadge(0,"ELIMINATED",76);assert(cross&&captions==std::vector<std::string>{"X"});
}
`);
});
test('official round cards share square native visuals, retain recorded difficulty/round/rank, and gallery totals include zero and all official grades',()=>{
 const round=read('src/ui/SurvivalRoundBadge.hpp'),counts=read('src/domain/SurvivalBadgeCollectionData.hpp'),collection=read('src/ui/SurvivalBadgeCollection.cpp');
 run(`#include "domain/MapDifficulty.hpp"
#include "domain/SurvivalBadgePresentation.hpp"
#include <cassert>
#include <cstdint>
#include <map>
#include <vector>
using namespace corum::ranked;
namespace matjson{
template<class T>struct Result{T value;T unwrapOr(T)const{return value;}};
struct Value{std::map<std::string,Value> fields;std::string text;std::int64_t number=0;
 Value const&operator[](std::string const&key)const{static Value empty;auto it=fields.find(key);return it==fields.end()?empty:it->second;}
 Result<std::string>asString()const{return {text};}Result<std::int64_t>asInt()const{return {number};}};
}
struct CCPoint{float x,y;};struct CCSize{float width,height;};
struct CCNode{CCSize size{};CCPoint anchor{};int code=0;float area=0;std::string grade;std::vector<CCNode*>children;void setAnchorPoint(CCPoint p){anchor=p;}void addChild(CCNode*n,int){children.push_back(n);}};
constexpr int Text=1;namespace FontWeight{constexpr int ExtraBold=1;}
std::vector<std::string>captions;
CCNode*menuLabel(std::string text,float,CCPoint,int,int){captions.push_back(text);return new CCNode;}
void fitFullMenuLabel(CCNode*,float,float,float){}
CCNode*survivalCollectionBadge(int code,std::string_view grade,float side,float area){auto*p=new CCNode;p->size={side,side};p->code=code;p->grade=grade;p->area=area;return p;}
namespace fmt{std::string format(char const*,std::int64_t n){return "#"+std::to_string(n);}}
${method(round,'inline CCNode* survivalRoundBadge(')}
${method(counts,'inline std::int64_t survivalBadgeCollectionTotal(')}
${method(collection,'std::string difficultyName(')}
int main(){
 for(auto const& [id,name]:{std::pair{"HARD","Hard"},std::pair{"HARDER","Harder"},std::pair{"EASY_DEMON","Easy Demon"}}){matjson::Value d;d.fields["id"].text=id;assert(difficultyName(d)==name);d.fields["name"].text=name;assert(difficultyName(d)==name);}
 matjson::Value counts;assert(survivalBadgeCollectionTotal(counts)==0);
 counts.fields["MYTHIC"].number=2147483648LL;counts.fields["RATED"].number=5;counts.fields["ELIMINATED"].number=4;
 assert(survivalBadgeCollectionTotal(counts)==2147483653LL);
 matjson::Value eliminatedOnly;eliminatedOnly.fields["ELIMINATED"].number=100;assert(survivalBadgeCollectionTotal(eliminatedOnly)==0);
 for(auto grade:SurvivalBadgeGrades){
  matjson::Value b;b.fields["grade"].text=grade;b.fields["difficulty"].text="EASY_DEMON";b.fields["roundLabel"].text="R2";b.fields["rank"].number=2;captions.clear();
  auto*card=survivalRoundBadge(b,{62,84});assert(card&&card->size.width==62&&card->size.height==62&&card->code==7&&card->grade==grade&&card->area==40);
  assert((captions==std::vector<std::string>{"R2","#2"}));
  captions.clear();assert(survivalRoundBadge(b,{78,78},false));assert((captions==std::vector<std::string>{"#2"}));
 }
 matjson::Value notEarned;assert(!survivalRoundBadge(notEarned,{62,62}));
}
`);
});
