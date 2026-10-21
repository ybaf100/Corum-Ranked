import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const read=p=>readFileSync(join(root,p),'utf8');
function run(source){const dir=mkdtempSync(join(tmpdir(),'achievement-followup-'));try{writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('locked one-shots retain rarity, progressive cards use their own equipped stage, and V surfaces are platinum',()=>run(`
#include "domain/AchievementStyle.hpp"
#include <cassert>
#include <optional>
#include <string>
using namespace corum::ranked;
struct Card{std::string id="wins",type="PROGRESSIVE",rarity;int currentLevel=5;bool unlocked=false;};
struct Title{bool active=false;std::string achievementId;std::optional<int> level;};
int main(){Card c;Title title;assert(achievementCardTier(c,title)==5);
 title={true,"wins",2};assert(achievementCardTier(c,title)==2);
 title.achievementId="other";assert(achievementCardTier(c,title)==5);
 title.achievementId="wins";title.level=6;assert(achievementCardTier(c,title)==5);
 title.level=0;assert(achievementCardTier(c,title)==5);title.level.reset();assert(achievementCardTier(c,title)==5);
 c.currentLevel=0;title.level=2;assert(achievementCardTier(c,title)==0);
 c.type="ONE_SHOT";for(auto const* rarity:{"COMMON","RARE","EPIC","LEGENDARY","MYTHIC"}){c.rarity=rarity;assert(achievementCardTier(c,title)==achievementRarityTier(rarity));}
 auto platinum=achievementTierColor(5);assert(platinum.r==229&&platinum.g==228&&platinum.b==226);
}`));
test('Claim All UI aggregates every category and does not display a completion notification',()=>{
 const popup=read('src/RankedPopup.cpp');
 assert.match(popup,/achievementPending\(view.achievements.cards,"ALL"\)/);
 assert.match(popup,/claimAllAchievements\("ALL"\)/);
 assert.doesNotMatch(popup,/syncAchievementClaimNotice|claim-all-complete|CLAIM ALL COMPLETE/);
 const effects=read('src/ui/AchievementEffects.cpp');
 assert.doesNotMatch(effects.slice(effects.indexOf('void styleAchievementFill')),/attach\(/);
 const cards=popup.slice(popup.indexOf('void renderAchievements('),popup.indexOf('void renderHistoryList('));
 assert.doesNotMatch(cards,/styleAchievementText\(progressLabel|styleAchievementTree\(/);
});
test('gold podium halo fades in after the first-place drop without replaying on settlement refresh',()=>run(`
#include "domain/SurvivalPodiumMotion.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){double previous=0;for(int i=0;i<=600;++i){auto f=survivalPodiumFrame(1,i/100.);assert(f.glowOpacity>=previous&&f.glowOpacity<=1);if(i<385)assert(f.glowOpacity==0);previous=f.glowOpacity;}assert(previous==1);
 assert(survivalPodiumFrame(2,8).glowOpacity==0&&survivalPodiumFrame(3,8).glowOpacity==0);
}`));
