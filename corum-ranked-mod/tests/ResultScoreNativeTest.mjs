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
ccColor3B tierColor(std::string const& tier) {
    if(tier=="RED")return {244,76,91};
    if(tier=="GOLD")return Gold;
    return {55,220,234};
}
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
    score->update(1); assert(score->m_complete&&!score->scheduled&&hasLabel(score,"PROMOTION MATCH"));
    assert(score->m_seriesRoot->children.back()->children.size()==3&&completions==0&&score->visible);
    score->update(99); assert(completions==0&&score->m_score->text=="125 LP");
    score->keyBackClicked(); assert(completions==1); score->onSkip(nullptr); assert(completions==1);
    i=ordinary(); i.after=i.before; i.demotionWarning=true;
    auto* warning=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(warning); warning->onEnter();
    assert(warning->m_delta->text=="+0 LP"&&warning->m_delta->color==TextMuted);
    assert(!hasLabel(warning,"DEMOTION WARNING")); warning->update(2);
    assert(hasLabel(warning,"DEMOTION WARNING")); warning->onSkip(nullptr);
    i=ordinary(); i.promotionInProgress=true; i.previousMarks=1;
    i.promotion={true,"RED","AQUA",300,10,5,{"WIN","LOSS"}};
    auto* series=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(series); series->onEnter();
    assert(hasLabel(series,"PROMOTION MATCH"));
    auto* grid=series->m_seriesRoot->children.back(); assert(grid->children.size()==10&&grid->scaleX>0);
    assert(!grid->children[0]->actions.empty()); // gray square slams when it appears
    assert(grid->children[0]->children[0]->actions.empty()); // previous check remains
    assert(!grid->children[1]->children[0]->actions.empty()); // latest X stamps
    assert(grid->children[1]->children[0]->children.size()==2); // a real X, not a delete/trash icon
    series->onSkip(nullptr);
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
    assert(promotion->m_fill->color==tierColor("AQUA"));
    promotion->onSkip(nullptr);
    i=ordinary(); i.beforeTier="AQUA"; i.afterTier="RED"; i.before=300; i.after=250; i.demoted=true;
    auto* demotion=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(demotion);
    oldLeft=demotion->m_leftIcon; oldRight=demotion->m_rightIcon; demotion->update(2);
    assert(demotion->m_tier=="RED"&&demotion->m_leftIcon!=oldLeft&&demotion->m_rightIcon!=oldRight);
    for(auto* part:oldLeft->children)assert(part->actions[0]->type=="spawn"); // falling parts, not shrink
    assert(demotion->m_leftIcon->scaleX==0.25f); demotion->onSkip(nullptr);
    i=ordinary(); i.beforeTier=i.afterTier="GOLD"; i.before=1225; i.after=1250;
    i.standingAfter.rank=5; i.standingAfter.total=10; i.standingAfter.topPercent=50;
    i.standingAfter.target=StandingTargetView{123,"bot",true,4,1275};
    auto* gold=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(gold);
    assert(PlayerIcon::accounts.back()==0&&gold->m_targetKey=="gold:123:4:1275");
    gold->update(2); assert(PlayerIcon::accounts.back()==0&&gold->m_rightCutoff->text=="1275 LP");
    gold->onSkip(nullptr);
    i.standingAfter.rank=1; i.standingAfter.target.reset();
    auto* leader=ResultScoreLayer::create(i,&screen,[]{}); screen.addChild(leader);
    assert(PlayerIcon::accounts.back()==99&&leader->m_targetKey=="gold-self");
    assert(leader->m_fill->size.width==leader->m_barWidth); leader->onSkip(nullptr);
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

test('font package contains the new Korean notices and unplaced em dash without changing countdown glyphs',()=>{
    const fonts=JSON.parse(read('mod.json')).resources.fonts;
    const required='—승급했습니다!다음 경기에서 패배하면 강등될 수 있습니다.Jukebox 모드가 필요합니다!';
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
${['std::vector<std::string> parseRatingMarks(','corum::ranked::StandingView parseStanding(',
    'corum::ranked::ScoreSeriesView parseScoreSeries(','corum::ranked::ResultScoreView parseResultScore(']
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

test('actual decoders and standings request reject stale sessions and validate result metadata',()=>{
    run(scoreNative+runtimeDouble+`
int main() {
    using V=matjson::Value;V standing;standing["rank"]=2;standing["total"]=100;standing["topPercent"]=99;
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
    CCMenu controls;CCMenu* m_controlMenu=&controls;std::function<void()>m_completion;
    void revealFinalState(bool){m_finalShown=true;}
    static RankTransitionLayer* create(std::string,std::string,RankTransitionType type,std::string,std::string,std::string,
        std::function<void()>callback,bool chain) {
        auto* layer=new RankTransitionLayer;layer->m_completion=std::move(callback);
        layer->m_chainToResult=chain&&type!=RankTransitionType::Placement;layer->m_skipAllowed=layer->m_chainToResult;return layer;
    }
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
    Ref keepScore(score);score->onEnter();score->keyBackClicked();
    assert(!owner.m_resultScoreLayer&&!owner.keypad&&!owner.menu.touch&&owner.rendered==1);
    drainMainQueue();assert(owner.keypad&&owner.menu.touch&&owner.toasts==1);
    owner.beginResultScore(match,*input);assert(mainQueue.empty()&&!owner.m_resultScoreLayer);
    match=settled("promotion");match.profileBeforeA.visibleRankedScore=299;match.profileAfterA.visibleRankedScore=350;
    match.profileAfterA.displayedTier="AQUA";match.ratingEventA.promotionSucceeded=true;
    owner.beginResultScore(match,*owner.resultScoreInput(match));
    auto* rank=owner.m_rankTransitionLayer;assert(rank&&!owner.m_resultScorePending&&!owner.keypad);
    Ref keepRank(rank);rank->keyBackClicked();
    assert(rank->m_finished&&!rank->parent&&!owner.m_rankTransitionLayer&&owner.m_resultScorePending);
    assert(!owner.m_resultScoreLayer&&!owner.keypad);rank->keyBackClicked();assert(mainQueue.size()==1);
    drainMainQueue();score=owner.m_resultScoreLayer;assert(score);score->onSkip(nullptr);
    drainMainQueue();assert(owner.keypad&&owner.menu.touch);
    match=settled("demotion");match.profileBeforeA.displayedTier="AQUA";match.profileBeforeA.visibleRankedScore=300;
    match.profileAfterA.visibleRankedScore=250;match.ratingEventA.demoted=true;
    owner.beginResultScore(match,*owner.resultScoreInput(match));rank=owner.m_rankTransitionLayer;
    rank->finishChainedReveal();assert(owner.m_resultScorePending);drainMainQueue();assert(owner.m_resultScoreLayer);
    owner.m_resultScoreLayer->onSkip(nullptr);drainMainQueue();
    RankTransitionLayer placed;placed.m_chainToResult=false;placed.keyBackClicked();assert(!placed.m_finalShown&&!placed.m_finished);
    placed.m_skipAllowed=true;placed.keyBackClicked();assert(placed.m_finalShown&&!placed.m_finished);
    match=settled("covered");owner.beginResultScore(match,*owner.resultScoreInput(match));owner.running=false;
    drainMainQueue();assert(!owner.m_resultScorePending&&!owner.m_resultScoreLayer);drainMainQueue();
}
`);
    assert.match(transition,/m_chainToResult = chainToResult && m_type != RankTransitionType::Placement/);
    assert.match(transition,/if \(m_chainToResult\) unlockSkip\(\)/);
    assert.match(method(transition,'void RankTransitionLayer::revealFinalState('),/if \(m_chainToResult\) \{ finishChainedReveal\(\); return; \}/);
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
    Page m_page=Page::Live;std::string m_titleSelectorAchievement;
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
    const main=method(popupSource,'void renderMain(');
    assert.match(main,/leftCenter.y \+ 92.0f/);
    assert.match(main,/\{64.0f, 64.0f\}/);
    assert(64*1.72*Math.SQRT2/2<84); // every rotating GOLD corner is below the common rank slot
});
