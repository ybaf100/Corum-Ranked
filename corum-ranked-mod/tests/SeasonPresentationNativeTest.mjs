import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');
function run(source){const dir=mkdtempSync(join(tmpdir(),'season-'));try{
 writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('season countdown uses absolute instants: gold days, red clock, muted start, hidden past start',()=>run(`
#include "domain/SeasonPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 SeasonView s;s.known=true;s.preseason=false;s.number=2;std::int64_t const now=1'700'000'000'000;
 assert(seasonCountdown(s,true,now).kind==SeasonCountdownKind::Hidden);
 s.targetAtMs=now+(3LL*86400+5*3600+120)*1000;
 auto a=seasonCountdown(s,true,now);assert(a.kind==SeasonCountdownKind::LeftDays&&a.tone==SeasonTone::Gold&&a.days==3&&a.hours==5);
 auto b=seasonCountdown(s,false,now);assert(b.kind==SeasonCountdownKind::StartsDays&&b.tone==SeasonTone::Muted&&b.days==3);
 s.targetAtMs=now+(5LL*3600+12*60+33)*1000;
 auto c=seasonCountdown(s,true,now);assert(c.kind==SeasonCountdownKind::LeftClock&&c.tone==SeasonTone::Red&&c.hours==5&&c.minutes==12&&c.seconds==33);
 auto d=seasonCountdown(s,false,now);assert(d.kind==SeasonCountdownKind::StartsHours&&d.tone==SeasonTone::Muted);
 s.targetAtMs=now-1;assert(seasonCountdown(s,true,now).kind==SeasonCountdownKind::Ended);assert(seasonCountdown(s,false,now).kind==SeasonCountdownKind::Hidden);
 SeasonView o=s;assert(seasonTimerShared(s,true,o,true));assert(!seasonTimerShared(s,true,o,false));o.number=3;assert(!seasonTimerShared(s,true,o,true));
 for(std::int64_t t=0;t<5000;t+=37){auto angle=seasonHandAngle(now+t);assert(angle>-40.0f&&angle<360.0f);}
 assert(std::abs(seasonHandAngle(now+500)-seasonHandAngle(now+900))<0.001f);
}
`));
test('season timer is wired into the lobby, runtime config and admin popup',()=>{
 const popup=read('src/RankedPopup.cpp'),runtime=read('src/RankedRuntime.cpp'),admin=read('src/debug/DebugAdminPopup.cpp'),timer=read('src/ui/SeasonTimer.cpp');
 assert.match(popup,/updateSeasonTimers\(\)/);assert.match(popup,/SeasonTimer::create\(true\)/);
 assert.match(runtime,/observeSeasons\(root\["seasons"\]\)/);
 assert.match(admin,/"SET_SEASON"/);assert.match(admin,/"GET_SEASONS"/);assert.match(admin,/std::mktime/);
 for(const key of ['season-preseason','season-number','season-left-days','season-left-clock','season-ended','season-starts-days','season-starts-hours'])assert(timer.includes(`"${key}"`),key);
});
