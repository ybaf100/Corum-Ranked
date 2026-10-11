import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
const source = read('src/RankedRuntime.cpp');
function method(marker) {
    const start = source.indexOf(marker);
    assert(start >= 0, marker);
    let end = source.indexOf('{', start) + 1, depth = 1;
    while (depth && end < source.length) {
        if (source[end] === '{') ++depth;
        if (source[end] === '}') --depth;
        ++end;
    }
    assert.equal(depth, 0, marker);
    return source.slice(start, end);
}

test('actual Runtime certificate GO retains Deathmatch identity, three starts, timestamp and raw score before PLAYING poll', () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const json = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'))
        .replace('bool isErr()const', 'bool isOk()const{return value.has_value();}\n    bool isErr()const');
    const header = read('src/RankedRuntime.hpp')
        .replace(/^#include <Geode.*$|^#pragma once$/gm, '').replaceAll('private:', 'public:');
    // Compile the real Runtime model and authority/journal/scoring bodies.
    // Only clock input and outbound transport are doubles; no fake Start/End
    // lifecycle can accidentally mirror the behavior being tested.
    const actual = [
        'bool RankedRuntime::isPlacement() const',
        'bool RankedRuntime::isSpectating() const',
        'bool RankedRuntime::canEnterNativeRoundScene() const',
        'bool RankedRuntime::nativeEntryStartGranted() const',
        'bool RankedRuntime::attemptStartWindowOpen() const',
        'bool RankedRuntime::armCurrentLevelForGameplay()',
        'bool RankedRuntime::canEnterCurrentLevel() const',
        'bool RankedRuntime::canTrackLevel(int',
        'bool RankedRuntime::canFinishTrackedLevel(int',
        'bool RankedRuntime::isGameplayLevel(int',
        'bool RankedRuntime::hasLocalAttemptInFlight() const',
        'bool RankedRuntime::hasLocalOpenAttempt() const',
        'bool RankedRuntime::hasLocalGameplayAuthority() const',
        'std::string RankedRuntime::currentAttemptContextKey() const',
        'double RankedRuntime::optimisticScoreForContext(',
        'int RankedRuntime::optimisticClearsForContext(',
        'int RankedRuntime::localDisplayClears() const',
        'double RankedRuntime::localDisplayScore(',
        'std::string RankedRuntime::newEventId(',
        'bool RankedRuntime::reportAttemptStart(int',
        'bool RankedRuntime::reportAttemptEnd(int',
        'void RankedRuntime::recordDeath(',
        'void RankedRuntime::retireDecisiveAttempt(',
    ].map(method).join('\n');
    const cpp = `
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "${join(root, 'src/domain/RoundResourcePolicy.hpp')}"
#include "${join(root, 'src/domain/SpectatorLocalSafety.hpp')}"
#include "${join(root, 'src/domain/ServerClock.cpp')}"
#include "${join(root, 'src/domain/AttemptScoring.cpp')}"
${json}
namespace geode::utils::web { struct WebResponse {}; }
namespace geode::async { template<class T> struct TaskHolder { void cancel() {} }; }
namespace fmt {
template<class T>std::string text(T const& value){std::ostringstream s;s<<value;return s.str();}
template<class... T>std::string format(std::string s,T const&... values){
    auto put=[&](auto const& value){auto at=s.find("{}");if(at!=std::string::npos)s.replace(at,2,text(value));};
    (put(values),...);return s;
}
}
${header}
std::int64_t fakeNow=0;
std::int64_t localNowMillis(){return fakeNow;}
${method('std::string iso8601FromMillis(')}
bool stateAllowsActiveAttempt(std::string const& state){
    return state=="ROUND_PLAYING"||state=="DEATHMATCH_PLAYING"||state=="ROUND_SETTLING";
}
int intentCalls=0,flushCalls=0;
namespace corum::ranked {
namespace log { template<class... T>void debug(T...){}template<class... T>void warn(T...){} }
std::optional<std::int64_t> RankedRuntime::signedDeadlineMillis()const{return std::nullopt;}
std::optional<std::int64_t> RankedRuntime::deadlineMillis()const{return std::nullopt;}
void RankedRuntime::sendAttemptStartIntent(PendingStart const&){++intentCalls;}
void RankedRuntime::flushAttemptEvents(){++flushCalls;}
void RankedRuntime::invalidateAttemptRequest(){++m_attemptRequestGeneration;m_attemptRequest.cancel();m_attemptBusy=false;}
${actual}
}
using namespace corum::ranked;
void prepare(RankedRuntime& r,bool deathmatch=true){
    r.m_view.stage=RuntimeStage::Matched;
    r.m_view.match.matchId="match";r.m_view.match.side="A";
    r.m_view.match.state=deathmatch?"DEATHMATCH_ENTERING":"ROUND_ENTERING";
    r.m_view.match.roundNumber=deathmatch?0:1;
    r.m_view.match.deathmatchSequence=deathmatch?4:0;
    RankedMapView map;map.levelId=123;map.qualifyingPercent=90;
    r.m_view.match.currentMap=map;
    auto& e=r.m_view.match.entry;e.roundNumber=r.m_view.match.roundNumber;
    e.deathmatchSequence=r.m_view.match.deathmatchSequence;e.levelId=123;
    e.enteredA=true;e.enteredB=true;e.startsAt="2026-10-07T02:00:03.000Z";
    assert(r.m_serverClock.observe("2026-10-07T02:00:00.000Z",0));
}
int main(){
    RankedRuntime r;prepare(r);intentCalls=flushCalls=0;fakeNow=0;
    // The known shared timestamp alone does not grant gameplay before GO.
    assert(!r.nativeEntryStartGranted()&&!r.canEnterCurrentLevel());
    assert(!r.armCurrentLevelForGameplay()&&!r.reportAttemptStart(123));
    fakeNow=2999;assert(!r.reportAttemptStart(123));
    assert(!r.m_pendingStart&&r.m_attemptBacklog.empty()&&r.m_localDeathmatchVisualAttempts==0);
    assert(intentCalls==0&&flushCalls==0);
    fakeNow=3000;r.m_view.match.entry.enteredB=false;
    assert(!r.nativeEntryStartGranted()&&!r.reportAttemptStart(123));
    r.m_view.match.entry.enteredB=true;
    ++r.m_view.match.entry.deathmatchSequence;
    assert(!r.nativeEntryStartGranted()&&!r.reportAttemptStart(123));
    --r.m_view.match.entry.deathmatchSequence;
    assert(r.nativeEntryStartGranted()&&r.canEnterCurrentLevel());
    assert(r.armCurrentLevelForGameplay());
    assert(r.m_gameplayStartedAt==r.m_view.match.entry.startsAt);
    assert(r.reportAttemptStart(123));
    assert(r.m_pendingStart&&r.m_pendingStart->contextKey=="match:D:4");
    assert(r.m_pendingStart->clientStartedAt=="2026-10-07T02:00:03.000Z");
    assert(r.m_localDeathmatchSequence==4&&r.m_localDeathmatchVisualAttempts==1&&intentCalls==0);
    // Repeated frame callbacks do not consume additional visual attempts.
    assert(r.reportAttemptStart(123));assert(r.m_localDeathmatchVisualAttempts==1);
    assert(r.localDisplayScore(1.0)==1.0); // Qualifying does not apply in DM.
    r.m_view.match.scoreA=72;assert(r.localDisplayScore(20.0)==72.0);
    assert(r.localDisplayScore(99.0)==99.0);
    // Rapid death/reset before HTTP ACK retains all three Starts in FIFO,
    // despite the cached state still being DEATHMATCH_ENTERING.
    fakeNow=3100;assert(r.reportAttemptEnd(123,25,true));
    assert(r.m_pendingEnd&&r.m_pendingEnd->optimisticScore==100.0);
    assert(r.reportAttemptEnd(123,25,true));
    assert(r.canEnterCurrentLevel()&&r.reportAttemptStart(123));
    assert(r.m_localDeathmatchVisualAttempts==2&&r.m_attemptBacklog.size()==1);
    assert(r.m_attemptBacklog.back().start.clientStartedAt=="2026-10-07T02:00:03.100Z");
    fakeNow=3200;assert(r.reportAttemptEnd(123,42,false));
    assert(r.m_attemptBacklog.back().end->optimisticScore==42.0);
    assert(r.canEnterCurrentLevel()&&r.reportAttemptStart(123));
    assert(r.m_localDeathmatchVisualAttempts==3&&r.m_attemptBacklog.size()==2);
    assert(!r.canEnterCurrentLevel()&&!r.reportAttemptStart(123));
    assert(r.m_localDeathmatchVisualAttempts==3&&r.m_attemptBacklog.size()==2&&intentCalls==0);
    // An already-journaled final Attempt can End after the short certificate
    // expires; the bounded grant cannot authorize another new Start.
    fakeNow=6001;assert(!r.nativeEntryStartGranted());
    assert(r.reportAttemptEnd(123,85,false));
    assert(r.m_attemptBacklog.back().end->optimisticScore==85.0);
    assert(!r.reportAttemptStart(123)&&intentCalls==0);
    assert(r.m_view.match.state=="DEATHMATCH_ENTERING");

    // Authoritative server usage and reused maps do not refill the DM cap.
    RankedRuntime used;prepare(used);fakeNow=3000;
    used.m_view.match.deathmatchAttemptsUsedA=2;
    assert(used.canEnterCurrentLevel()&&used.reportAttemptStart(123));
    assert(used.m_localDeathmatchVisualAttempts==3&&!used.canEnterCurrentLevel());
    assert(used.reportAttemptEnd(123,20,false)&&!used.reportAttemptStart(123));

    RankedRuntime sideB;prepare(sideB);sideB.m_view.match.side="B";
    sideB.m_view.match.deathmatchAttemptsUsedB=3;
    assert(!sideB.canEnterCurrentLevel()&&!sideB.reportAttemptStart(123));
    assert(!sideB.m_pendingStart&&sideB.m_attemptBacklog.empty());

    // A delayed frame grants GO without shifting the shared first Start time.
    RankedRuntime late;prepare(late);fakeNow=3350;
    assert(late.armCurrentLevelForGameplay()&&late.reportAttemptStart(123));
    assert(late.m_pendingStart->clientStartedAt=="2026-10-07T02:00:03.000Z");
    assert(late.m_view.match.state=="DEATHMATCH_ENTERING"&&intentCalls==0);

    // Standard Round entry still sends its lease and uses the existing score.
    RankedRuntime round;prepare(round,false);fakeNow=3000;
    assert(round.armCurrentLevelForGameplay()&&round.reportAttemptStart(123));
    assert(intentCalls==1&&round.m_localDeathmatchVisualAttempts==0);
    assert(round.m_pendingStart->contextKey=="match:R:1");
    assert(round.reportAttemptEnd(123,100,true));
    assert(round.m_pendingEnd->optimisticScore==250.0);

    // Own feed records exactly one real native death, never a Clear/duplicate,
    // and decisive server retirement leaves no journal blocking the next round.
    RankedRuntime died;prepare(died,false);fakeNow=3000;
    assert(died.armCurrentLevelForGameplay()&&died.reportAttemptStart(123));
    assert(died.reportAttemptEnd(123,42,false,std::nullopt,true));
    assert(died.m_pendingEnd->nativeDeath&&died.m_view.match.deathLog.size()==1);
    assert(died.m_view.match.deathLog[0].side=="A"&&died.m_view.match.deathLog[0].progressPercent==42);
    assert(died.reportAttemptEnd(123,42,false,std::nullopt,true)&&died.m_view.match.deathLog.size()==1);
    died.retireDecisiveAttempt();
    assert(!died.m_pendingStart&&!died.m_pendingEnd&&died.m_attemptBacklog.empty()&&!died.m_gameplayStartEligible);

    // Exact scene spectator firewall still wins over the entry certificate.
    RankedRuntime blocked;prepare(blocked);int owner=0;
    blocked.m_spectatorLocalTransportOwner=&owner;
    assert(!blocked.reportAttemptStart(123)&&!blocked.m_pendingStart);
    assert(blocked.m_localDeathmatchVisualAttempts==0&&intentCalls==2);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-entry-deathmatch-'));
    try {
        const file = join(dir, 'test.cpp');
        writeFileSync(file, cpp);
        for (const platform of ['GEODE_IS_WINDOWS', 'GEODE_IS_IOS']) {
            const binary = join(dir, platform);
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', '-pedantic',
                `-D${platform}=1`, '-I', join(root, 'src'), file, '-o', binary], { stdio: 'pipe' });
            execFileSync(binary, [], { stdio: 'pipe' });
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
