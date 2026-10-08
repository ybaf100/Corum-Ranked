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
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace gd {using string=std::string;}
struct GJGameLevel {int m_levelID=0,m_demon=0,m_demonDifficulty=0,average=0;bool m_autoLevel=false;
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
GJGameLevel easy;easy.m_levelID=11;easy.average=1;GJGameLevel demon;demon.m_levelID=12;demon.m_demon=1;demon.m_demonDifficulty=3;
GJGameLevel unrelated;unrelated.m_levelID=88;unrelated.average=5;
manager.result.values={&easy,&demon,&unrelated};manager.onGetOnlineLevelsCompleted("ok",keyA);
assert(manager.m_levelManagerDelegate==&foreign&&foreign.callbacks==0);
assert(details.difficulty(11).label=="Easy"&&details.difficulty(12).label=="Easy Demon");
assert(details.difficulty(88).label=="N/A");assert(!details.requestGeneration(keyA.c_str()));
auto revision=details.revision();details.remember(&easy);assert(details.revision()==revision);
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
    assert.match(screen,/if \(!corum::ranked::ui::rankedJukeboxLoaded\(\)\) return/);
    assert.match(screen,/m_jukebox\.reset\(\)/);
    assert.match(read('src/RankedRoundPreparation.cpp'),/roundModalOpen\(origin\)/);
});
