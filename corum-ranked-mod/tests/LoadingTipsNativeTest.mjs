import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const read=path=>readFileSync(new URL(path,import.meta.url),'utf8');
const popup=read('../src/RankedPopup.cpp');
function body(name){const at=popup.indexOf(name);assert(at>=0,name);let end=popup.indexOf('{',at)+1,depth=1;while(depth){depth+=(popup[end]==='{')-(popup[end]==='}');++end;}return popup.slice(at,end);}

test('loading tips come from loading-tips.md: one EN and one KR line per numbered tip, generated into the build',()=>{
 const md=read('../../ranked/translations/loading-tips.md');
 const numbers=[...md.matchAll(/^(\d+)\.$/gm)].map(m=>Number(m[1]));
 assert(numbers.length>=5,'at least the five default tips');
 assert.equal(new Set(numbers).size,numbers.length);
 assert.equal([...md.matchAll(/^EN \S/gm)].length,numbers.length);
 assert.equal([...md.matchAll(/^KR \S/gm)].length,numbers.length);
 const generated=read('../src/domain/LoadingTips.generated.hpp');
 assert.match(generated,new RegExp(`std::array<LoadingTip, ${numbers.length}> kLoadingTips`));
 for(const line of md.split('\n').filter(l=>/^(EN|KR) /.test(l)))assert(generated.includes(JSON.stringify(line.slice(3))),line);
 // The build fails on a malformed tips file instead of dropping a tip.
 const build=read('../../ranked/tools/localization/build.mjs');
 assert.match(build,/expected "N\.", "EN text" or "KR text"/);
 assert.match(build,/tips\.map\(tip=>tip\.en\+tip\.ko\)/,'tip characters join the font charsets');
});

test('the lobby loading views use the loading bar and a tappable tip, not the spinner',()=>{
 const main=body('void renderMain(RuntimeView const& view)');
 assert.match(main,/if \(awaitingSurvivalProfile\(view\)\) \{\s*addLoadingView\(view, tr\("syncing","SYNCING\.\.\."\), true\);/);
 assert.match(main,/if \(view\.stage == RuntimeStage::Loading\) \{\s*addLoadingView\(view, lobbyMessage\(view\), false\);/);
 assert.doesNotMatch(main,/LoadingSpinner/);
 const view=body('void addLoadingView(');
 assert.match(view,/RankedLoadingBar::create\(barWidth, 7\.0f, &m_loadingShown,/);
 assert.match(view,/menu_selector\(CorumRankedLayer::onLoadingTip\)/);
 // Tapping updates the tip in place; it never re-renders from inside the button's own callback.
 const tap=body('void onLoadingTip(CCObject*)');
 assert.match(tap,/getChildByIDRecursive\("ranked-loading-tip-text"_spr\)/);
 assert.doesNotMatch(tap,/render\(|m_renderedUiKey/);
});

test('loading bar follows the entry steps, creeps without reaching the next one and never rewinds; tips change on tap',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-loading-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/LoadingProgress.hpp"
#include <cassert>
#include <set>
using namespace corum::ranked;
int main(){
 char const* steps[]={"Loading authoritative Ranked configuration...","Verifying your Geometry Dash account...","Creating a server session...","Checking for an active Placement run..."};
 float last=0;
 for(auto const* status:steps){auto const step=loadingStepFor(status,false);assert(step.reached>=last&&step.next>step.reached);last=step.reached;}
 assert(loadingStepFor("",true).reached>=last);
 // Catches up with the reached step, then creeps and stops 2% short of the next.
 auto const step=loadingStepFor(steps[2],false);float shown=0;
 for(int i=0;i<600;++i)shown=advanceLoadingBar(shown,step,1.0f/60.0f);
 assert(shown>=step.reached&&shown<=step.next-0.02f+1e-5f&&shown>step.next-0.05f);
 // An earlier step later on does not move the bar back.
 assert(advanceLoadingBar(0.8f,loadingStepFor(steps[0],false),1.0f/60.0f)==0.8f);
 // A huge frame (app resumed) does not jump past the step.
 assert(advanceLoadingBar(0.0f,step,30.0f)<=step.reached);
 // The next tip is always a different one and every tip can come up.
 std::set<std::size_t> seen;
 for(std::size_t r=0;r<40;++r){auto const next=nextLoadingTip(2,5,r);assert(next!=2&&next<5);seen.insert(next);}
 assert(seen.size()==4);
 assert(nextLoadingTip(0,1,7)==0);
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',new URL('../src',import.meta.url).pathname,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
