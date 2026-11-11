import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function run(cpp){
    const directory=mkdtempSync(join(tmpdir(),'corum-tier-guide-'));
    try{writeFileSync(join(directory,'test.cpp'),cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(directory,'test.cpp'),'-o',join(directory,'test')],{stdio:'pipe'});
        execFileSync(join(directory,'test'),[],{stdio:'pipe'});
    }finally{rmSync(directory,{recursive:true,force:true});}
}
test('tier guide folds Solo divisions into five columns and keeps open-ended and missing tiers',()=>{
    run(`#include "domain/TierGuidePresentation.hpp"
#include <cassert>
int main(){using namespace corum::ranked;
 std::vector<TierGuideBand> solo{{"RED3",0,100},{"RED2",100,200},{"RED1",200,300},{"AQUA3",300,400},{"AQUA2",400,500},{"AQUA1",500,600,3,2},
  {"BRONZE3",600,700},{"BRONZE2",700,800},{"BRONZE1",800,900},{"SILVER3",900,1000},{"SILVER2",1000,1100},{"SILVER1",1100,1200,5,3},{"GOLD",1200,std::nullopt}};
 auto columns=tierGuideColumns(solo);
 assert(columns[0].base=="RED"&&columns[0].divisions==3&&columns[0].minimum==0&&columns[0].maximumExclusive==300);
 assert(columns[1].promotionGames==3&&columns[1].promotionWins==2&&columns[1].maximumExclusive==600);
 assert(columns[3].promotionGames==5&&columns[3].minimum==900&&columns[3].maximumExclusive==1200);
 assert(columns[4].base=="GOLD"&&columns[4].divisions==1&&!columns[4].maximumExclusive&&columns[4].present);
 // Bands may arrive in any order; a base with no band stays as a dash.
 auto partial=tierGuideColumns({{"SILVER1",1100,1200},{"SILVER3",900,1000},{"RED",0,300}});
 assert(partial[3].minimum==900&&partial[3].maximumExclusive==1200&&partial[3].divisions==2);
 assert(partial[0].present&&!partial[1].present&&!partial[2].present&&!partial[4].present);
}`);
});
test('tier guide popup follows the Q3 board: segment, refresh, X, columns, info panels',()=>{
    const source=read('src/ui/RankedTierGuidePopup.cpp');
    assert.match(source,/rankedPopupLayout\(\{Width,Height\}/);
    assert.match(source,/constexpr float Width=480, Height=280/);
    assert.match(source,/RankedTierGuidePopup::onMode/);assert.match(source,/RankedTierGuidePopup::onRefresh/);assert.match(source,/RankedTierGuidePopup::onClose/);
    assert.match(source,/tierGuideColumns\(bands\)/);assert.match(source,/view\.survival\.tierBands/);
    assert.match(source,/tier-guide-current/);assert.match(source,/tier-guide-demotion-detail/);
    const ui=JSON.parse(read('../ranked/translations/ui.json'));
    for(const key of ['tier-guide-current','tier-guide-start','tier-guide-ladder','tier-guide-promotion','tier-guide-demotion','tier-guide-promotion-detail','tier-guide-survival-promotion','tier-guide-demotion-detail'])assert.ok(ui[key],key);
});
