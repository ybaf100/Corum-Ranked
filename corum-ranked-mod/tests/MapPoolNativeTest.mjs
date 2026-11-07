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

test('map pool button sits on the Solo start card and opens the popup; VIEW LEVEL reuses the history flow',()=>{
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8');
 const pool=readFileSync(join(root,'src/ui/RankedMapPoolPopup.cpp'),'utf8');
 const runtime=readFileSync(join(root,'src/RankedRuntime.cpp'),'utf8');
 // Mirrors the UI editor (+71) on the left side of the start button.
 assert.match(popup,/addMapPoolButton\(\{rightCenter\.x - 71\.0f, rightCenter\.y \+ 18\.0f\}\)/);
 assert.match(popup,/RankedMapPoolPopup::create\(\)/);
 // Closed with the other lobby dialogs before entering a level or closing settings.
 assert.equal((popup.match(/if \(m_mapPool\) if \(auto popup = m_mapPool->lock\(\)\) popup->dismiss\(\);/g)||[]).length,2);
 assert.match(popup,/!modal\(m_mapPool\)/);
 // The lobby button fades with the panel: no CCDrawNode inside it.
 const button=popup.slice(popup.indexOf('void addMapPoolButton'),popup.indexOf('void onMapPool'));
 assert.doesNotMatch(button,/CCDrawNode::create/);
 assert.match(pool,/showHistoryLevel\(levelNumber\(m_rows\[index\]\.levelId\)\)/);
 assert.match(pool,/mapPoolUrl\(id\)/);assert.match(pool,/Authorization", "Bearer " \+ token/);
 assert.match(runtime,/\/api\/ranked\/solo\/map-pool/);
 // The open info view follows its map by ID across a refresh.
 assert.match(pool,/row\.canonicalLevelId == self->m_detailId/);
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
