import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
test('real native presentation conceals the local catalog, reveals after unlock, and derives mode prefixes from scope',()=>{
 const dir=mkdtempSync(join(tmpdir(),'achievement-overhaul-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`
#include "domain/AchievementPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
struct Card{bool concealed=true;std::string scope="SURVIVAL",id="survival_secret_1",nameKo="Hidden",nameEn="Hidden",descriptionKo="safe hint",descriptionEn="safe hint";};
int main(){Card c;
 assert(achievementCardName(c,false)=="HIDDEN");assert(achievementCardName(c,true)=="Hidden");
 assert(achievementCardDescription(c,false)=="Available in a normal match");
 assert(achievementCardDescription(c,true)=="일반 매치에서 달성 가능");
 c.id="survival_secret_2";assert(achievementCardDescription(c,false)=="Available in Death Match");
 c.concealed=false;assert(achievementCardName(c,false)=="Titans Clash");
 assert(achievementCardDescription(c,false)=="Tie an ongoing Survival Death Match.");
 c.id="survival_secret_1";assert(achievementCardName(c,true)=="아직 끝나지 않았다");
 c.id="last_attempt_match_win";c.scope="SOLO_RANKED";assert(achievementCardName(c,true)=="아직 끝나지 않았다");
 assert(achievementScopedName("SURVIVAL","[Survival] Survivor",false)=="[Survival] Survivor");
 assert(achievementScopedName("SHARED","Pioneer",false)=="[Shared] Pioneer");
}`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')]);
  execFileSync(join(dir,'test'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('native API parsing and all achievement title surfaces keep scope and concealment',()=>{
 const runtime=readFileSync(join(root,'src/RankedRuntime.cpp'),'utf8');
 assert.match(runtime,/card\.concealed = value\["concealed"\]/);
 assert.match(runtime,/title\.scope = value\["scope"\]/);assert.match(runtime,/notice\.scope = value\["scope"\]/);
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8');
 assert.match(popup,/achievementScopedName\(title.scope/);assert.doesNotMatch(popup,/achievementScopedName\(notice.scope/);
 assert.match(popup,/achievementCardName\(card/);assert.match(popup,/achievementCardDescription\(card/);
 assert.doesNotMatch(readFileSync(join(root,'src/ui/AchievementUnlockToast.cpp'),'utf8'),/achievementScopedName\(value\["scope"\]/);
});
