import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const pause=read('src/RankedPauseLayer.cpp');
const popup=read('src/ui/RankedSurrenderPopup.hpp');
const visuals=read('src/ui/RankedVisuals.hpp');
const body=(source,name)=>{const at=source.indexOf(name);assert(at>=0,name);const open=source.indexOf('{',at);let end=open+1,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(at,end);};

test('packaged Corum font charsets cover Korean countdown, pause and surrender labels',()=>{
    const fonts=JSON.parse(read('mod.json')).resources.fonts;
    const captions=read('src/ui/RankedTranslations.hpp')+read('src/ui/RankedEntryOverlay.cpp')+pause;
    const glyphs=new Set([...captions].map(c=>c.codePointAt(0)).filter(n=>n>=0xac00&&n<=0xd7a3));
    for(const weight of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
        const available=new Set();
        for(const item of fonts[weight].charset.split(',')){
            const [first,last]=item.split('-').map(Number);
            for(let n=first;n<=(last??first);++n)available.add(n);
        }
        for(const n of glyphs)assert(available.has(n), `${weight} missing ${String.fromCodePoint(n)}`);
    }
});

test('pause surrender uses native GD X sprite and Corum modal/font, never vanilla alert',()=>{
    assert.match(pause,/CCSprite::createWithSpriteFrameName\("GJ_closeBtn_001\.png"\)/);
    assert.doesNotMatch(pause,/surrenderFlag|drawPolygon|CircleButtonSprite/);
    assert.match(pause,/getChildByIDRecursive\("center-button-menu"\)/);
    assert.match(pause,/exit->setEnabled\(false\)/);
    assert.match(pause,/exit->setTarget\(this, menu_selector\(CorumRankedPauseLayer::onBlockedQuit\)\)/);
    assert.match(pause,/ui::label\([\s\S]*FontWeight::ExtraBold/);
    assert.match(popup,/FontWeight::Black/);
    assert.match(popup,/surface\(\{370, 235\}/);
    assert.match(popup,/i18n::text\("surrender-round"/);
    assert.match(popup,/i18n::text\("surrender-match"/);
    assert.match(popup,/WeakRef<PauseLayer>/);
    assert.doesNotMatch(pause+popup,/createQuickPopup|bigFont\.fnt|goldFont\.fnt/);
    const start=pause.indexOf('m_fields->leaseId = ranked.beginMatchGameplayPause');
    const end=pause.indexOf('auto* center',start);
    assert.doesNotMatch(pause.slice(start,end),/leaseId\.empty\(\).*return/);
});

test('actual pause/modal callbacks block only owned manual quit, send exact scope once and close before timeout resume',()=>{
    const modalMethods=['bool sameRound() const','void onRound(','void onMatch(','void onConfirm(','void onCancel(','void watchContext(','void dismiss()'].map(n=>body(popup,n)).join('\n');
    const cpp=`
#include <cassert>
#include <optional>
#include <string>
#include <cmath>
#include "${join(root, 'src/domain/FontMetrics.hpp')}"
struct CCObject {};
struct AppDelegate {bool m_gamePaused=false;static AppDelegate* get(){static AppDelegate app;return &app;}};
namespace geode::log {template<class...T>void info(T...) {}}
struct Node { bool parent=true;void* getParent()const{return parent?(void*)this:nullptr;} };
struct Label {std::string text;void setString(char const* s){text=s;}void limitLabelWidth(float,float,float){};char const* getFntFile()const{return "CorumSansBold.fnt";}};
struct PauseLayer:Node {int quits=0,resumes=0;bool dispatching=false;void onQuit(CCObject*){++quits;}void onResume(CCObject*);};
struct Level{int m_levelID=123;};
struct PlayLayer{Level* m_level=nullptr;inline static PlayLayer* active=nullptr;bool m_isPaused=false;int quits=0;static PlayLayer* get(){return active;}void onQuit(){++quits;}void resume(){m_isPaused=false;AppDelegate::get()->m_gamePaused=false;}};
template<class T>struct Ref {T* p=nullptr;Ref()=default;explicit Ref(T* v):p(v){}T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
 template<class T>struct WeakRef{T* value=nullptr;explicit WeakRef(T* p):value(p){}Ref<T> lock()const{return Ref<T>(value);}};
 namespace geode {template<class T>using WeakRef=::WeakRef<T>;}
 std::optional<geode::WeakRef<PauseLayer>> activeRankedPause;
 namespace fmt {template<class...T>std::string format(char const*,T...){return "budget";}}
struct Popup{int closes=0;void onClose(CCObject*){++closes;}};
#define schedule_selector(x) 0
namespace corum::ranked {
enum class RuntimeStage{Idle,Matched};
struct RankedRuntime {
 enum class SurrenderStatus{Idle,WaitingAttempt,Sending,Retrying,Accepted,Rejected};
 struct Match{std::string matchId="m";int roundNumber=1,deathmatchSequence=0;};
 struct View{RuntimeStage stage=RuntimeStage::Idle;Match match;}v;
 bool placement=false,spectator=false,conceded=false,whole=false;int sends=0,ends=0;double remaining=30000;
 SurrenderStatus status=SurrenderStatus::Idle;
 static RankedRuntime& get(){static RankedRuntime r;return r;}
 void tick(){}auto const& view()const{return v;}bool isPlacement()const{return placement;}bool isSpectating()const{return spectator;}
 bool isGameplayLevel(int id)const{return id==123;}double matchPauseRemainingMs()const{return remaining;}
 bool gameplayConceded(int,int)const{return conceded;}SurrenderStatus surrenderStatus()const{return status;}
 void surrenderCurrentMatch(bool value){whole=value;++sends;status=SurrenderStatus::Sending;}
 void endMatchGameplayPause(std::string const&){++ends;}
};
int returnRequests=0;bool requestConcededGameplayReturn(PlayLayer*){++returnRequests;return true;}
 namespace i18n {inline bool korean=false;bool isKorean(){return korean;}std::string text(char const* key,char const* fallback){return korean?std::string("ko:")+key:fallback;}}
namespace ui {
using CCLabelBMFont=Label;
${body(visuals,'inline float fontScale(')}
${body(visuals,'inline void fitLabelWidth(')}
struct RankedSurrenderPopup:Popup {
 std::optional<WeakRef<PauseLayer>> m_owner;std::string m_matchId="m";int m_round=1,m_deathmatch=0;
 bool m_wholeMatch=false,m_confirmation=false,m_submitted=false,m_closed=false,m_showSubmissionStatus=false;Label label;Label* m_status=&label;int renders=0;
 void render(){++renders;}void unschedule(int){}
 ${modalMethods}
};
}
}
using corum::ranked::RankedRuntime;
namespace ui=corum::ranked::ui;
namespace i18n=corum::ranked::i18n;
${body(pause,'bool ownsRankedPlayerPause()')}
void dismissNativeRankedExitConfirmation(PauseLayer*){};
struct CorumRankedPauseLayer:PauseLayer {
 struct Button {bool enabled=true;void setEnabled(bool v){enabled=v;}} button;Label clock;
 struct Fields{std::optional<WeakRef<ui::RankedSurrenderPopup>> popup;std::string leaseId="lease",matchId="m";int round=1,deathmatch=0;Label* budget;Button* surrender;std::optional<WeakRef<PlayLayer>> owner;bool concessionQuit=false,nativeQuitInProgress=false,nativeQuitReachedPlay=false;std::optional<WeakRef<ui::RankedSurrenderPopup>> exitConfirmation{};}f{{},"lease","m",1,0,&clock,&button,{}};Fields* m_fields=&f;
 int exitWarnings=0;void confirmRankedExit(){++exitWarnings;}
 void unschedule(int){}
 ${body(pause,'void onQuit(CCObject* sender)')}
 ${body(pause,'void watchBudget(float)')}
 ${body(pause,'void retireRankedPause()')}
 ${body(pause,'void releaseRankedPauseOwnership()')}
 ${body(pause,'void onResume(CCObject* sender)')}
 ${body(pause,'void resumeRankedPause(CCObject* sender)')}
};
// Model the binding entering the Corum hook, then its next-original call.
void PauseLayer::onResume(CCObject* sender){if(dispatching){++resumes;return;}dispatching=true;static_cast<CorumRankedPauseLayer*>(this)->onResume(sender);dispatching=false;}
int main(){using namespace corum::ranked;
 Level level;PlayLayer play;play.m_level=&level;PlayLayer::active=&play;
 auto& r=RankedRuntime::get();CorumRankedPauseLayer p;p.f.owner.emplace(&play);activeRankedPause.emplace(&p);p.onQuit(nullptr);assert(p.quits==1);
 r.v.stage=RuntimeStage::Matched;p.onQuit(nullptr);assert(p.quits==1);
 r.spectator=true;p.onQuit(nullptr);assert(p.quits==2);r.spectator=false;
 r.placement=true;p.onQuit(nullptr);assert(p.quits==3);r.placement=false;
 level.m_levelID=999;p.onQuit(nullptr);assert(p.quits==4);level.m_levelID=123;
 play.onQuit();assert(play.quits==1); // automatic PlayLayer route untouched
 ui::RankedSurrenderPopup d;d.m_owner.emplace(&p);p.f.popup.emplace(&d);
 d.onRound(nullptr);assert(d.m_confirmation&&!d.m_wholeMatch&&d.renders==1);
 d.onConfirm(nullptr);assert(r.sends==1&&!r.whole&&d.label.text=="Sending surrender...");
 d.onConfirm(nullptr);assert(r.sends==1);
 i18n::korean=true;r.status=RankedRuntime::SurrenderStatus::Retrying;d.watchContext(0);
 assert(d.label.text=="ko:surrender-retrying");
 r.status=RankedRuntime::SurrenderStatus::Rejected;d.watchContext(0);assert(!d.m_submitted);
 d.watchContext(0);assert(d.label.text=="ko:surrender-rejected"); // failure remains visible until retry
 d.onMatch(nullptr);d.onConfirm(nullptr);assert(r.sends==2&&r.whole);
 r.status=RankedRuntime::SurrenderStatus::WaitingAttempt;d.watchContext(0);assert(d.label.text=="ko:surrender-waiting-attempt");
 r.remaining=0;p.resumeRankedPause(nullptr);assert(d.closes==1&&p.resumes==1&&r.ends==1);
 assert(!d.m_owner&&!p.f.popup&&p.f.leaseId.empty());
 d.watchContext(0);d.dismiss();assert(d.closes==1); // idempotent close
 ui::RankedSurrenderPopup late;late.m_owner.emplace(&p);r.remaining=30000;r.v.match.roundNumber=2;
 late.onConfirm(nullptr);assert(r.sends==2);late.watchContext(0);assert(late.closes==1);
 r.v.match.roundNumber=1;ui::RankedSurrenderPopup gone;gone.m_owner.emplace(&p);p.parent=false;
 gone.watchContext(0);assert(gone.closes==1); // no raw owner callback after scene retirement
 p.parent=true;r.conceded=true;p.f.leaseId="second";p.f.owner.emplace(&play);p.watchBudget(0);
 assert(p.resumes==1&&returnRequests==1&&p.f.leaseId=="second"); // keep native Pause for its deferred Quit
 r.conceded=false;p.f.leaseId="third";r.remaining=30000;r.v.match.roundNumber=2;p.f.round=2;
 p.watchBudget(0);assert(p.resumes==1&&returnRequests==1); // new round is not conceded
 r.remaining=0;p.watchBudget(0);assert(p.resumes==2&&returnRequests==1); // timeout resumes, never concedes
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-pause-native-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});

// Geode 5.10.1 Popup::init registers force-priority, but Popup::~Popup is its
// only cleanup. Model delayed destruction by keeping a closed modal alive.
// Compile Corum's actual onExit, so it must release ONLY that owner promptly.
test('closed retained surrender modal releases force priority before the next round',()=>{
    const exit=body(popup,'void onExit() override');
    const cpp=`
#include <cassert>
#include <optional>
#include <set>
struct CCObject {virtual ~CCObject()=default;};
struct CCTouchDispatcher {
 std::set<CCObject*> forceOwners;
 static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}
 void unregisterForcePrio(CCObject* o){forceOwners.erase(o);}
};
struct Popup:CCObject {
 bool parent=true;int exits=0;
 Popup(){CCTouchDispatcher::get()->forceOwners.insert(this);}
 virtual ~Popup(){CCTouchDispatcher::get()->unregisterForcePrio(this);}
 virtual void onExit(){parent=false;++exits;}
};
#define schedule_selector(x) 0
struct RankedSurrenderPopup:Popup {bool m_closed=false;std::optional<int> m_owner{1};void unschedule(int){} ${exit}};
int main(){auto* d=CCTouchDispatcher::get();Popup external;
 for(int round=1;round<=3;++round){
   Popup old;old.onExit();assert(d->forceOwners.contains(&old)); // old lifecycle leak until destruction
   {RankedSurrenderPopup modal;
    assert(d->forceOwners.contains(&modal));modal.onExit();
    assert(!d->forceOwners.contains(&modal)&&!modal.parent&&modal.exits==1&&!modal.m_owner);
    assert(d->forceOwners.contains(&external)&&d->forceOwners.contains(&old));
    modal.onExit();assert(!d->forceOwners.contains(&modal));}
   assert(d->forceOwners.contains(&external));
 }
 assert(d->forceOwners.size()==1);
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-modal-priority-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
        execFileSync(join(dir,'test'),[],{stdio:'pipe'});
    }finally{rmSync(dir,{recursive:true,force:true});}
});
