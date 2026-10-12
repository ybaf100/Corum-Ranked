// GD network and delegate boundaries are doubles; the real resolver/callbacks run.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = name => readFileSync(join(root, name), 'utf8');
const extract = (source, marker) => {
    const start = source.indexOf(marker); assert(start >= 0, marker);
    let end = source.indexOf('{', start) + 1, depth = 1;
    while (depth) { depth += (source[end] === '{') - (source[end] === '}'); ++end; }
    return source.slice(start, end);
};
function run(source) {
    const dir = mkdtempSync(join(tmpdir(), 'corum-map-details-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), source);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio:'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio:'pipe'});
    } finally { rmSync(dir, {recursive:true, force:true}); }
}
test('native map metadata maps all GD faces including Demon subtypes and ignores unrelated Corum ratings', () => {
    run(`#include "${join(root,'src/domain/MapDifficulty.hpp')}"
#include <cassert>
#include <string>
int main(){using namespace corum::ranked;
assert(mapDifficulty(true,false,0,0).label=="Auto");
assert(mapDifficulty(false,false,0,1).label=="Easy");
assert(mapDifficulty(false,false,0,2).label=="Normal");
assert(mapDifficulty(false,false,0,3).label=="Hard");
assert(mapDifficulty(false,false,0,4).label=="Harder");
assert(mapDifficulty(false,false,0,5).label=="Insane");
for(int invalid:{-1,0,6,21,999}) assert(mapDifficulty(false,false,0,invalid).label=="N/A");
assert(mapDifficulty(false,true,3,0).label=="Easy Demon");
assert(mapDifficulty(false,true,4,0).label=="Medium Demon");
assert(mapDifficulty(false,true,5,0).label=="Insane Demon");
assert(mapDifficulty(false,true,6,0).label=="Extreme Demon");
assert(mapDifficulty(false,true,0,0).label=="Hard Demon");
assert(mapDifficulty(false,true,2,0).frame=="diffIcon_06_btn_001.png");
assert(mapDifficulty(false,true,3,5).frame=="diffIcon_07_btn_001.png");
}`);
});

test('actual appearance resolver serializes lookups, survives A→B, restores foreign delegates and never attests geometry', () => {
    const header=read('src/RankedMapDetails.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('private:','public:');
    const cpp=read('src/RankedMapDetails.cpp').replace(/^#include.*$/gm,'');
    const route=extract(read('src/RankedLevelPreparation.cpp'), 'void onGetOnlineLevelsCompleted(gd::string response');
    run(`
#include "${join(root,'src/domain/MapDifficulty.hpp')}"
#include "${join(root,'src/domain/MapMetadata.hpp')}"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace gd {using string=std::string;}
enum class GJLevelType{Local,Saved,SearchResult};
struct GJGameLevel {GJLevelType m_levelType=GJLevelType::Saved;int m_levelID=0,m_demon=0,m_demonDifficulty=0,average=0,m_stars=0,m_timestamp=0;bool m_autoLevel=false;
int getAverageDifficulty(){return average;}};
namespace cocos2d {struct CCArray {std::vector<GJGameLevel*> values;};}
using cocos2d::CCArray;
struct LevelManagerDelegate {virtual ~LevelManagerDelegate()=default;
virtual void loadLevelsFinished(CCArray*,char const*){} virtual void loadLevelsFinished(CCArray*,char const*,int){}
virtual void loadLevelsFailed(char const*){} virtual void loadLevelsFailed(char const*,int){}
virtual void setupPageInfo(gd::string,char const*){}};
template<class T> struct CCArrayExt:std::vector<T>{explicit CCArrayExt(CCArray* a):std::vector<T>(a->values.begin(),a->values.end()){}};
enum class SearchType {Type19};
struct GJSearchObject {std::string key;static GJSearchObject* create(SearchType,std::string q){static GJSearchObject s;s.key=q;return &s;}
char const* getKey(){return key.c_str();}};
struct Dictionary {int removals=0;void removeObjectForKey(char const*){++removals;}};
struct GameLevelManager {
static GameLevelManager* instance;static GameLevelManager* sharedState(){return instance;}
LevelManagerDelegate* m_levelManagerDelegate=nullptr;Dictionary stored;Dictionary* m_storedLevels=&stored;
int queries=0;std::string lastKey;CCArray result;
bool isDLActive(char const*){return false;}void resetTimerForKey(char const*){}
void getOnlineLevels(GJSearchObject* s){++queries;lastKey=s->key;}
void onGetOnlineLevelsCompleted(gd::string,gd::string tag){if(m_levelManagerDelegate)m_levelManagerDelegate->loadLevelsFinished(&result,tag.c_str());}
};
GameLevelManager* GameLevelManager::instance=nullptr;
namespace geode::prelude {namespace log {template<class...T>void warn(T...) {}}}
namespace corum::ranked {
struct Context {int levelId=0;bool valid()const{return levelId>0;}} current;
Context currentPreparationContext(){return current;}
struct RankedLevelPreparation:LevelManagerDelegate {
bool active=false;int callbacks=0;static RankedLevelPreparation& get(){static RankedLevelPreparation p;return p;}
bool busy(){return active;}std::uint64_t requestGeneration(char const* k){return active&&std::string(k)=="primary"?1:0;}
void responseCompleted(char const*,std::uint64_t){}void loadLevelsFinished(CCArray*,char const*)override{++callbacks;active=false;}
};}
${header}
${cpp}
struct RoutedManager:GameLevelManager {${route}};
struct Foreign:LevelManagerDelegate {int callbacks=0;void loadLevelsFinished(CCArray*,char const*)override{++callbacks;}};
int main(){using namespace corum::ranked;RoutedManager manager;GameLevelManager::instance=&manager;Foreign foreign;
manager.m_levelManagerDelegate=&foreign;auto& details=RankedMapDetails::get();
details.request({11,12});assert(manager.queries==1&&manager.m_levelManagerDelegate==&foreign);
auto keyA=manager.lastKey;auto genA=details.requestGeneration(keyA.c_str());assert(genA);
details.request({99});assert(manager.queries==1); // no overlap while old request is in flight
GJGameLevel easy;easy.m_levelID=11;easy.average=1;easy.m_stars=2;easy.m_timestamp=22560;
GJGameLevel demon;demon.m_levelID=12;demon.m_demon=1;demon.m_demonDifficulty=3;demon.m_stars=10;
GJGameLevel unrelated;unrelated.m_levelID=88;unrelated.average=5;
manager.result.values={&easy,&demon,&unrelated};manager.onGetOnlineLevelsCompleted("ok",keyA);
assert(manager.m_levelManagerDelegate==&foreign&&foreign.callbacks==0);
assert(details.difficulty(11).label=="Easy"&&details.difficulty(12).label=="Easy Demon");
assert(details.metadata(11).stars==2&&details.metadata(11).durationSeconds==94);
assert(details.metadata(12).kind=="DEMON"&&!details.metadata(12).stars);
assert(mapLengthText(details.metadata(11).durationSeconds)=="1m 34s");
assert(mapLengthText(details.metadata(12).durationSeconds)=="?m ?s");
assert(details.difficulty(88).label=="N/A");assert(!details.requestGeneration(keyA.c_str()));
auto revision=details.revision();details.remember(&easy);assert(details.revision()==revision);
easy.m_timestamp=0;details.remember(&easy);assert(details.revision()==revision&&details.metadata(11).durationSeconds==94);
details.m_retryAt={};details.request({99});assert(manager.queries==2&&manager.lastKey=="99,99&gameVersion=22");
auto keyB=manager.lastKey;details.responseCompleted(keyA.c_str(),genA);assert(details.requestGeneration(keyB.c_str()));
details.loadLevelsFailed(keyB.c_str());assert(!details.requestGeneration(keyB.c_str()));
details.request({99});assert(manager.queries==2); // bounded retry
manager.onGetOnlineLevelsCompleted("foreign","other");assert(foreign.callbacks==1&&manager.m_levelManagerDelegate==&foreign);
auto& primary=RankedLevelPreparation::get();primary.active=true;details.m_retryAt={};details.request({100});assert(manager.queries==2);
manager.onGetOnlineLevelsCompleted("primary","primary");assert(primary.callbacks==1&&manager.m_levelManagerDelegate==&foreign);
current.levelId=77;details.request({77});assert(manager.queries==2); // UI never competes with exact attestation
current.levelId=0;details.request({11,12});assert(manager.queries==2); // cached faces
details.m_cache.at(11).fetchedAt-=std::chrono::minutes(11);details.request({11});assert(manager.queries==3);
assert(manager.lastKey=="11,11&gameVersion=22");
}`);
});

test('Jukebox callback is optional, exact-owner checked, selector-present and invoked through the existing native widget',()=>{
    const popup=read('src/ui/RankedJukeboxPopup.hpp');
    const helpers=extract(popup,'inline bool rankedJukeboxLoaded()')+'\n'+extract(popup,'inline CCMenuItem* rankedJukeboxOpener(');
    run(`#include <cassert>
#include <string>
struct Loader {bool loaded=false;static Loader* get(){static Loader l;return &l;}bool isModLoaded(char const* id){assert(std::string(id)=="fleym.nongd");return loaded;}};
struct CCMenuItem {void* m_pListener=nullptr;int m_pfnSelector=0;bool enabled=true;bool isEnabled(){return enabled;}};
struct CustomSongWidget {void* m_songInfoObject=nullptr;CCMenuItem* button=nullptr;
CCMenuItem* getChildByIDRecursive(char const* id){assert(std::string(id)=="fleym.nongd/nong-button");return button;}};
template<class T>T typeinfo_cast(CCMenuItem* b){return static_cast<T>(b);}
${helpers}
int main(){CustomSongWidget widget;CCMenuItem item;widget.m_songInfoObject=&widget;widget.button=&item;item.m_pListener=&widget;item.m_pfnSelector=1;
assert(!rankedJukeboxOpener(&widget));Loader::get()->loaded=true;assert(rankedJukeboxOpener(&widget)==&item);
item.m_pListener=&item;assert(!rankedJukeboxOpener(&widget));item.m_pListener=&widget;item.m_pfnSelector=0;
assert(!rankedJukeboxOpener(&widget));item.m_pfnSelector=1;item.enabled=false;assert(!rankedJukeboxOpener(&widget));
item.enabled=true;widget.m_songInfoObject=nullptr;assert(!rankedJukeboxOpener(&widget));assert(!rankedJukeboxOpener(nullptr));}
`);
    assert.match(popup,/opener->activate\(\)/);
    assert.match(popup,/LevelTools::getSongObject\(level->m_audioTrack\)/);
    assert.match(popup,/updateWithMultiAssets\(level->m_songIDs, level->m_sfxIDs/);
    assert.match(popup,/m_context != currentPreparationContext\(\)/);
    assert.match(popup,/unregisterForcePrio\(this\)/);
    assert.doesNotMatch(popup, /JBSongWidget\*|reinterpret_cast|setTarget|addNongLayer\(/);
    const screen=read('src/RankedPopup.cpp');
    const button=extract(screen,'void addJukeboxButton(');
    assert.doesNotMatch(button,/if .* return/);
    assert.match(button,/grayButtonVisual\(m_jukeboxButton->getNormalImage\(\)\)/);
    assert.match(screen,/tr\("jukebox-required",/);
    assert.match(screen,/m_jukebox\.reset\(\)/);
    assert.match(read('src/RankedRoundPreparation.cpp'),/roundModalOpen\(origin\)/);
});

test('actual difficulty badge draws stars in a circle, white GDDL digits on a colored square and keeps exact length',()=>{
    const badge=read('src/ui/MapDifficultyBadge.hpp').replace(/^#include.*$|^#pragma once$/gm,'');
    run(`
#include "${join(root,'src/domain/MapDifficulty.hpp')}"
#include "${join(root,'src/domain/MapMetadata.hpp')}"
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <string_view>
#include <utility>
#include <vector>
using GLubyte=unsigned char;
namespace cocos2d {
// Geode 5.10.1 CCGeometry.h supports assignment from BOTH CCPoint and CCSize.
// An aggregate-only point double would incorrectly accept ambiguous {x,y} assignments.
struct CCSize;
struct CCPoint{
float x=0,y=0;
constexpr CCPoint()=default;
constexpr CCPoint(float x,float y):x(x),y(y){}
constexpr CCPoint(CCPoint const&)=default;
constexpr CCPoint&operator=(CCPoint const&)=default;
constexpr CCPoint&operator=(CCSize const&);
};
struct CCSize{
float width=20,height=20;
constexpr CCSize()=default;
constexpr CCSize(float width,float height):width(width),height(height){}
};
constexpr CCPoint&CCPoint::operator=(CCSize const&s){x=s.width;y=s.height;return *this;}
struct ccColor3B{GLubyte r,g,b;bool operator==(ccColor3B const&)const=default;};
struct ccColor4F{float r,g,b,a;};
ccColor3B ccc3(GLubyte r,GLubyte g,GLubyte b){return{r,g,b};}
ccColor4F ccc4f(float r,float g,float b,float a){return{r,g,b,a};}
struct CCNode{
std::vector<CCNode*>children;CCSize size;CCPoint position;float scale=1;std::string id;
static CCNode*create(){return new CCNode;}
void setContentSize(CCSize s){size=s;}CCSize getContentSize(){return size;}
void addChild(CCNode*n,int=0){children.push_back(n);}void setPosition(CCPoint p){position=p;}
void setScale(float v){scale=v;}void setID(std::string v){id=std::move(v);}
};
struct CCDrawNode:CCNode{
std::vector<CCPoint>polygon;ccColor4F fill;
static CCDrawNode*create(){return new CCDrawNode;}
bool drawPolygon(CCPoint*p,unsigned int n,ccColor4F const&c,float,ccColor4F const&){polygon.assign(p,p+n);fill=c;return true;}
};
struct CCLabelBMFont:CCNode{
std::string text;ccColor3B color{};
static CCLabelBMFont*create(char const*s,char const*){auto*l=new CCLabelBMFont;l->text=s;return l;}
void setColor(ccColor3B c){color=c;}
};
struct CCSprite:CCNode{std::string frame;
static CCSprite*createWithSpriteFrameName(char const*s){auto*p=new CCSprite;p->frame=s;return p;}};
}
namespace geode::utils {
template<class N>struct Result{N value{};bool ok=false;bool isOk()const{return ok;}N unwrap()const{return value;}};
template<class N>Result<N>numFromString(std::string_view s,int base=10){Result<N>r;auto c=std::from_chars(s.data(),s.data()+s.size(),r.value,base);r.ok=c.ec==std::errc{}&&c.ptr==s.data()+s.size();return r;}
}
namespace geode::prelude {using namespace cocos2d;}
std::string operator""_spr(char const*s,std::size_t n){return std::string(s,n);}
namespace corum::ranked {
struct RankedMapDetails{
MapMetadata local;MapDifficulty face;
static RankedMapDetails&get(){static RankedMapDetails d;return d;}
MapMetadata metadata(int){return local;}MapDifficulty difficulty(int){return face;}
};
namespace ui{
inline constexpr cocos2d::ccColor3B TextMuted{147,155,172},Text{230,235,244};
enum class FontWeight{SemiBold};
cocos2d::CCLabelBMFont*menuLabel(std::string s,float,cocos2d::CCPoint p,cocos2d::ccColor3B c,FontWeight){
auto*l=cocos2d::CCLabelBMFont::create(s.c_str(),"bigFont.fnt");l->setColor(c);l->setPosition(p);return l;}
void fitMenuLabelWidth(cocos2d::CCLabelBMFont*,float,float,float){}
}}
${badge}
int main(){using namespace corum::ranked;using namespace ui;using namespace cocos2d;
MapMetadata info;info.kind="NORMAL";info.stars=8;info.officialLevelId=10;info.durationSeconds=84;
auto*row=mapDifficultyBadge(42,info,135);
auto*shape=static_cast<CCDrawNode*>(row->children[0]);auto*number=static_cast<CCLabelBMFont*>(row->children[1]);
assert(shape->polygon.size()==48&&number->text=="8"&&number->color==(ccColor3B{255,255,255}));
assert(static_cast<CCSprite*>(row->children[2])->frame=="diffIcon_05_btn_001.png");
assert(mapLengthText(displayedMapMetadata(42,info).durationSeconds)=="1m 24s");
info.kind="DEMON";info.stars.reset();info.gddlTier=13;info.gddlColor="#dd5a5a";
info.officialLevelId.reset();RankedMapDetails::get().face=mapDifficulty(false,true,3,0);
row=mapDifficultyBadge(42,info,135);shape=static_cast<CCDrawNode*>(row->children[0]);number=static_cast<CCLabelBMFont*>(row->children[1]);
assert(shape->polygon.size()==4&&number->text=="13"&&number->color==(ccColor3B{255,255,255}));
assert(static_cast<CCLabelBMFont*>(row->children[3])->text=="Easy Demon");
assert(static_cast<CCSprite*>(row->children[2])->frame=="diffIcon_07_btn_001.png");
assert(std::abs(shape->fill.r-221/255.f)<.001f&&std::abs(shape->fill.g-90/255.f)<.001f);
info.gddlTier.reset();info.gddlColor="wrong";row=mapDifficultyBadge(42,info,135);
assert(static_cast<CCLabelBMFont*>(row->children[1])->text=="?");
auto&native=RankedMapDetails::get().local;native.kind="NORMAL";native.stars=6;native.durationSeconds=80;
info={};info.durationSeconds=95;auto fallback=displayedMapMetadata(42,info);
assert(fallback.stars==6&&fallback.durationSeconds==95);info={};assert(displayedMapMetadata(42,info).durationSeconds==80);
assert(mapLengthText(std::nullopt)=="?m ?s"&&mapLengthText(-1)=="?m ?s");
assert(mapLengthText(59.8)=="1m 0s");
}
`);
});
