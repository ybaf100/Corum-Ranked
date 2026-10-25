import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const source=readFileSync(join(root,'src/RankedRuntime.cpp'),'utf8');
function method(name){let a=source.indexOf(name);assert(a>=0);let end=source.indexOf('{',a)+1,n=1;while(n){n+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(a,end).replace('RankedRuntime::','');}
test('production ACK projection/round-version fence and spectator entry wait for the real End FIFO',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;assert(json&&result);
 const parse=method('void RankedRuntime::parseMatchState(');const fence=parse.slice(parse.indexOf('{')+1,parse.indexOf('    observeServerNow(root);'));
 const cpp=`#include <matjson.hpp>
#include "domain/SpectatorHandoffPolicy.hpp"
#include "domain/RankedGameplayPolicy.hpp"
#include <cassert>
#include <string>
#include <optional>
#include <algorithm>
using namespace corum::ranked;
namespace log {template<class...T>void debug(T...){}}
enum class RuntimeStage{Matched,Ready};
struct Runtime{
 struct Match{std::string matchId="m",state="ROUND_PLAYING",side="A",opponentName="B",spectatorOpponentName,deadlineAt="new";int roundNumber=1,deathmatchSequence=0;bool placement=false,spectatorActive=false,spectatorLiveEnabled=true;
 // Model Apple's int64_t (long long) versus matjson's long on this Linux runner.
 std::optional<int>currentMap=42,spectatorCurrentProgress;long long roundStateVersion=0;std::int64_t stateVersion=0;double committedScoreA=0,committedScoreB=0,scoreA=0,scoreB=0;
 int clearsA=0,clearsB=0,deathmatchAttemptsUsedA=0,deathmatchAttemptsUsedB=0,deathmatchAttemptsCompletedA=0,deathmatchAttemptsCompletedB=0;}match;
 struct View{RuntimeStage stage=RuntimeStage::Matched;Match match;}m_view;
 int m_localDeathmatchSequence=0,m_localDeathmatchVisualAttempts=0,pollsAccepted=0;bool pending=true;
 bool hasLocalAttemptInFlight()const{return pending;}bool isSpectating()const{return m_view.match.spectatorActive;}
 void setTransientError(std::string){}void pollFence(matjson::Value const&root){${fence}++pollsAccepted;}
 ${method('void RankedRuntime::applyAttemptSnapshot(')}
 ${method('bool RankedRuntime::canEnterCurrentLevelAsSpectator() const')}
};
int main(){Runtime r;matjson::Value ack;ack["deadlineAt"]="ack";ack["roundSnapshot"]["roundNumber"]=1;ack["roundSnapshot"]["stateVersion"]=5;
 ack["roundSnapshot"]["phase"]="LAST_ATTEMPT_WINDOW";ack["roundSnapshot"]["spectatorActive"]=true;
 r.applyAttemptSnapshot(ack);assert(r.m_view.match.spectatorActive&&!r.canEnterCurrentLevelAsSpectator());
 r.pending=false;assert(r.canEnterCurrentLevelAsSpectator());r.applyAttemptSnapshot(ack);assert(r.canEnterCurrentLevelAsSpectator());
 auto stale=ack;stale["deadlineAt"]="old";stale["roundSnapshot"]["stateVersion"]=4;stale["roundSnapshot"]["spectatorActive"]=false;
 r.applyAttemptSnapshot(stale);assert(r.m_view.match.spectatorActive&&r.m_view.match.deadlineAt=="ack");
 stale["roundSnapshot"]["stateVersion"]=6;stale["roundSnapshot"]["phase"]="ROUND_PLAYING";r.applyAttemptSnapshot(stale);assert(r.m_view.match.spectatorActive&&r.m_view.match.state=="LAST_ATTEMPT_WINDOW");
 stale["roundSnapshot"]["roundNumber"]=2;r.applyAttemptSnapshot(stale);assert(r.m_view.match.spectatorActive);
 matjson::Value poll;poll["matchId"]="m";poll["stateVersion"]=1;poll["currentRound"]["roundNumber"]=1;poll["currentRound"]["stateVersion"]=4;
 r.pollFence(poll);assert(r.pollsAccepted==0);poll["currentRound"]["stateVersion"]=5;r.pollFence(poll);assert(r.pollsAccepted==1);
 Runtime missing;ack["roundSnapshot"].erase("spectatorActive");ack["roundSnapshot"]["lastAttemptWindow"]["triggerSide"]="A";missing.applyAttemptSnapshot(ack);assert(!missing.m_view.match.spectatorActive);
 r.m_view.match.spectatorActive=false;assert(!r.canEnterCurrentLevelAsSpectator());r.m_view.match.spectatorActive=true;r.m_view.match.spectatorLiveEnabled=false;assert(!r.canEnterCurrentLevelAsSpectator());
}`;
 const d=mkdtempSync(join(tmpdir(),'corum-handoff-'));try{writeFileSync(join(d,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-O0','-I',join(root,'src'),'-I',join(json,'include'),'-I',result,join(d,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(d,'test')],{stdio:'pipe'});execFileSync(join(d,'test'),[],{stdio:'pipe'});}catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(d,{recursive:true,force:true});}
});
