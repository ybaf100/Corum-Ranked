import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const between=(source,start,end)=>{const from=source.indexOf(start);assert.ok(from>=0,start);const to=source.indexOf(end,from+start.length);assert.ok(to>from,end);return source.slice(from,to);};
function run(cpp) {
    const directory=mkdtempSync(join(tmpdir(),'corum-badge-wear-'));
    try {
        writeFileSync(join(directory,'test.cpp'),cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(directory,'test.cpp'),'-o',join(directory,'test')],{stdio:'pipe'});
        execFileSync(join(directory,'test'),[],{stdio:'pipe'});
    } finally {rmSync(directory,{recursive:true,force:true});}
}

test('badge wear grows from UNRANKED to BRONZE and leaves SILVER and GOLD untouched',()=>{
    run(`#include <cassert>
#include "domain/RankTierPresentation.hpp"
int main(){using namespace corum::ranked;
 auto const order={"UNRANKED","RED","AQUA","BRONZE","SILVER"};
 RankBadgeWear previous{0,0,0,-1.0f,-1.0f,0.0f};
 for(auto tier:order){auto w=rankBadgeWear(tier);
  assert(w.r+w.g+w.b>=previous.r+previous.g+previous.b&&w.glow>=previous.glow&&w.bob>=previous.bob);
  assert(w.glow>=0.0f&&w.glow<=1.0f&&w.bob>=0.0f&&w.bob<=1.0f&&w.ring>0.0f&&w.ring<=1.0f);previous=w;}
 for(auto tier:{"SILVER","SILVER2","GOLD","MASTER","CHAMPION"}){auto w=rankBadgeWear(tier);
  assert(w.r==255&&w.g==255&&w.b==255&&w.glow==1.0f&&w.bob==1.0f&&w.ring==1.0f);}
 for(auto tier:{"RED","AQUA","BRONZE","UNRANKED"}){auto w=rankBadgeWear(tier);assert(w.r<255&&w.glow<1.0f);}
 // Divisions share their tier's wear; unknown names wear like UNRANKED.
 auto red=rankBadgeWear("RED"),red2=rankBadgeWear("RED2");assert(red.r==red2.r&&red.glow==red2.glow&&red.bob==red2.bob);
 auto unknown=rankBadgeWear(""),unranked=rankBadgeWear("UNRANKED");assert(unknown.r==unranked.r&&unknown.glow==0.0f&&unranked.bob==0.0f);
 // Only AQUA has the orbit ring, and only it is dimmed.
 assert(rankBadgeWear("AQUA").ring<1.0f&&rankBadgeWear("RED").ring==1.0f&&rankBadgeWear("BRONZE").ring==1.0f);
}`);
});

test('promotion parts and the finished trophy carry the same wear tint; transitions never touch colour',()=>{
    const badges=read('src/ui/RankBadgeAssets.cpp');
    const parts=between(badges,'std::vector<PartSpec> partsFor(','CCLayerColor* centeredRoot()');
    const assembly=between(badges,'RankBadgeAssembly createRankBadgeAssembly(','bool hasPersistentRankAmbient(');
    const spec=between(badges,'struct PartSpec {','std::vector<PartSpec> partsFor(');
    const header=read('src/ui/RankBadgeAssets.hpp');
    const types=between(header,'struct RankBadgePart {','enum class RankBadgeDisplay {');
    run(`#include "domain/RankTierPresentation.hpp"
${read('tests/helpers/ResultPresentationNativeDouble.hpp').replace('#pragma once','')}
namespace corum::ranked::ui{
${types}
${spec}
${parts}
std::string normalizeRankName(std::string rank){return rank;}
CCLayerColor* centeredRoot(){return CCLayerColor::create({255,255,255,255});}
CCSprite* rankResourceSprite(std::string const& file){auto* s=CCSprite::createWithSpriteFrameName(file.c_str());s->size={1024,1024};return s;}
void fitRankSprite(CCSprite* s,CCSize size){s->setScale(size.width/s->size.width);}
CCSprite* trophy(std::string const& rank,CCSize){auto* s=CCSprite::createWithSpriteFrameName(("trophy-"+rank).c_str());return s;}
${assembly}
}
int main(){using namespace corum::ranked;using namespace corum::ranked::ui;
 for(auto tier:{"UNRANKED","RED","AQUA","BRONZE","SILVER","GOLD"}){
  auto a=createRankBadgeAssembly(tier,118.0f);auto w=rankBadgeWear(tier);ccColor3B tint{w.r,w.g,w.b};
  std::size_t count=0;
  for(auto const* group:{&a.baseParts,&a.ornaments,&a.fragments})for(auto const& part:*group){
   assert(part.sprite->color==tint);assert(part.sprite->opacity==255);assert(part.assembledScale==part.sprite->getScale());++count;}
  assert(count==partsFor(tier).size());
  // The root stays neutral so parent fades and cascades behave as before.
  assert((a.root->color==ccColor3B{255,255,255})&&a.root->opacity==255);
 }
}`);
    const visuals=read('src/ui/RankedVisuals.hpp');
    const trophy=between(visuals,'inline CCSprite* trophy(','} // namespace corum::ranked::ui');
    assert.match(trophy,/auto const wear = rankBadgeWear\(tier\);\s*result->setColor\(\{wear\.r, wear\.g, wear\.b\}\);/);
    // Rank transitions animate opacity, scale, position and rotation only.
    const transition=read('src/ui/RankTransitionLayer.cpp');
    assert.doesNotMatch(transition,/CCTint(?:To|By)|part\.sprite->setColor|\.root->setColor|finalBadge->setColor/);
});

test('worn backdrops dim the glow and the AQUA ring; UNRANKED has no glow or bob, GOLD is unchanged',()=>{
    const badges=read('src/ui/RankBadgeAssets.cpp');
    const backdrop=between(badges,'CCNode* createRankBadgeBackdrop(','} // namespace corum::ranked::ui');
    const display=read('src/ui/RankBadgeAssets.hpp').match(/enum class RankBadgeDisplay\s*\{[^}]+\};/)[0];
    run(`#include "domain/RankTierPresentation.hpp"
#include <map>
#include <array>
${read('tests/helpers/ResultPresentationNativeDouble.hpp').replace('#pragma once','')}
struct CCRepeatForever{static CCAction* create(CCAction* a){auto* n=action("repeat",a->duration);n->children={a};return n;}};
struct CCEaseSineInOut{static CCAction* create(CCAction* a){auto* n=action("ease",a->duration);n->children={a};return n;}};
namespace corum::ranked::ui{
${display}
struct RankTierProfile{ccColor3B color;};
RankTierProfile rankTierProfile(std::string const&){return {{10,20,30}};}
std::string normalizeRankName(std::string rank){return rank;}
CCLayerColor* centeredRoot(){return CCLayerColor::create({255,255,255,255});}
std::map<CCSprite*,std::array<int,2>> pulses;
CCSprite* ambientSprite(CCNode* root,std::string const& file,CCSize size,int z,GLubyte opacity,ccColor3B const* tint=nullptr){
 auto* n=CCSprite::createWithSpriteFrameName(file.c_str());n->setContentSize(size);n->setOpacity(opacity);if(tint)n->setColor(*tint);root->addChild(n,z);return n;}
void pulseOpacity(CCSprite* n,GLubyte low,GLubyte high,float){if(n)pulses[n]={low,high};}
CCNode* createRankAmbientFx(std::string const&,CCSize,RankBadgeDisplay){return CCNode::create();}
CCSprite* trophy(std::string const& rank,CCSize){return CCSprite::createWithSpriteFrameName(("trophy-"+rank).c_str());}
${backdrop}
}
CCSprite* find(CCNode* n,std::string const& name){for(auto* c:n->children)if(c->id==name)return static_cast<CCSprite*>(c);return nullptr;}
float tilt(CCNode* motion){if(motion->actions.empty())return 0;auto* seq=motion->actions[0]->children[0];auto* spawn=seq->children[0];return spawn->children[1]->children[0]->value;}
int main(){using namespace corum::ranked::ui;
 auto glow=[](char const* tier,RankBadgeDisplay d){return find(createRankBadgeBackdrop(tier,{40,40},d),"fx-rank-radial-glow-white.png");};
 auto* gold=glow("GOLD",RankBadgeDisplay::Large);assert(gold&&gold->opacity==96&&(pulses[gold]==std::array<int,2>{42,86}));
 auto* silver=glow("SILVER",RankBadgeDisplay::Compact);assert(silver&&silver->opacity==28&&(pulses[silver]==std::array<int,2>{20,42}));
 auto* red=glow("RED",RankBadgeDisplay::Large);assert(red&&red->opacity==30&&(pulses[red]==std::array<int,2>{20,41}));
 auto* bronze=glow("BRONZE",RankBadgeDisplay::Large);assert(bronze&&bronze->opacity==48&&bronze->opacity<62);
 assert(glow("UNRANKED",RankBadgeDisplay::Large)==nullptr);
 auto* ring=find(createRankBadgeBackdrop("AQUA",{40,40},RankBadgeDisplay::Large),"fx-rank-broken-ring-white.png");assert(ring&&ring->opacity==40&&!ring->actions.empty());
 // Idle bob: none for UNRANKED, half for RED, 3/4 for AQUA, full for SILVER/GOLD; Compact never bobs.
 auto motion=[](char const* tier,RankBadgeDisplay d){return createAnimatedRankBadge(tier,{40,40},d)->children[0];};
 assert(motion("UNRANKED",RankBadgeDisplay::Hero)->actions.empty());
 assert(std::abs(tilt(motion("RED",RankBadgeDisplay::Hero))-0.325f)<1e-4f);
 assert(std::abs(tilt(motion("AQUA",RankBadgeDisplay::Large))-0.4875f)<1e-4f);
 assert(std::abs(tilt(motion("SILVER",RankBadgeDisplay::Hero))-0.65f)<1e-4f);
 assert(std::abs(tilt(motion("GOLD",RankBadgeDisplay::Large))-0.65f)<1e-4f);
 assert(motion("GOLD",RankBadgeDisplay::Compact)->actions.empty());
}`);
});
