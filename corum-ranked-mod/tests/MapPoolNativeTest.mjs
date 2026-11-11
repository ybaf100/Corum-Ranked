import test from 'node:test';
import assert from 'node:assert/strict';
import {writeFileSync,mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
function run(source){const dir=mkdtempSync(join(tmpdir(),'map-pool-'));try{
 writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('map pool: difficulty is the default order, filters by pool, rates without data sort last',()=>run(`
#include "domain/MapPoolPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
MapPoolRow row(std::string id,int rank,int pool,int rounds,std::optional<double> share,std::optional<double> draw){
 MapPoolRow r;r.canonicalLevelId=id;r.title=id;r.difficultyRank=rank;r.pool=pool;r.rounds=rounds;r.share=share;r.drawRate=draw;return r;}
int main(){
 std::vector<MapPoolRow> rows{row("insane",5,3,40,40.0,10.0),row("hard",3,1,10,10.0,std::nullopt),row("demon",6,4,50,50.0,0.0),row("hard-b",3,2,0,0.0,std::nullopt)};
 auto order=mapPoolOrder(rows,0,MapPoolSort::Difficulty);
 assert((order==std::vector<std::size_t>{1,3,0,2}));
 order=mapPoolOrder(rows,0,MapPoolSort::Share);assert((order==std::vector<std::size_t>{2,0,1,3}));
 // No draw data (nullopt) sorts below 0%, ties fall back to difficulty.
 order=mapPoolOrder(rows,0,MapPoolSort::Draw);assert((order==std::vector<std::size_t>{0,2,1,3}));
 order=mapPoolOrder(rows,3,MapPoolSort::Difficulty);assert((order==std::vector<std::size_t>{0}));
 assert(mapPoolOrder(rows,5,MapPoolSort::Rounds).empty());
 assert(kMapPoolSorts[0]==MapPoolSort::Difficulty);
}
`));

test('map pool: percent, number, clock and death peak formatting',()=>run(`
#include "domain/MapPoolPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 assert(mapPoolPercent(12.4)=="12.4%");assert(mapPoolPercent(50.0)=="50%");assert(mapPoolPercent(0.0)=="0%");
 assert(mapPoolPercent(std::nullopt)=="-");assert(mapPoolPercent(33.349)=="33.3%");
 assert(mapPoolDecimal(7.4)=="7.4");assert(mapPoolDecimal(2.0)=="2");
 assert(mapPoolNumber(1284)=="1,284");assert(mapPoolNumber(999)=="999");assert(mapPoolNumber(1234567)=="1,234,567");
 assert(mapPoolClock(84.0)=="1:24");assert(mapPoolClock(372.0)=="6:12");assert(mapPoolClock(5.0)=="0:05");assert(mapPoolClock(std::nullopt)=="-");
 assert(mapPoolDeathPeak({6,9,11,14,19,13,10,8,6,4})==4);assert(mapPoolDeathPeak(std::vector<double>(10,0))==-1);
 assert(mapPoolUpdatedMinutes(59)==0);assert(mapPoolUpdatedMinutes(185)==3);
}
`));

test('map pool is its own lobby page; map info is a popup; VIEW LEVEL reuses the history flow',()=>{
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8');
 const view=readFileSync(join(root,'src/ui/RankedMapPoolView.cpp'),'utf8');
 const info=readFileSync(join(root,'src/ui/RankedMapInfoPopup.cpp'),'utf8');
 const visuals=readFileSync(join(root,'src/ui/MapPoolVisuals.hpp'),'utf8');
 const runtime=readFileSync(join(root,'src/RankedRuntime.cpp'),'utf8');
 // Mirrors the UI editor (+71) on the left side of the start button.
 assert.match(popup,/menu_selector\(CorumRankedLayer::onMapPool\), "ranked-map-pool-button"/);
 // A separate screen like match history, rendered once (it redraws itself).
 assert.match(popup,/m_page = Page::MapPool;/);assert.match(popup,/if \(m_page == Page::MapPool\) return "map-pool";/);
 assert.match(popup,/RankedMapPoolView::create\(size\)/);
 // A found match leaves the page; its popups close with the lobby dialogs.
 assert.match(popup,/leaveMapPoolOnMatch\(runtime\.view\(\)\);/);assert.match(popup,/if \(m_page != Page::MapPool \|\| view\.stage != RuntimeStage::Matched\) return;/);
 assert.equal((popup.match(/if \(m_mapPoolView\) if \(auto view = m_mapPoolView->lock\(\)\) view->closePopups\(\);/g)||[]).length,2);
 assert.match(view,/void RankedMapPoolView::cleanup\(\)[\s\S]*closePopups\(\);/);
 // The lobby button fades with the panel: no CCDrawNode inside it.
 const button=popup.slice(popup.indexOf('void addMapPoolButton'),popup.indexOf('void onMapPool'));
 assert.doesNotMatch(button,/CCDrawNode::create/);
 // Page content uses color layers, which follow lobby fades.
 assert.doesNotMatch(view.slice(view.indexOf('RankedMapPoolView* RankedMapPoolView::create')),/CCDrawNode::create/);
 assert.match(view,/RankedMapInfoPopup::create\(m_rows\[index\]\)/);
 assert.match(view,/showHistoryLevel\(levelNumber\(m_rows\[index\]\.levelId\)\)/);
 // The info popup closes before a native level scene is pushed over it.
 assert.match(info,/if \(canOpenHistoryLevel\(\) && level > 0\) dismiss\(\);\s*showHistoryLevel\(level\);/);
 assert.match(info,/mapPoolUrl\(id\)/);assert.match(visuals,/Authorization", "Bearer " \+ token/);
 assert.match(runtime,/\/api\/ranked\/solo\/map-pool/);
});

test('pool tabs scroll sideways in a 5.5-tab strip; order is a picker list titled ORDER: <current>',()=>{
 const view=readFileSync(join(root,'src/ui/RankedMapPoolView.cpp'),'utf8');
 const filter=readFileSync(join(root,'src/ui/RankedHistoryFilterPopup.hpp'),'utf8');
 assert.match(view,/mapPoolTabStripWidth\(kTabWidth, kTabGap\)/);assert.match(view,/CCClippingNode::create\(mask\)/);
 assert.match(view,/mapPoolTabScroll\(m_startOffset \+ dx, m_contentWidth, m_width\)/);
 assert.match(view,/RankedHistoryFilterPopup::create\(std::move\(choices\), sortKey\(m_sort\)/);
 assert.match(view,/"map-pool-order-button", "ORDER: \{\}"/);
 assert.match(filter,/heading\.empty\(\)\?i18n::text\("match-history","MATCH HISTORY"\):heading/);
 const ui=JSON.parse(readFileSync(join(root,'../ranked/translations/ui.json'),'utf8'));
 assert.equal(ui['map-pool-order-button'].ko,'순서 : {}');
 run(`
#include "domain/MapPoolPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 assert(kMapPoolTabs==7);assert(mapPoolTabStripWidth(46,4)==5.5f*46+5*4);
 auto const content=7*46.0f+6*4,view=mapPoolTabStripWidth(46,4);
 assert(mapPoolTabScroll(10,content,view)==0);assert(mapPoolTabScroll(-1000,content,view)==view-content);assert(mapPoolTabScroll(-20,content,view)==-20);
 assert(mapPoolTabScroll(-50,100,200)==0);
}
`);
});

test('GD difficulty label names the face; the sheet rating only breaks ties',()=>{
 const visuals=readFileSync(join(root,'src/ui/MapPoolVisuals.hpp'),'utf8');
 assert.match(visuals,/row\.gdDifficulty\.empty\(\) \? std::nullopt : recordedMapDifficulty\(row\.gdDifficulty\)/);
 run(`
#include "domain/MapDifficulty.hpp"
#include "domain/MapPoolPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 assert(recordedMapDifficulty("Extreme Demon")->label=="Extreme Demon");
 assert(!recordedMapDifficulty("13"));
 MapPoolRow a,b;a.canonicalLevelId="a";b.canonicalLevelId="b";a.difficultyRank=b.difficultyRank=8;a.rating=14;b.rating=11;
 std::vector<MapPoolRow> rows{a,b};assert((mapPoolOrder(rows,0,MapPoolSort::Difficulty)==std::vector<std::size_t>{1,0}));
}
`);
});

test('map pool Korean copy only uses glyphs the bundled fonts already carry',()=>{
 const manifest=JSON.parse(readFileSync(join(root,'mod.json'),'utf8'));
 const ui=JSON.parse(readFileSync(join(root,'../ranked/translations/ui.json'),'utf8'));
 const present=new Set();
 for(const range of manifest.resources.fonts.CorumSansBold.charset.split(',')){const [a,b=a]=range.split('-').map(Number);for(let v=a;v<=b;v++)present.add(v);}
 const keys=Object.keys(ui).filter(k=>k.startsWith('map-pool')||k.startsWith('map-info'));
 assert.ok(keys.length>=50);
 const missing=[...new Set(keys.flatMap(k=>[...ui[k].ko]))].filter(c=>c!=='\n'&&!present.has(c.codePointAt(0)));
 assert.deepEqual(missing,[]);
});

test('difficulty order groups by Pool first; ascending/descending flips the order',()=>run(`
#include "domain/MapPoolPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
MapPoolRow row(std::string id,int rank,int pool,std::optional<double> share){MapPoolRow r;r.canonicalLevelId=id;r.title=id;r.difficultyRank=rank;r.pool=pool;r.share=share;return r;}
int main(){
 // A Pool 1 demon still comes before a Pool 2 easy level.
 std::vector<MapPoolRow> rows{row("p2-easy",1,2,5.0),row("p1-demon",8,1,5.0),row("p1-hard",3,1,9.0),row("p2-insane",5,2,std::nullopt)};
 assert((mapPoolOrder(rows,0,MapPoolSort::Difficulty,true)==std::vector<std::size_t>{2,1,0,3}));
 assert((mapPoolOrder(rows,0,MapPoolSort::Difficulty,false)==std::vector<std::size_t>{3,0,1,2}));
 assert(mapPoolDefaultAscending(MapPoolSort::Difficulty)&&!mapPoolDefaultAscending(MapPoolSort::Share));
 // Values flip; ties stay easiest first in both directions; no data stays below 0.
 assert((mapPoolOrder(rows,0,MapPoolSort::Share,false)==std::vector<std::size_t>{2,0,1,3}));
 assert((mapPoolOrder(rows,0,MapPoolSort::Share,true)==std::vector<std::size_t>{3,0,1,2}));
 // Native GD faces map onto the server's difficulty order.
 assert(mapPoolRankFromNative(-1)==0&&mapPoolRankFromNative(5)==5&&mapPoolRankFromNative(7)==6&&mapPoolRankFromNative(8)==7);
 assert(mapPoolRankFromNative(6)==8&&mapPoolRankFromNative(9)==9&&mapPoolRankFromNative(10)==10&&mapPoolRankFromNative(0)==99);
 // UTF-8 safe title cut.
 assert(mapPoolUtf8Prefix("Bloodbath",5)==5);assert(mapPoolUtf8Prefix("맵풀",1)==3);assert(mapPoolUtf8Prefix("ab",9)==2);
}
`));

test('map titles get a full-width line; unresolved levels are looked up from GD; direction toggle',()=>{
 const view=readFileSync(join(root,'src/ui/RankedMapPoolView.cpp'),'utf8');
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8');
 assert.match(view,/titleWidth = std::max\(40\.0f, barLeft - 8 - title\)/);
 assert.match(view,/fitTitle\(title, map\.title, columns\.titleWidth, 0\.29f\)/);
 assert.match(view,/badge->setPosition\(\{columns\.title, 2\}\)/);
 assert.match(view,/mapPoolOrder\(m_rows, m_pool, m_sort, m_ascending\)/);
 assert.match(view,/self->m_ascending = mapPoolDefaultAscending\(sort\)/);
 assert.match(view,/RankedMapDetails::get\(\)\.difficulty\(levelNumber\(row\.levelId\)\)/);
 assert.match(view,/if \(applyNativeDifficulties\(\)\) rebuildRows\(true\)/);
 assert.match(popup,/for \(auto id : pool->unresolvedLevels\(\)\) ids\.push_back\(id\)/);
 const ui=JSON.parse(readFileSync(join(root,'../ranked/translations/ui.json'),'utf8'));
 assert.equal(ui['map-pool-ascending'].ko,'오름차순');assert.equal(ui['map-pool-descending'].ko,'내림차순');
});
