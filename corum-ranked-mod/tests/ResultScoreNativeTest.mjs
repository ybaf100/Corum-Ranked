import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name) {
    const at=source.indexOf(name); assert(at>=0,name);
    // Skip default arguments (e.g. afterReveal = {}) before the method body.
    let params=source.indexOf('(',at)+1,parens=1;
    while(parens) { if(source[params]==='(')++parens; if(source[params]===')')--parens; ++params; }
    const open=source.indexOf('{',params); let end=open+1,depth=1;
    while(depth&&end<source.length) { if(source[end]==='{')++depth; if(source[end]==='}')--depth; ++end; }
    assert.equal(depth,0,name); return source.slice(at,end);
}
const strip=s=>s.replace(/^#include .*?$|^#pragma once$/gm,'');
function run(cpp) {
    const dir=mkdtempSync(join(tmpdir(),'corum-result-native-'));
    try {
        const source=join(dir,'result.cpp'),binary=join(dir,'result');
        writeFileSync(source,cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),source,'-o',binary],{stdio:'pipe'});
        execFileSync(binary,[],{stdio:'pipe'});
    } finally { rmSync(dir,{recursive:true,force:true}); }
}

test('actual blur translation unit compiles with the Windows RPC small macro',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-blur-windows-'));
    try {
        const source=join(dir,'blur.cpp');
        // Renderer boundaries are declarations only. The actual Corum source
        // must compile while the rpcndr.h macro seen in the Windows CI is active.
        writeFileSync(source,`
#include <cmath>
namespace cocos2d {
struct CCSize { float width, height; CCSize(float w,float h):width(w),height(h){} };
struct CCPoint { float x,y; };
struct CCRect {};
struct CCTexture2D { unsigned int getPixelsWide(); unsigned int getPixelsHigh(); void setAntiAliasTexParameters(); };
struct CCGLProgram {
    bool initWithVertexShaderByteArray(char const*,char const*);
    void addAttribute(char const*,unsigned int);
    bool link(); void updateUniforms(); void use(); void autorelease();
    int getUniformLocationForName(char const*);
    void setUniformLocationWith2f(int,float,float);
};
struct CCNode { void visit(); };
struct CCSprite : CCNode {
    static CCSprite* createWithTexture(CCTexture2D*,CCRect const&);
    CCTexture2D* getTexture(); CCRect const& getTextureRect();
    bool isFlipY(); void setFlipY(bool); void setShaderProgram(CCGLProgram*);
    void setScaleX(float); void setScaleY(float); void setPosition(CCPoint const&);
};
struct CCRenderTexture {
    static CCRenderTexture* create(int,int); CCSprite* getSprite();
    void beginWithClear(float,float,float,float); void end();
};
inline constexpr char kCCAttributeNamePosition[]="a_position",kCCAttributeNameColor[]="a_color",kCCAttributeNameTexCoord[]="a_texCoord";
inline constexpr unsigned int kCCVertexAttrib_Position=0,kCCVertexAttrib_Color=1,kCCVertexAttrib_TexCoords=2;
}
namespace geode::prelude { using namespace cocos2d; }
#define small char
${strip(read('src/ui/ResultBackdropBlur.cpp'))}
`);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-fsyntax-only',source],{stdio:'pipe'});
    } finally { rmSync(dir,{recursive:true,force:true}); }
});

test('actual blur capture downsamples once and uses adjacent bilinear texels for both axes',()=>{
    run(`
#include <cassert>
#include <cmath>
#include <vector>
namespace cocos2d {
struct CCSize { float width,height;CCSize(float w,float h):width(w),height(h){} };
struct CCPoint { float x=0,y=0; };
struct CCRect {};
struct CCTexture2D {
    unsigned int width,height;bool linear=false;
    unsigned int getPixelsWide(){return width;}unsigned int getPixelsHigh(){return height;}
    void setAntiAliasTexParameters(){linear=true;}
};
struct CCGLProgram {
    CCPoint step;bool initWithVertexShaderByteArray(char const*,char const*){return true;}
    void addAttribute(char const*,unsigned int){}bool link(){return true;}
    void updateUniforms(){}void use(){}void autorelease(){}
    int getUniformLocationForName(char const*){return 1;}
    void setUniformLocationWith2f(int,float x,float y){step={x,y};}
};
struct CCNode { int visits=0;virtual ~CCNode()=default;virtual void visit(){++visits;} };
struct CCSprite:CCNode {
    CCTexture2D* texture;CCGLProgram* shader=nullptr;CCRect rect;bool flip=false;
    float scaleX=1,scaleY=1;CCPoint position;
    static CCSprite* createWithTexture(CCTexture2D* texture,CCRect const&){auto* s=new CCSprite;s->texture=texture;return s;}
    CCTexture2D* getTexture(){return texture;}CCRect const& getTextureRect(){return rect;}
    bool isFlipY(){return flip;}void setFlipY(bool value){flip=value;}
    void setShaderProgram(CCGLProgram* program){shader=program;}
    void setScaleX(float value){scaleX=value;}void setScaleY(float value){scaleY=value;}
    void setPosition(CCPoint value){position=value;}void visit()override;
};
struct CCRenderTexture {
    CCSprite* sprite;inline static std::vector<CCRenderTexture*> textures;
    inline static std::vector<CCSprite*> passes;
    static CCRenderTexture* create(int w,int h){
        auto* target=new CCRenderTexture;target->sprite=new CCSprite;
        // Simulate backing pixels differing from logical dimensions, as on retina.
        target->sprite->texture=new CCTexture2D{static_cast<unsigned int>(w*2),static_cast<unsigned int>(h*2)};
        target->sprite->flip=true;textures.push_back(target);return target;
    }
    CCSprite* getSprite(){return sprite;}void beginWithClear(float,float,float,float){}void end(){}
};
void CCSprite::visit(){CCNode::visit();CCRenderTexture::passes.push_back(this);}
inline constexpr char kCCAttributeNamePosition[]="a_position",kCCAttributeNameColor[]="a_color",kCCAttributeNameTexCoord[]="a_texCoord";
inline constexpr unsigned int kCCVertexAttrib_Position=0,kCCVertexAttrib_Color=1,kCCVertexAttrib_TexCoords=2;
}
namespace geode::prelude {using namespace cocos2d;}
#define small char
${strip(read('src/ui/ResultBackdropBlur.cpp'))}
int main(){
    using namespace cocos2d;
    CCNode screen;auto* result=corum::ranked::ui::captureBlurredResult(&screen,{568,320});
    auto const& targets=CCRenderTexture::textures;auto const& passes=CCRenderTexture::passes;
    assert(result&&screen.visits==1&&targets.size()==4&&passes.size()==3);
    assert(!passes[0]->shader&&passes[0]->texture==targets[0]->sprite->texture);
    assert(passes[0]->scaleX==0.5f&&passes[0]->scaleY==0.5f);
    assert(passes[1]->texture==targets[1]->sprite->texture&&passes[2]->texture==targets[2]->sprite->texture);
    assert(passes[1]->shader->step.x==1.0f/passes[1]->texture->width&&passes[1]->shader->step.y==0);
    assert(passes[2]->shader->step.y==1.0f/passes[2]->texture->height&&passes[2]->shader->step.x==0);
    assert(passes[1]->texture->width==passes[2]->texture->width&&passes[1]->texture->height==passes[2]->texture->height);
    for(auto* pass:passes)assert(pass->texture->linear&&pass->flip&&pass->position.x==142&&pass->position.y==80);
    assert(result->texture==targets[3]->sprite->texture&&result->scaleX==2&&result->scaleY==2);
    assert(result->position.x==284&&result->position.y==160&&result->flip);
}
`);
});

const model=`
#include "${join(root,'src/domain/ResultScorePresentation.hpp')}"
#include <cassert>
using namespace corum::ranked;
ScoreAnimationInput ordinary() {
    ScoreAnimationInput i; i.matchId="one"; i.beforeTier=i.afterTier="RED"; i.before=100; i.after=125; i.won=true;
    i.bands={{"RED",0},{"AQUA",300},{"BRONZE",600},{"SILVER",900},{"GOLD",1200}}; return i;
}
`;

test('actual score model uses 2s/4s, exact LP, cutoff crossings and a fixed GOLD result target',()=>{
    run(model+`
int main() {
    auto i=ordinary(); assert(validScoreBands(i.bands)&&i.duration()==2);
    auto s=sampleScoreProgress(i,1); assert(s.score==113&&s.target==300&&s.minimum==0&&!s.complete);
    assert(sampleScoreProgress(i,-1).score==100&&sampleScoreProgress(i,99).score==125);
    i.before=i.after=0; s=sampleScoreProgress(i,2); assert(s.score==0&&s.fill==0&&s.complete);
    i.before=299; i.after=350; i.afterTier="AQUA"; i.promoted=true;
    assert(i.duration()==4&&i.after-i.before==51);
    assert(sampleScoreProgress(i,0).tier=="RED");
    s=sampleScoreProgress(i,2); assert(s.tier=="AQUA"&&s.minimum==300&&s.target==600&&s.score==325);
    i=ordinary(); i.beforeTier="BRONZE"; i.afterTier="AQUA"; i.before=600; i.after=550; i.demoted=true;
    assert(i.duration()==4&&sampleScoreProgress(i,0).tier=="BRONZE");
    s=sampleScoreProgress(i,2); assert(s.tier=="AQUA"&&s.score==575&&s.fill>0.9);
    i=ordinary(); i.beforeTier=i.afterTier="GOLD"; i.before=1325; i.after=1300;
    i.standingBefore.rank=1; i.standingAfter.rank=2;
    i.standingAfter.target=StandingTargetView{42,"target",false,1,1325};
    s=sampleScoreProgress(i,0); assert(!s.goldLeader&&s.fill==1&&s.standing.target->accountId==42);
    s=sampleScoreProgress(i,1); assert(s.fill<1&&s.fill>0.8&&s.standing.target->accountId==42);
    s=sampleScoreProgress(i,2); assert(s.target==1325&&s.minimum==1200&&s.fill==0.8);
    i.standingAfter.rank=1; i.standingAfter.target.reset();
    for(auto t:{0.0,1.0,2.0}) { s=sampleScoreProgress(i,t); assert(s.goldLeader&&s.fill==1); }
    i.bands[2].minimum=300; assert(!validScoreBands(i.bands));
    i.bands.pop_back(); assert(!validScoreBands(i.bands));
    ResultPresentationGate gate; assert(!gate.begin("")); assert(gate.begin("one"));
    assert(!gate.begin("one")); assert(gate.begin("two")); gate.reset(); assert(gate.begin("two"));
}
`);
});

const scoreNative=`
#include "${join(root,'tests/helpers/ResultPresentationNativeDouble.hpp')}"
#include <cctype>
${model}
namespace cocos2d { using ::CCSize; using ::ccColor3B; }
${strip(read('src/ui/RankBadgeAssets.hpp'))}
namespace corum::ranked::ui {
inline constexpr ccColor3B Text{230,235,244},TextMuted{147,155,172},Gold{255,207,74},SurfaceRaised{16,28,45};
enum class FontWeight {Regular,Medium,SemiBold,ExtraBold,Black};
CCLabelBMFont* menuLabel(std::string text,float scale,CCPoint position,ccColor3B color,FontWeight) {
    auto* label=new CCLabelBMFont; label->setString(text.c_str()); label->setScale(scale+0.18f);
    label->setPosition(position); label->setColor(color); return label;
}
void fitMenuLabelWidth(CCLabelBMFont* label,float width,float maximum,float) {
    label->setScale(std::min(maximum+0.18f,width/label->size.width));
}
${method(read('src/ui/RankedVisuals.hpp'),'inline ccColor3B tierColor(')}
CCNode* surface(CCSize size,CCPoint position,ccColor3B,ccColor3B,int,bool) {
    auto* root=CCNode::create(); root->size=size; root->position=position; return root;
}
void cancelMenuTouch(CCMenu* menu) { if(menu)++menu->cancellations; }
namespace i18n { bool isKorean(){return false;} std::string text(char const*,char const* english){return english;} }
RankBadgeAssembly createRankBadgeAssembly(std::string const& tier,float size) {
    RankBadgeAssembly badge; badge.root=CCLayerColor::create({0,0,0,0}); badge.root->id=tier;
    for(int i=0;i<5;++i) { auto* part=CCSprite::createWithSpriteFrameName("part"); part->size={100,100}; part->setScale(size/100);
        badge.root->addChild(part); RankBadgePart b{"part",part,part->getScale()};
        (i<2?badge.baseParts:badge.ornaments).push_back(b); }
    return badge;
}
CCNode* createRankBadgeBackdrop(std::string const& tier,CCSize,RankBadgeDisplay) {
    auto* halo=CCLayerColor::create({0,0,0,0});halo->id="halo:"+tier;return halo;
}
RankTierProfile rankTierProfile(std::string const& tier) { RankTierProfile p;p.color=tierColor(tier);return p; }
inline std::vector<std::string> animatedBadgeRequests;
CCNode* createAnimatedRankBadge(std::string const& tier,CCSize size,RankBadgeDisplay) {
    animatedBadgeRequests.push_back(tier);
    return createRankBadgeAssembly(tier,size.width).root;
}
void fitRankSprite(CCSprite* sprite,CCSize size) { sprite->setScale(std::min(size.width/sprite->size.width,size.height/sprite->size.height)); }
struct PlayerIcon : CCNode {
    inline static std::vector<int> accounts;
    static PlayerIcon* createLocal(){accounts.push_back(99);return new PlayerIcon;}
    static PlayerIcon* createRemote(int account){accounts.push_back(account);return new PlayerIcon;}
};
CCSprite* captureBlurredResult(CCNode*,CCSize) { return new CCSprite; }
}
${strip(read('src/ui/StandingVisuals.hpp'))}
${strip(read('src/ui/ResultScoreLayer.hpp')).replace('private:','public:')}
${strip(read('src/ui/ResultScoreLayer.cpp'))}
`;

const goldModel=`
ScoreAnimationInput gold() {
    auto i=ordinary();i.beforeTier=i.afterTier="GOLD";i.before=1300;i.after=1400;
    i.standingBefore.rank=6;i.standingBefore.total=6;
    i.standingBefore.target=StandingTargetView{32,"nearest",false,5,1320};
    i.standingAfter.rank=2;i.standingAfter.total=6;
    i.standingAfter.target=StandingTargetView{50,"leader",false,1,1500};
    i.goldTimeline={true,5,0,{
        {1500,1,{50,"leader",false,1,1500},false},
        {1390,1,{39,"highest crossed",false,2,1390},false},
        {1350,2,{35,"tied bot",true,3,1350},false},
        {1320,1,{32,"nearest",false,5,1320},false}},std::nullopt};
    return i;
}
`;

test('GOLD warning and promotion tie states follow the shared server rank during bar movement',()=>{
    run(model+goldModel+`
int main(){
 auto i=gold();i.before=1200;i.after=1250;
 i.goldTimeline={true,3,0,{{1200,3,{3,"normal bot",true,1,1200},false,
    {{1,2,{3,"normal bot",true,1,1200},false},{0,1,{4,"warning",false,3,1200},false}}}},std::nullopt};
 i.goldTimeline.beforePriority=0;i.goldTimeline.afterPriority=1;
 i.standingAfter.rank=1;i.standingAfter.total=4;i.standingAfter.target.reset();
 auto s=sampleScoreProgress(i,0);assert(s.standing.rank==3&&s.standing.total==4&&s.standing.target->accountId==3&&s.standing.target->rank==1);
 s=sampleScoreProgress(i,1);assert(s.standing.rank==1&&s.goldLeader);
 i.after=1200;s=sampleScoreProgress(i,1);assert(s.standing.rank==3&&!s.goldLeader);
 s=sampleScoreProgress(i,2);assert(s.standing.rank==1&&s.goldLeader);
 i.goldTimeline.moving=GoldMovingRival{1200,1200,{2,"moving",false,1,1200},1,0};
 s=sampleScoreProgress(i,0);assert(s.standing.rank==4&&s.standing.target->rank==1);
 // Promotion wins split ranks even at one LP; nearest target uses the
 // closest higher state, not the highest state of that score group.
 i.goldTimeline.moving.reset();i.goldTimeline.beforePriority=1;
 i.goldTimeline.groups[0].states={{3,1,{3,"two wins",false,1,1200},false},{2,1,{4,"zero wins",false,2,1200},false},{1,1,{5,"normal",false,3,1200},false}};
 s=sampleScoreProgress(i,0);assert(s.standing.rank==3&&s.standing.target->accountId==4&&s.standing.target->rank==2);
}
`);
});

test('GOLD snapshot follows shared ranks, the highest overtake, moving rivals and losses without resetting the floor',()=>{
    run(model+goldModel+`
int main() {
    auto i=gold();auto s=sampleScoreProgress(i,0);
    assert(s.standing.rank==6&&s.standing.total==6&&s.standing.target->accountId==39&&!s.goldOvertake);
    s=sampleScoreProgress(i,1);assert(s.score==1350&&s.standing.rank==3&&s.standing.target->accountId==39);
    s=sampleScoreProgress(i,1.7);assert(s.standing.rank==2&&s.standing.target->accountId==50&&s.goldOvertake);
    s=sampleScoreProgress(i,2);assert(s.standing.rank==2&&s.target==1500&&s.fill>0.66&&s.fill<0.67);
    // A tied LP is a shared rank, not an overtake of that tied target.
    i.after=1390;i.standingAfter.target=StandingTargetView{50,"leader",false,1,1500};
    s=sampleScoreProgress(i,2);assert(s.goldOvertake&&s.standing.rank==2);
    i=gold();i.before=1380;i.after=1390;
    s=sampleScoreProgress(i,2);assert(!s.goldOvertake);
    i=gold();i.before=1400;i.after=1300;i.won=false;
    i.standingAfter.rank=6;i.standingAfter.target=StandingTargetView{32,"nearest",false,5,1320};
    int previous=0;bool changed=false;
    for(int frame=0;frame<=120;++frame) {
        s=sampleScoreProgress(i,frame/60.0);assert(s.minimum==1200&&s.fill>0&&!s.goldOvertake);
        if(previous&&s.standing.target->accountId!=previous)changed=true;
        previous=s.standing.target->accountId;
    }
    assert(changed&&s.standing.rank==6&&s.fill>0.83&&s.fill<0.84);
    i=gold();i.goldTimeline.moving=GoldMovingRival{1420,1380,{42,"moving",false,2,1420}};
    i.standingAfter.total=7;
    s=sampleScoreProgress(i,0);assert(s.standing.rank==7&&s.standing.target->accountId==42);
    s=sampleScoreProgress(i,1);assert(s.standing.rank==4&&s.standing.target->accountId==42&&s.target==1400);
    s=sampleScoreProgress(i,2);assert(s.goldOvertake&&s.standing.rank==2&&s.standing.total==7&&s.standing.target->accountId==50);
    i=gold();i.before=1200;i.after=1220;
    assert(sampleScoreProgress(i,0).fill==0&&sampleScoreProgress(i,0.01).fill>0);
    i.before=1400;i.after=1510;i.standingAfter.rank=1;i.standingAfter.target.reset();
    s=sampleScoreProgress(i,2);assert(s.goldLeader&&s.fill==1&&s.standing.rank==1);
}
`);
});

test('whole GOLD layer flies selected crossed avatars, shrinks loss targets and places own rank between LP and bar',()=>{
    run(scoreNative+goldModel+`
using namespace corum::ranked::ui;
bool containsAction(CCAction* a,std::string const& type) {
    if(!a)return false;
    if(a->type==type)return true;
    for(auto* child:a->children)if(containsAction(child,type))return true;
    return false;
}
int main() {
    CCNode screen;auto i=gold();
    auto* layer=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(layer);layer->onEnter();
    auto* old=layer->m_rightIcon;assert(PlayerIcon::accounts.back()==32&&layer->m_ownRankValue==6);
    int exchanged=0;
    for(int frame=0;frame<150;++frame) {
        layer->update(1.0f/60);
        assert(layer->m_fill->size.height==16&&layer->m_fill->size.width>0);
        assert(layer->m_ownRank->position.y>layer->m_barY+16&&layer->m_ownRank->position.y<layer->m_score->position.y);
        assert(std::abs(layer->m_ownRank->position.x-layer->m_score->position.x)<.001f);
        if(layer->m_rightIcon!=old) {
            ++exchanged;assert(layer->m_goldOvertakeShown);
            assert(containsAction(old->actions.back(),"CCMoveBy")&&!containsAction(old->actions.back(),"CCScaleTo"));
            assert(!layer->m_rightIcon->actions.empty()); old=layer->m_rightIcon;
        }
    }
    assert(exchanged==3&&layer->m_goldOvertakesShown==3&&PlayerIcon::accounts.back()==50);
    assert(layer->m_ownRankValue==2&&layer->m_ownRank->id=="ranked-result-current-rank");
    layer->update(.5f);assert(!layer->m_goldTargetChanging&&layer->m_rightCutoff->text=="1500 LP");
    assert(std::abs(layer->m_fill->size.width/layer->m_barWidth-2.0f/3)<.0001f);
    layer->onSkip(nullptr);drainMainQueue();
    i=gold();i.before=1400;i.after=1300;i.won=false;i.standingAfter.rank=6;
    i.standingAfter.target=StandingTargetView{32,"nearest",false,5,1320};
    layer=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(layer);layer->onEnter();
    old=layer->m_rightIcon;bool shrunk=false;
    for(int frame=0;frame<150;++frame) {
        layer->update(1.0f/60);assert(layer->m_fill->size.width>0&&layer->m_fill->size.height==16);
        if(layer->m_rightIcon!=old) {
            assert(containsAction(old->actions.back(),"CCScaleTo")&&!containsAction(old->actions.back(),"CCMoveBy"));
            shrunk=true;old=layer->m_rightIcon;
        }
    }
    assert(shrunk&&!layer->m_goldOvertakeShown&&layer->m_ownRankValue==6&&layer->m_rightCutoff->text=="1320 LP");
    assert(layer->m_fill->size.width/layer->m_barWidth>.83f);
    layer->onSkip(nullptr);drainMainQueue();
}
`);
});

test('GOLD caps individual overtakes at the highest five, serializes stalled frames and skips cleanly',()=>{
    run(scoreNative+goldModel+`
using namespace corum::ranked::ui;
int main() {
    CCNode screen;auto i=gold();
    i.goldTimeline.overtakes={
      {107,"highest",false,2,1390},{106,"six",false,3,1380},
      {-105,"bot",true,4,1370},{104,"four",false,5,1360},
      {103,"three",false,6,1350},{102,"two",false,7,1330},{101,"one",false,8,1320}};
    auto const targets=goldOvertakeTargets(i.goldTimeline,i.before,i.after);
    assert(targets.size()==5&&targets.front().accountId==103&&targets.back().accountId==107);
    assert(goldOvertakeTargets(i.goldTimeline,1400,1300).empty());
    auto* layer=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(layer);layer->onEnter();
    assert(PlayerIcon::accounts.back()==103);
    layer->update(3);assert(layer->m_goldOvertakesShown==1&&!layer->m_complete);
    layer->update(.1f);assert(layer->m_goldOvertakesShown==1); // each avatar gets time
    for(int n=2;n<=5;++n) {layer->update(.29f);assert(layer->m_goldOvertakesShown==static_cast<std::size_t>(n));}
    assert(layer->m_complete&&layer->m_goldOvertakesShown==5);
    assert(PlayerIcon::accounts.back()==50);
    layer->update(1);assert(layer->m_goldOvertakesShown==5);
    layer->onSkip(nullptr);drainMainQueue();
    layer=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(layer);layer->onEnter();
    layer->onSkip(nullptr);assert(layer->m_goldOvertakesShown==5&&layer->m_complete);
    layer->update(.4f);assert(PlayerIcon::accounts.back()==50);
    layer->onSkip(nullptr);drainMainQueue();
    i.before=1380;i.after=1390;i.goldTimeline.overtakes.clear();
    assert(goldOvertakeTargets(i.goldTimeline,i.before,i.after).empty()); // a final tie never flies
}
`);
});

test('actual rounded bar preserves full height at sub-pixel widths and has distinct finite polygon edges',()=>{
    run(scoreNative+`
int main() {
    using namespace corum::ranked::ui;CCDrawNode node;
    for(float width:{1e-9f,.0001f,.001f,.1f,1.0f,8.0f,16.0f,300.0f}) {
        drawRoundedBar(&node,width,{255,100,50});assert(node.size.height==16&&node.size.width==width);
        float minX=width,maxX=0,minY=16,maxY=0;
        for(auto const& p:node.polygon) {minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minY=std::min(minY,p.y);maxY=std::max(maxY,p.y);}
        assert(std::abs(minX)<.00001f&&std::abs(maxX-width)<.00001f&&std::abs(minY)<.00001f&&std::abs(maxY-16)<.00001f);
        if(width>=.001f)assert(node.polygon.size()>16);
    }
    drawRoundedBar(&node,0,{255,100,50});assert(node.polygon.empty());
}
`);
});

test('whole Corum score layer holds final state, excludes premature notices and retires before restoring input',()=>{
    run(scoreNative+`
using namespace corum::ranked::ui;
bool hasLabel(CCNode* n,std::string const& value) {
    if(auto* l=dynamic_cast<CCLabelBMFont*>(n);l&&l->text==value)return true;
    for(auto* c:n->children)if(hasLabel(c,value))return true;
    return false;
}
int main() {
    CCNode screen; auto i=ordinary(); i.promotionStarted=true;
    i.promotion={true,"RED","AQUA",300,3,1,{}}; int completions=0;
    ResultScoreLayer* score=nullptr;
    score=ResultScoreLayer::create(i,&screen,[&]{
        assert(score->parent==nullptr&&score->m_closed&&!score->keypad&&!score->m_menu->touch);
        assert(score->references>=2); ++completions;
    });
    screen.addChild(score); Ref keep(score); score->onEnter();
    assert(score->keypad&&score->m_menu->touch&&score->scheduled);
    assert(!hasLabel(score,"PROMOTION MATCH")&&hasLabel(score,"WIN"));
    score->update(1); assert(!score->m_complete&&!hasLabel(score,"PROMOTION MATCH"));
    score->onExit(); assert(!score->keypad&&!score->m_menu->touch&&!score->scheduled&&completions==0);
    score->onEnter(); assert(score->keypad&&score->m_menu->touch&&score->scheduled);
    score->update(1); assert(score->m_complete&&score->scheduled&&hasLabel(score,"PROMOTION MATCH"));
    assert(score->m_seriesRoot->children.back()->children.size()==3&&completions==0&&score->visible);
    score->update(99); assert(completions==0&&score->m_score->text=="125 LP");
    auto const cancellations=score->m_menu->cancellations;
    score->keyBackClicked(); assert(completions==0&&score->parent==&screen&&score->keypad);
    assert(!score->m_menu->enabled&&score->m_menu->touch&&score->m_menu->cancellations==cancellations);
    score->onSkip(nullptr); assert(mainQueue.size()==1);drainMainQueue();assert(completions==1);
    i=ordinary(); i.after=i.before; i.demotionWarning=true;
    auto* warning=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(warning); warning->onEnter();
    assert(warning->m_delta->text=="+0 LP"&&warning->m_delta->color==TextMuted);
    assert(!hasLabel(warning,"DEMOTION WARNING")); warning->update(2);
    assert(hasLabel(warning,"DEMOTION WARNING")); warning->onSkip(nullptr);warning->onSkip(nullptr);drainMainQueue();
    i=ordinary(); i.promotionInProgress=true; i.previousMarks=1;
    i.promotion={true,"RED","AQUA",300,10,5,{"WIN","LOSS"}};
    auto* series=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(series); series->onEnter();
    assert(hasLabel(series,"PROMOTION MATCH"));
    auto* grid=series->m_seriesRoot->children.back(); assert(grid->children.size()==10&&grid->scaleX>0);
    assert(!grid->children[0]->actions.empty()); // gray square slams when it appears
    assert(grid->children[0]->children[0]->actions.empty()); // previous check remains
    assert(!grid->children[1]->children[0]->actions.empty()); // latest X stamps
    assert(grid->children[1]->children[0]->children.size()==2); // a real X, not a delete/trash icon
    series->onSkip(nullptr);series->onSkip(nullptr);drainMainQueue();
}
`);
});

test('whole score layer changes both icons, uses bot/self GOLD avatars and readable competition rank colors',()=>{
    run(scoreNative+`
using namespace corum::ranked::ui;
int main() {
    CCNode screen; auto i=ordinary(); i.before=299; i.after=350; i.afterTier="AQUA"; i.promoted=true;
    auto* promotion=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(promotion);
    auto* oldLeft=promotion->m_leftIcon; auto* oldRight=promotion->m_rightIcon;
    promotion->update(2);
    assert(promotion->m_tier=="AQUA"&&promotion->m_targetKey=="BRONZE");
    assert(promotion->m_leftIcon!=oldLeft&&promotion->m_rightIcon!=oldRight);
    assert(oldLeft->actions.size()==1&&oldRight->actions.size()==1);
    assert(promotion->m_leftIcon->children[0]->opacity==0); // Placed-style part assembly
    assert(promotion->m_fill->segmentColor.r==tierColor("AQUA").r/255.0f);
    assert(promotion->m_leftIcon->children.back()->id=="halo:AQUA");
    promotion->onSkip(nullptr);promotion->onSkip(nullptr);drainMainQueue();
    i=ordinary(); i.beforeTier="AQUA"; i.afterTier="RED"; i.before=300; i.after=250; i.demoted=true;
    auto* demotion=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(demotion);
    oldLeft=demotion->m_leftIcon; oldRight=demotion->m_rightIcon; demotion->update(2);
    assert(demotion->m_tier=="RED"&&demotion->m_leftIcon!=oldLeft&&demotion->m_rightIcon!=oldRight);
    for(auto* part:oldLeft->children)if(dynamic_cast<CCSprite*>(part))assert(part->actions[0]->type=="spawn"); // falling parts, not shrink
    assert(demotion->m_leftIcon->scaleX==0.25f); demotion->onSkip(nullptr);demotion->onSkip(nullptr);drainMainQueue();
    i=ordinary(); i.beforeTier=i.afterTier="GOLD"; i.before=1225; i.after=1250;
    i.standingAfter.rank=5; i.standingAfter.total=10; i.standingAfter.topPercent=50;
    i.standingAfter.target=StandingTargetView{123,"bot",true,4,1275};
    auto* gold=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(gold);
    assert(PlayerIcon::accounts.back()==0&&gold->m_targetKey=="gold:123:1");
    gold->update(2); assert(PlayerIcon::accounts.back()==0&&gold->m_rightCutoff->text=="1275 LP");
    gold->onSkip(nullptr);gold->onSkip(nullptr);drainMainQueue();
    i.standingAfter.rank=1; i.standingAfter.target.reset();
    auto* leader=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(leader);
    assert(PlayerIcon::accounts.back()==99&&leader->m_targetKey=="gold-self");
    assert(leader->m_fill->size.width==leader->m_barWidth); leader->onSkip(nullptr);leader->onSkip(nullptr);drainMainQueue();
    assert(standingColor(1)==Gold&&standingColor(4)==ccc3(16,20,28));
    assert(standingNumber(4,0.4f)->children.size()==9); // black numbers have a light outline
    assert(standingNumber(2,0.4f)->children.size()==1);
    StandingView standing; standing.rank=2;standing.total=100;standing.topPercent=2;
    assert(topPercentText(standing)=="Top 2%");
    standing.topPercent=0.001;assert(topPercentText(standing)=="Top <0.01%");
    standing={};assert(topPercentText(standing)=="Top —%");
}
`);
});

test('actual bar keeps sub-LP motion, rounded ends and the LP center on the fill endpoint',()=>{
    run(scoreNative+`
using namespace corum::ranked::ui;
int main() {
    auto i=ordinary();i.after=101;
    auto a=sampleScoreProgress(i,0.50),b=sampleScoreProgress(i,0.51);
    assert(a.score==b.score&&b.fill>a.fill); // motion continues between integer label changes
    auto const start=sampleScoreProgress(i,0).fill,finish=sampleScoreProgress(i,2).fill;
    auto const firstStep=sampleScoreProgress(i,0.01).fill-start;
    auto const middleStep=sampleScoreProgress(i,1.01).fill-sampleScoreProgress(i,1).fill;
    auto const lastStep=finish-sampleScoreProgress(i,1.99).fill;
    assert(firstStep>0&&middleStep>firstStep*30&&middleStep>lastStep*30);
    assert(sampleScoreProgress(i,2).score==101&&sampleScoreProgress(i,2).complete);
    CCNode screen;auto* layer=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(layer);
    auto const left=layer->m_leftIcon->position,right=layer->m_rightIcon->position;
    for(float dt:{0.01f,0.10f,0.39f,0.01f,0.49f,1.0f}) {
        layer->update(dt);auto sample=sampleScoreProgress(i,layer->m_elapsed);
        auto const end=layer->m_barLeft+layer->m_barWidth*static_cast<float>(sample.fill);
        assert(std::abs(layer->m_score->position.x-end)<0.0001f);
        assert(layer->m_leftIcon->position.x==left.x&&layer->m_rightIcon->position.x==right.x);
        assert(layer->m_leftCutoff->position.x==layer->m_barLeft);
        assert(layer->m_rightCutoff->position.x==layer->m_barLeft+layer->m_barWidth);
        auto* fill=layer->m_fill;assert(fill->segments==1&&fill->polygon.size()>16);
        float minX=fill->size.width,minY=16,maxX=0,maxY=0;
        for(auto const& p:fill->polygon){minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);}
        assert(std::abs(minX)<0.0001f&&std::abs(maxX-fill->size.width)<0.0001f);
        assert(std::abs(minY)<0.0001f&&std::abs(maxY-16)<0.0001f);
        assert(fill->polygon.front().y<0.0001f&&fill->polygon.front().x>0&&fill->polygon.front().x<fill->size.width);
        assert(layer->m_delta->position.x!=layer->m_score->position.x);
    }
    layer->onSkip(nullptr);layer->onSkip(nullptr);drainMainQueue();
    i.before=i.after=0;auto* empty=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(empty);
    assert(empty->m_fill->segments==0&&empty->m_score->position.x==empty->m_barLeft);
    empty->onSkip(nullptr);empty->onSkip(nullptr);drainMainQueue();
    i.beforeTier=i.afterTier="GOLD";i.before=1200;i.after=1500;i.standingAfter.rank=1;
    auto* full=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(full);
    assert(full->m_fill->polygon.size()>16&&full->m_score->position.x==full->m_barLeft+full->m_barWidth);
    assert(full->m_delta->position.x+full->m_delta->size.width*full->m_delta->scaleX/2<=full->size.width-12); // delta fits; LP stays anchored
    full->onSkip(nullptr);full->onSkip(nullptr);drainMainQueue();
}
`);
});

test('actual score track uses the immediately lower tier and follows promotion/demotion crossings',()=>{
    run(scoreNative+`
using namespace corum::ranked::ui;
void assertTrack(ResultScoreLayer* layer, std::string const& tier) {
    auto const& bands=layer->m_input.bands;
    auto const band=std::find_if(bands.begin(),bands.end(),[&](auto const& b){return b.tier==tier;});
    auto const expected=band==bands.begin()?ccc3(100,108,124):tierColor(std::prev(band)->tier);
    auto const actual=layer->m_track->segmentColor;
    assert(actual.r==expected.r/255.0f&&actual.g==expected.g/255.0f&&actual.b==expected.b/255.0f);
    assert(actual.a==1.0f&&layer->m_track->size.width==layer->m_barWidth&&layer->m_track->polygon.size()>16);
    auto const foreground=tierColor(tier);
    if(layer->m_fill->size.width>0)assert(layer->m_fill->segmentColor.r==foreground.r/255.0f);
    assert(layer->m_score->color==foreground&&layer->m_tier==tier);
}
int main() {
    CCNode screen;auto i=ordinary();
    for(auto const& band:i.bands) {
        i.beforeTier=i.afterTier=band.tier;i.before=band.minimum+20;i.after=i.before+13;
        auto* layer=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(layer);
        assertTrack(layer,band.tier);layer->update(1);assertTrack(layer,band.tier);
        layer->update(1);assertTrack(layer,band.tier);layer->onSkip(nullptr);layer->onSkip(nullptr);drainMainQueue();
    }
    for(std::size_t index=1;index<i.bands.size();++index) {
        i=ordinary();auto const& lower=i.bands[index-1];auto const& higher=i.bands[index];
        i.beforeTier=lower.tier;i.afterTier=higher.tier;i.before=higher.minimum-1;i.after=higher.minimum+50;i.promoted=true;
        auto* promotion=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(promotion);
        assertTrack(promotion,lower.tier);promotion->update(2);assertTrack(promotion,higher.tier);
        promotion->update(2);assertTrack(promotion,higher.tier);promotion->onSkip(nullptr);promotion->onSkip(nullptr);drainMainQueue();
        i.beforeTier=higher.tier;i.afterTier=lower.tier;i.before=higher.minimum;i.after=higher.minimum-50;
        i.promoted=false;i.demoted=true;
        auto* demotion=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(demotion);
        assertTrack(demotion,higher.tier);demotion->update(2);assertTrack(demotion,lower.tier);
        demotion->update(2);assertTrack(demotion,lower.tier);demotion->onSkip(nullptr);demotion->onSkip(nullptr);drainMainQueue();
    }
}
`);
});

test('Skip/Esc settles notices first; the next press retires, and track color wipes from left to right',()=>{
    run(scoreNative+`
using namespace corum::ranked::ui;
int main(){
 CCNode screen;auto i=ordinary();i.demotionWarning=true;int closed=0;
 auto* warning=ResultScoreLayer::create(i,&screen,[&]{++closed;});screen.addChild(warning);warning->onEnter();
 assert(warning->m_skipCaption->text=="SKIP"&&!warning->m_noticesShown);
 warning->keyBackClicked();assert(warning->m_complete&&warning->m_readyToClose&&warning->m_noticesShown&&!warning->m_closed);
 assert(warning->m_score->text=="125 LP"&&warning->m_skipCaption->text=="CONTINUE"&&closed==0&&mainQueue.empty());
 for(auto const& [node,scale]:warning->m_stamps)assert(node->visible&&node->scaleX==scale&&node->actions.empty());
 warning->keyBackClicked();assert(warning->m_closed&&closed==0&&mainQueue.size()==1);drainMainQueue();assert(closed==1);
 auto* natural=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(natural);natural->onEnter();
 natural->update(2);assert(natural->m_complete&&!natural->m_readyToClose&&natural->scheduled);
 natural->update(.26f);assert(!natural->m_readyToClose);natural->update(.02f);assert(natural->m_readyToClose&&!natural->scheduled);
 natural->onSkip(nullptr);drainMainQueue();
 i=ordinary();i.before=299;i.after=350;i.afterTier="AQUA";i.promoted=true;
 auto* promotion=ResultScoreLayer::create(i,&screen,[]{});screen.addChild(promotion);
 auto const previous=promotion->m_track->segmentColor;
 for(int frame=0;frame<250&&!promotion->m_trackChanging;++frame)promotion->update(.016f);
 assert(promotion->m_trackChanging&&promotion->m_track->segmentColor.r==previous.r);
 auto first=promotion->m_trackWipe->size.width;assert(first>0&&first<promotion->m_barWidth);
 promotion->update(.10f);assert(promotion->m_trackWipe->size.width>first&&promotion->m_trackChanging);
 assert(promotion->m_trackWipe->position.x==promotion->m_barLeft);
 promotion->update(.6f);assert(!promotion->m_trackChanging&&promotion->m_trackWipe->segments==0);
 assert(promotion->m_track->segmentColor.r==tierColor("RED").r/255.0f);
 assert(promotion->m_leftTier->anchor.x==1&&promotion->m_leftTier->position.x<promotion->m_barLeft);
 assert(promotion->m_rightTier->anchor.x==0&&promotion->m_rightTier->position.x>promotion->m_barLeft+promotion->m_barWidth);
 promotion->onSkip(nullptr);assert(promotion->m_icons->children.size()==2);
 for(auto* part:promotion->m_leftIcon->children)if(dynamic_cast<CCSprite*>(part))assert(part->opacity==255&&part->actions.empty());
 promotion->onSkip(nullptr);drainMainQueue();
}
`);
});

test('font package contains the new Korean notices and unplaced em dash without changing countdown glyphs',()=>{
    const fonts=JSON.parse(read('mod.json')).resources.fonts;
    const required='—티어로 승급했습니다!총명다른 기기 접속 끊기디버그 도전과제 모두 해금플레이어 기록 초기화다음 경기에서 패배하면 강등될 수 있습니다.Jukebox 모드가 필요합니다!';
    for(const [name,font] of Object.entries(fonts)) {
        if(name==='CorumSansCountdown') { assert.equal(font.charset,'32,48-57');continue; }
        const points=new Set;
        for(const range of font.charset.split(',')) {
            const [a,b]=range.split('-').map(Number);
            for(let p=a;p<=(b??a);++p)points.add(p);
        }
        for(const ch of name==='CorumSans'?'—':required)assert(points.has(ch.codePointAt(0)),name+': '+ch);
    }
});

const runtimeSource=read('src/RankedRuntime.cpp'),popupSource=read('src/RankedPopup.cpp');
const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
const jsonDouble=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'))
    .replace('bool isErr()const','bool isOk()const{return value.has_value();}\n    bool isErr()const');
const runtimeDouble=`
#include <chrono>
#include <map>
#include <variant>
#include <charconv>
${jsonDouble}
namespace geode::utils::web {
struct WebResponse {matjson::Value root;int status=200;matjson::Result<matjson::Value>json()const{return {root};}};
struct Operation {std::string url;};
struct WebRequest {void timeout(std::chrono::seconds){} Operation get(std::string url){return {url};}};
struct Pending {Operation request;std::function<void(WebResponse)>callback;};
inline std::vector<Pending>pending;
}
namespace web=geode::utils::web;
namespace geode::async {template<class T>struct TaskHolder {
    void cancel(){} void spawn(web::Operation request,std::function<void(T)>cb){web::pending.push_back({request,std::move(cb)});}
};}
${read('src/RankedRuntime.hpp').replace(/^#include <Geode.*?$|^#pragma once$/gm,'').replace('private:','public:')}
template<class T>matjson::Result<T>numFromString(std::string const& text) {
    T value{};auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()?matjson::Result<T>{value}:matjson::Result<T>{};
}
${['corum::ranked::MapMetadata parseMapMetadata(','std::vector<std::string> parseRatingMarks(','corum::ranked::StandingView parseStanding(',
    'corum::ranked::ScoreSeriesView parseScoreSeries(','corum::ranked::GoldScoreTimeline parseGoldTimeline(','corum::ranked::ResultScoreView parseResultScore(']
    .map(name=>method(runtimeSource,name)).join('\n')}
namespace corum::ranked {
RankedRuntime& RankedRuntime::get(){static RankedRuntime r;return r;}
RuntimeView const& RankedRuntime::view()const{return m_view;}
std::string RankedRuntime::endpoint(std::string const& path)const{return path;}
bool successful(web::WebResponse const& response){return response.status>=200&&response.status<300;}
web::WebRequest baseRequest(std::string const&){return {};}
${method(runtimeSource,'void RankedRuntime::fetchStanding()')}
}
`;

test('actual status helper shows YOUR RANK only in the lobby and keeps card streak/promotion warnings',()=>{
    run(scoreNative+runtimeDouble+`
namespace corum::ranked::i18n {
std::string streak(int,bool){return "2 WINS";}
std::string promotionMatch(int){return "PROMOTION MATCH";}
}
using corum::ranked::RankedProfileView;
struct StatusFixture {
    CCNode root;CCNode* m_root=&root;
    ccColor3B kCyan{0,255,255},kGold{255,255,0},kRed{255,0,0};
    std::string tr(char const*,char const* english){return english;}
    CCLabelBMFont* makeLabel(std::string const& text,float,CCPoint,ccColor3B,char const*) {
        auto* label=new CCLabelBMFont;label->setString(text.c_str());return label;
    }
    ${method(popupSource,'void addRankStatus(')}
};
int main() {
    StatusFixture f;RankedProfileView profile;
    f.addRankStatus(profile,{},.26f,170);assert(f.root.children.empty());
    f.addRankStatus(profile,{},.26f,170,true);
    assert(f.root.children.size()==1&&static_cast<CCLabelBMFont*>(f.root.children.back())->text=="YOUR RANK");
    profile.resultStreak=2;profile.demotionWarning=true;
    f.addRankStatus(profile,{},.26f,170);
    assert(static_cast<CCLabelBMFont*>(f.root.children.back())->text=="2 WINS · DEMOTION WARNING");
    profile.resultStreak=0;profile.demotionWarning=false;profile.promotion.active=true;
    f.addRankStatus(profile,{},.26f,170);
    assert(static_cast<CCLabelBMFont*>(f.root.children.back())->text=="PROMOTION MATCH");
}
`);
    assert.match(method(popupSource,'void renderModeCard('),/addRankStatus\(profile,[^;]+,true\)/);
    assert.doesNotMatch(method(popupSource,'void addPlayerCard('),/addRankStatus\(profile,[^;]+,true\)/);
});

test('actual decoders and standings request reject stale sessions and validate result metadata',()=>{
    run(scoreNative+runtimeDouble+`
int main() {
    using V=matjson::Value;
    V metadata;metadata["kind"]="DEMON";metadata["stars"]=10;metadata["gddlTier"]=16;
    metadata["gddlColor"]="#da4534";metadata["durationSeconds"]=90.5;metadata["officialLevelId"]=14;
    auto mapInfo=parseMapMetadata(metadata);
    assert(mapInfo.kind=="DEMON"&&mapInfo.gddlTier==16&&mapInfo.gddlColor=="#da4534"&&mapInfo.durationSeconds==90.5);
    assert(parseMapMetadata(V{}).kind=="UNKNOWN");
    metadata["gddlTier"]=41;metadata["stars"]=100;metadata["gddlColor"]="#zzzzzz";metadata["durationSeconds"]=-1;
    mapInfo=parseMapMetadata(metadata);assert(!mapInfo.gddlTier&&!mapInfo.stars&&mapInfo.gddlColor.empty()&&!mapInfo.durationSeconds);
    V standing;standing["rank"]=2;standing["total"]=100;standing["topPercent"]=99;
    standing["target"]["gdAccountId"]="123";standing["target"]["player"]="target";
    standing["target"]["rank"]=1;standing["target"]["visibleRankedScore"]=1325;standing["target"]["isBot"]=true;
    auto decoded=parseStanding(standing);assert(decoded.rank==2&&decoded.topPercent==2&&decoded.target->bot);
    standing["target"]["rank"]=2;assert(!parseStanding(standing).target);
    standing["rank"]=101;assert(!parseStanding(standing).rank);standing["rank"]=2;
    V promotion;promotion["gamesRequired"]=10;promotion["winsRequired"]=5;
    promotion["results"]=V::Array{"WIN","LOSS"};assert(parseScoreSeries(promotion).active);
    promotion["winsRequired"]=11;assert(!parseScoreSeries(promotion).active);
    V score;score["version"]=1;V::Array bands;
    for(auto const& band:ordinary().bands){V item;item["tier"]=band.tier;item["minInclusive"]=band.minimum;bands.push_back(item);}
    score["tierBands"]=bands;score["players"]["A"]["before"]=standing;score["players"]["A"]["after"]=standing;
    score["players"]["B"]["before"]=standing;score["players"]["B"]["after"]=standing;
    V timeline;timeline["fixedTotal"]=3;timeline["higherFixed"]=0;
    V group;group["score"]=1325;group["count"]=2;group["target"]=standing["target"];group["target"]["rank"]=1;
    timeline["groups"]=V::Array{group};
    timeline["moving"]["beforeScore"]=1400;timeline["moving"]["afterScore"]=1300;
    timeline["moving"]["target"]=group["target"];
    auto gold=parseGoldTimeline(timeline);assert(gold.available&&gold.groups.size()==1&&gold.groups[0].count==2&&gold.moving->after==1300);
    auto statesTimeline=timeline;V normal;normal["priority"]=1;normal["count"]=1;normal["target"]=group["target"];
    V warning=normal;warning["priority"]=0;statesTimeline["beforePriority"]=0;statesTimeline["afterPriority"]=1;
    group["states"]=V::Array{normal,warning};statesTimeline["groups"]=V::Array{group};
    auto decodedStates=parseGoldTimeline(statesTimeline);assert(decodedStates.available&&decodedStates.beforePriority==0&&decodedStates.groups[0].states.size()==2);
    group["states"]=V::Array{normal};statesTimeline["groups"]=V::Array{group};assert(!parseGoldTimeline(statesTimeline).available);
    group["states"]=V::Array{normal,normal};statesTimeline["groups"]=V::Array{group};assert(!parseGoldTimeline(statesTimeline).available);
    auto malformedState=timeline;malformedState["beforePriority"]=-1;assert(!parseGoldTimeline(malformedState).available);
    malformedState=timeline;malformedState["moving"]["afterPriority"]=33;assert(!parseGoldTimeline(malformedState).available);
    auto withOvertakes=timeline;
    withOvertakes["overtakes"]=V::Array{group["target"]};
    assert(parseGoldTimeline(withOvertakes).overtakes.size()==1);
    withOvertakes["overtakes"]=V::Array{group["target"],group["target"]};
    assert(!parseGoldTimeline(withOvertakes).available); // duplicate avatar cannot fly twice
    withOvertakes["overtakes"]=V::Array(6,group["target"]);
    assert(!parseGoldTimeline(withOvertakes).available);
    score["players"]["A"]["goldTimeline"]=timeline;
    assert(parseResultScore(score).playerA.goldTimeline.available&&!parseResultScore(score).playerB.goldTimeline.available);
    auto invalid=timeline;invalid["higherFixed"]=4;assert(!parseGoldTimeline(invalid).available);
    invalid=timeline;group["count"]=0;invalid["groups"]=V::Array{group};assert(!parseGoldTimeline(invalid).available);
    auto const originalGroup=timeline["groups"].asArray().unwrap().front();
    invalid=timeline;invalid["groups"]=V::Array{originalGroup,originalGroup};assert(!parseGoldTimeline(invalid).available);
    invalid=timeline;invalid["moving"]["beforeScore"]=-1;invalid["moving"]["afterScore"]=-1;assert(!parseGoldTimeline(invalid).available);
    assert(parseResultScore(score).available);score["version"]=2;assert(!parseResultScore(score).available);
    score["version"]=1;bands[2]["minInclusive"]=300;score["tierBands"]=bands;assert(!parseResultScore(score).available);
    auto& r=RankedRuntime::get();r.m_rankedOpen=true;r.m_sessionToken="first";
    r.fetchStanding();assert(web::pending.size()==1&&r.m_standingBusy);
    V response;response["standing"]=standing;web::pending[0].callback({response});
    assert(!r.m_standingBusy&&r.m_view.standing.rank==2&&r.m_view.standing.topPercent==2);
    r.fetchStanding();assert(web::pending.size()==2);
    // A callback queued before begin/leave cancellation must never update the next session.
    ++r.m_standingGeneration;r.m_sessionToken="second";r.m_standingBusy=false;r.m_view.standing={};
    r.fetchStanding();web::pending[1].callback({response});assert(!r.m_view.standing.rank&&r.m_standingBusy);
    web::pending[2].callback({response});assert(r.m_view.standing.rank==2&&!r.m_standingBusy);
    r.fetchStanding();web::pending[3].callback({{},503});assert(r.m_view.standing.rank==2&&!r.m_standingBusy);
    r.fetchStanding();r.m_rankedOpen=false;response["standing"]["rank"]=1;
    web::pending[4].callback({response});assert(r.m_view.standing.rank==2);
}
`);
});

test('actual result handoff separates Esc dispatches, never replays polling results and leaves Placement unchanged',()=>{
    const transition=read('src/ui/RankTransitionLayer.cpp');
    run(scoreNative+runtimeDouble+`
using corum::ranked::RankedRuntime;using corum::ranked::RankedProfileView;
namespace corum::ranked::ui {
enum class RankTransitionType {Placement,Promotion,Demotion};
struct RankTransitionLayer : CCLayerColor {
    bool m_finished=false,m_chainToResult=false,m_skipAllowed=false,m_finalShown=false;
    RankTransitionType m_type=RankTransitionType::Promotion;
    CCMenu controls;CCMenu* m_controlMenu=&controls;std::function<void()>m_completion;
    std::string m_oldRank="RED",m_newRank="AQUA",m_eventText="PROMOTED!";
    CCNode* m_animationRoot=CCNode::create();
    CCMenuItemSpriteExtra skip,continuation;
    CCMenuItemSpriteExtra* m_skipItem=&skip;CCMenuItemSpriteExtra* m_continueItem=&continuation;
    void addFlash(ccColor3B,GLubyte,float){}
    void addParticles(std::string const&,int,float,float,float,bool,bool){}
    static RankTransitionLayer* create(std::string oldRank,std::string newRank,RankTransitionType type,std::string,std::string,std::string,
        std::function<void()>callback,bool chain) {
        auto* layer=new RankTransitionLayer;layer->m_completion=std::move(callback);
        layer->m_type=type;layer->m_oldRank=std::move(oldRank);layer->m_newRank=std::move(newRank);
        layer->m_chainToResult=chain&&type!=RankTransitionType::Placement;layer->m_skipAllowed=layer->m_chainToResult;return layer;
    }
    ${method(transition,'void RankTransitionLayer::revealFinalState(').replace('RankTransitionLayer::','')}
    ${method(transition,'void RankTransitionLayer::onSkip(').replace('RankTransitionLayer::','')}
    ${method(transition,'void RankTransitionLayer::onContinue(').replace('RankTransitionLayer::','')}
    ${method(transition,'void RankTransitionLayer::finishChainedReveal()').replace('RankTransitionLayer::','')}
    ${method(transition,'void RankTransitionLayer::keyBackClicked()').replace('RankTransitionLayer::','')}
};
}
using corum::ranked::ui::RankTransitionLayer;using corum::ranked::ui::RankTransitionType;
std::string upper(std::string text){return text;}
std::string tr(char const*,char const* english){return english;}
std::pair<std::string,ccColor3B>ratingEventText(corum::ranked::RatingEventView const&){return {"event",{255,255,255}};}
${method(popupSource,'std::optional<RankTransitionType> authoritativeTransitionType(')}
struct CorumRankedLayer : CCLayerColor {
    CCMenu menu;CCMenu* m_menu=&menu;CCLayerColor fade;CCLayerColor* m_fadeRoot=&fade;
    RankTransitionLayer* m_rankTransitionLayer=nullptr;
    corum::ranked::ui::ResultScoreLayer* m_resultScoreLayer=nullptr;
    corum::ranked::ResultPresentationGate m_resultPresentationGate;
    bool m_resultScorePending=false;std::string m_rankTransitionPlayedKey;int rendered=0,toasts=0;
    void render(){++rendered;}void updateDynamicUi(){}void playNextAchievementToast(){++toasts;}
    ${['bool playResultRankTransition(','void restoreResultInput()','std::optional<corum::ranked::ScoreAnimationInput> resultScoreInput(',
       'void queueResultScore(','void beginResultScore('].map(name=>method(popupSource,name)).join('\n')}
};
corum::ranked::MatchView settled(std::string id) {
    corum::ranked::MatchView match;match.matchId=id;match.state="MATCH_RESULT";match.side="A";match.winnerSide="A";
    auto& before=match.profileBeforeA;before.displayedTier="RED";before.visibleRankedScore=100;before.placementGames=before.placementGamesRequired=5;
    match.profileAfterA=before;match.profileAfterA.visibleRankedScore=125;match.scorePresentation.available=true;
    match.scorePresentation.bands=ordinary().bands;return match;
}
int main() {
    auto& runtime=RankedRuntime::get();CorumRankedLayer owner;runtime.m_view.match=settled("normal");auto& match=runtime.m_view.match;
    auto input=owner.resultScoreInput(match);assert(input);
    match.profileBeforeA.visibleRankedScore=0;assert(owner.resultScoreInput(match)->before==0);
    match.placement=true;assert(!owner.resultScoreInput(match));match.placement=false;
    match.state="PLACEMENT_RESULT";assert(!owner.resultScoreInput(match));match.state="MATCH_RESULT";
    match.profileBeforeA.placementGames=4;assert(!owner.resultScoreInput(match));match.profileBeforeA.placementGames=5;
    match.profileBeforeA.displayedTier="UNRANKED";assert(!owner.resultScoreInput(match));match.profileBeforeA.displayedTier="RED";
    match.profileBeforeA.visibleRankedScore.reset();assert(!owner.resultScoreInput(match));match.profileBeforeA.visibleRankedScore=100;
    owner.beginResultScore(match,*input);assert(owner.m_resultScorePending&&!owner.m_resultScoreLayer&&!owner.keypad&&!owner.menu.touch);
    owner.beginResultScore(match,*input);assert(mainQueue.size()==1);drainMainQueue();
    auto* score=owner.m_resultScoreLayer;assert(score&&!owner.m_resultScorePending);
    Ref keepScore(score);score->onEnter();score->keyBackClicked();assert(score->m_readyToClose&&!score->m_closed&&mainQueue.empty());score->keyBackClicked();
    assert(owner.m_resultScoreLayer==score&&score->parent==&owner&&!owner.keypad&&owner.rendered==0);
    drainMainQueue();assert(!owner.m_resultScoreLayer&&!owner.keypad&&!owner.menu.touch&&owner.rendered==1);
    drainMainQueue();assert(owner.keypad&&owner.menu.touch&&owner.toasts==1);
    owner.beginResultScore(match,*input);assert(mainQueue.empty()&&!owner.m_resultScoreLayer);
    match=settled("promotion");match.profileBeforeA.visibleRankedScore=299;match.profileAfterA.visibleRankedScore=350;
    match.profileAfterA.displayedTier="AQUA";match.ratingEventA.promotionSucceeded=true;
    owner.beginResultScore(match,*owner.resultScoreInput(match));
    auto* rank=owner.m_rankTransitionLayer;assert(rank&&!owner.m_resultScorePending&&!owner.keypad);
    Ref keepRank(rank);rank->revealFinalState(false);
    assert(rank->m_finalShown&&!rank->m_finished&&rank->parent==&owner&&mainQueue.empty());
    assert(rank->m_continueItem->visible&&rank->m_continueItem->enabled&&!owner.m_resultScorePending);
    rank->revealFinalState(false);assert(mainQueue.empty()); // natural completion holds the Continue screen
    auto const cancellations=rank->controls.cancellations;
    rank->onContinue(nullptr);rank->onContinue(nullptr);rank->keyBackClicked();
    assert(rank->m_finished&&rank->parent==&owner&&owner.m_rankTransitionLayer==rank);
    assert(rank->controls.cancellations==cancellations&&mainQueue.size()==1);
    drainMainQueue();assert(!rank->parent&&!owner.m_rankTransitionLayer&&owner.m_resultScorePending);
    assert(!owner.m_resultScoreLayer&&!owner.keypad);
    drainMainQueue();score=owner.m_resultScoreLayer;assert(score);score->onSkip(nullptr);score->onSkip(nullptr);
    drainMainQueue();assert(!owner.keypad);drainMainQueue();assert(owner.keypad&&owner.menu.touch);
    match=settled("promotion-skip");match.profileAfterA.displayedTier="AQUA";match.ratingEventA.promotionSucceeded=true;
    owner.beginResultScore(match,*owner.resultScoreInput(match));rank=owner.m_rankTransitionLayer;
    rank->onSkip(nullptr);assert(rank->m_finished&&!rank->m_finalShown&&owner.m_rankTransitionLayer==rank);
    drainMainQueue();assert(owner.m_resultScorePending);drainMainQueue();assert(owner.m_resultScoreLayer);
    owner.m_resultScoreLayer->keyBackClicked();owner.m_resultScoreLayer->keyBackClicked();drainMainQueue();drainMainQueue();
    match=settled("demotion");match.profileBeforeA.displayedTier="AQUA";match.profileBeforeA.visibleRankedScore=300;
    match.profileAfterA.visibleRankedScore=250;match.ratingEventA.demoted=true;
    owner.beginResultScore(match,*owner.resultScoreInput(match));rank=owner.m_rankTransitionLayer;
    rank->keyBackClicked();assert(!owner.m_resultScorePending&&mainQueue.size()==1);
    drainMainQueue();assert(owner.m_resultScorePending);drainMainQueue();assert(owner.m_resultScoreLayer);
    owner.m_resultScoreLayer->onSkip(nullptr);owner.m_resultScoreLayer->onSkip(nullptr);drainMainQueue();drainMainQueue();
    RankTransitionLayer placed;placed.m_chainToResult=false;placed.keyBackClicked();assert(!placed.m_finalShown&&!placed.m_finished);
    placed.m_skipAllowed=true;placed.keyBackClicked();assert(placed.m_finalShown&&!placed.m_finished);
    // Only GOLD -> SILVER full-screen demotion removes the final halo.
    auto const haloRequests=corum::ranked::ui::animatedBadgeRequests.size();
    RankTransitionLayer goldDemotion;goldDemotion.m_type=RankTransitionType::Demotion;
    goldDemotion.m_oldRank="GOLD";goldDemotion.m_newRank="SILVER";goldDemotion.revealFinalState(false);
    assert(corum::ranked::ui::animatedBadgeRequests.size()==haloRequests);
    RankTransitionLayer silverPromotion;silverPromotion.m_type=RankTransitionType::Promotion;
    silverPromotion.m_oldRank="BRONZE";silverPromotion.m_newRank="SILVER";silverPromotion.revealFinalState(false);
    assert(corum::ranked::ui::animatedBadgeRequests.size()==haloRequests+1);
    // A retained but retired owner must not render or open another foreground.
    match=settled("retired");owner.beginResultScore(match,*owner.resultScoreInput(match));drainMainQueue();
    auto rendered=owner.rendered;owner.m_resultScoreLayer->onSkip(nullptr);owner.m_resultScoreLayer->onSkip(nullptr);owner.running=false;
    drainMainQueue();assert(!owner.m_resultScoreLayer&&owner.rendered==rendered&&mainQueue.empty());owner.running=true;
    match=settled("retired-rank");match.profileAfterA.displayedTier="AQUA";match.ratingEventA.promotionSucceeded=true;
    owner.beginResultScore(match,*owner.resultScoreInput(match));owner.m_rankTransitionLayer->keyBackClicked();owner.running=false;
    drainMainQueue();assert(!owner.m_rankTransitionLayer&&!owner.m_resultScorePending&&mainQueue.empty());owner.running=true;
    match=settled("covered");owner.beginResultScore(match,*owner.resultScoreInput(match));owner.running=false;
    drainMainQueue();assert(!owner.m_resultScorePending&&!owner.m_resultScoreLayer);drainMainQueue();
}
`);
    assert.match(transition,/m_chainToResult = chainToResult && m_type != RankTransitionType::Placement/);
    assert.match(transition,/if \(m_chainToResult\) unlockSkip\(\)/);
    assert.doesNotMatch(method(transition,'void RankTransitionLayer::revealFinalState('),/finishChainedReveal/);
    const result=method(popupSource,'void renderResult(');
    assert(result.indexOf('if (!scoreInput)')<result.indexOf('animateRankedScoreChange'));
    assert(result.indexOf('beginResultScore')<result.indexOf('enqueueAchievementToasts'));
});

test('actual Settings visibility and Jukebox callbacks honor screen ownership, missing mod and direct opening',()=>{
    run(scoreNative+runtimeDouble+`
template<class T>T typeinfo_cast(CCNode* n){return dynamic_cast<T>(n);}
namespace corum::ranked::ui {
${method(read('src/ui/RankedVisuals.hpp'),'inline void grayButtonVisual(')}
bool jukeboxLoaded=false;bool rankedJukeboxLoaded(){return jukeboxLoaded;}
struct RankedJukeboxPopup:CCNode {
    static inline int opened=0;static RankedJukeboxPopup* create(void*){return new RankedJukeboxPopup;}
    void show(){static CCNode scene;scene.addChild(this);}void openWhenReady(){++opened;}
};
}
struct CCTouchDispatcher {
    int releases=0;CCNode* released=nullptr;
    static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}
    void unregisterForcePrio(CCNode* owner){++releases;released=owner;}
};
struct FLAlertLayer:CCNode {
    inline static int opened=0,closed=0;std::string message;
    static FLAlertLayer* create(char const*,std::string text,char const*){auto* alert=new FLAlertLayer;alert->message=text;return alert;}
    void show(){++opened;static CCNode scene;scene.addChild(this);}
    void onBtn1(CCObject*){++closed;removeFromParentAndCleanup(true);}
};
using corum::ranked::RankedRuntime;using corum::ranked::RuntimeStage;
std::string tr(char const*,char const* english){return english;}
struct CorumRankedLayer:CCNode {
    enum class Page{Live,Achievements,HistoryList,HistoryDetail,PlacementPoolSelection,PlacementSkipConfirm};
    struct {bool moving()const{return false;}}m_modeTransition;
    Page m_page=Page::Live;std::string m_titleSelectorAchievement;bool m_modeNoticeOpen=false;
    CCNode* m_rankTransitionLayer=nullptr;CCNode* m_resultScoreLayer=nullptr;bool m_resultScorePending=false;
    CCMenu settings;CCMenu* m_settingsMenu=&settings;CCMenuItemSpriteExtra* m_jukeboxButton=nullptr;
    std::optional<WeakRef<FLAlertLayer>>m_jukeboxNotice;
    std::optional<WeakRef<corum::ranked::ui::RankedJukeboxPopup>>m_jukebox;
    bool mapReady=false;void* findPlayableMap(){return mapReady?this:nullptr;}
    CCMenuItemSpriteExtra* addButton(std::string,CCPoint,int,bool,float) {
        auto* face=CCNode::create();face->addChild(CCLayerColor::create({64,224,255,255}));
        face->addChild(new CCLabelBMFont);return CCMenuItemSpriteExtra::create(face,this,0);
    }
    void settingsVisibility(){${method(popupSource,'if (m_settingsMenu) {')}}
    ${['void addJukeboxButton(','void onJukebox(','void closeJukeboxNotice()'].map(name=>method(popupSource,name)).join('\n')}
};
int main() {
    CorumRankedLayer owner;auto& runtime=RankedRuntime::get();using P=CorumRankedLayer::Page;
    for(auto stage:{RuntimeStage::Idle,RuntimeStage::Ready,RuntimeStage::JoiningQueue,RuntimeStage::Queued,RuntimeStage::Matched,RuntimeStage::Error}) {
        runtime.m_view.stage=stage;
        for(auto page:{P::Live,P::Achievements,P::HistoryList,P::HistoryDetail,P::PlacementPoolSelection,P::PlacementSkipConfirm}) {
            owner.m_page=page;owner.settingsVisibility();
            auto allowed=page==P::Achievements||page==P::HistoryList||page==P::HistoryDetail||
                (page==P::Live&&(stage==RuntimeStage::Ready||stage==RuntimeStage::JoiningQueue||stage==RuntimeStage::Queued));
            assert(owner.settings.visible==allowed&&owner.settings.touch==allowed);
            owner.m_modeNoticeOpen=true;owner.settingsVisibility();assert(!owner.settings.visible&&!owner.settings.touch);
            owner.m_modeNoticeOpen=false;
            owner.m_resultScorePending=true;owner.settingsVisibility();assert(!owner.settings.visible&&!owner.settings.touch);
            owner.m_resultScorePending=false;owner.m_rankTransitionLayer=&owner;owner.settingsVisibility();assert(!owner.settings.visible);
            owner.m_rankTransitionLayer=nullptr;owner.m_resultScoreLayer=&owner;owner.settingsVisibility();assert(!owner.settings.visible);
            owner.m_resultScoreLayer=nullptr;
        }
    }
    owner.addJukeboxButton({0,0});assert(owner.m_jukeboxButton&&owner.m_jukeboxButton->enabled);
    auto* face=owner.m_jukeboxButton->getNormalImage();
    assert(face->children[0]->color.r==face->children[0]->color.g&&face->children[1]->color.r==176);
    owner.onJukebox(nullptr);owner.onJukebox(nullptr);assert(FLAlertLayer::opened==1);
    assert(owner.m_jukeboxNotice->lock()->message=="The Jukebox mod is required!");
    auto* notice=owner.m_jukeboxNotice->lock().data();owner.closeJukeboxNotice();owner.closeJukeboxNotice();
    assert(FLAlertLayer::closed==1&&CCTouchDispatcher::get()->releases==1&&CCTouchDispatcher::get()->released==notice);
    corum::ranked::ui::jukeboxLoaded=true;owner.addJukeboxButton({0,0});assert(!owner.m_jukeboxButton->enabled);
    owner.mapReady=true;owner.addJukeboxButton({0,0});assert(owner.m_jukeboxButton->enabled);
    owner.onJukebox(nullptr);owner.onJukebox(nullptr);
    assert(corum::ranked::ui::RankedJukeboxPopup::opened==1&&FLAlertLayer::opened==1);
}
`);
    assert.match(method(popupSource,'void addRoundOptions('),/addJukeboxButton\(\{size.width \/ 2.0f \+ 116.0f, y\}/);
    assert.match(method(popupSource,'void addPlayerCard('),/center.x \+ \(self \? -55 : 55\)/);
    const main=method(popupSource,'void renderModeCard(');
    assert.match(main,/leftCenter.y \+ 68.0f/);
    assert.match(main,/\{74.0f, 74.0f\}/);
    assert.match(main,/addRankStatus\(profile,\{leftCenter.x,leftCenter.y \+ 91.0f\}/);
    assert(18+74/2+1.4<68-5); // rank stays above the emblem with room for the Survival warning condition
});
