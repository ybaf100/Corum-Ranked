import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
function run(code){const dir=mkdtempSync(join(tmpdir(),'corum-death-feed-'));try{
 writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('real death-feed policy bounds three rows, deduplicates polling, preserves five seconds and retires map contexts',()=>run(`
#include "${join(root,'src/domain/DeathFeed.hpp')}"
#include <cassert>
int main(){using namespace corum::ranked;DeathFeed feed;feed.reset("match:r1");
 feed.observe({{"1","A",35,1000}},1000,1000);feed.observe({{"2","B",49,2000}},2000,2000);
 feed.observe({{"3","A",70,3000}},3000,3000);feed.observe({{"4","B",90,4000}},4000,4000);
 assert(feed.rows().size()==3&&feed.rows().front().event.id=="2"&&feed.rows().back().event.id=="4");
 feed.observe({{"2","B",49,2000}},6999,6999);assert(feed.rows().front().createdAtMs==2000);
 feed.observe({},7000,7000);assert(feed.rows().size()==3); // beginning outgoing fade/down, no fourth row
 feed.observe({},7250,7250);assert(feed.rows().size()==2&&feed.rows().front().event.id=="3");
 feed.observe({{"2","B",49,2000},{"stale","A",20,1000}},7300,7300);assert(feed.rows().size()==2);
 feed.reset("match:r2");assert(feed.rows().empty());feed.observe({{"5","A",20,8000}},9000,9000);
 assert(feed.rows().size()==1&&feed.rows().front().createdAtMs==8000);feed.observe({},13250,13250);assert(feed.rows().empty());
}
`));
test('real native UI keeps outgoing and survivor motions separate with own red/opponent green and a close Qualifying percentage',()=>{
 const s=readFileSync(join(root,'src/PlayLayer.cpp'),'utf8');
 // Visual action parameters supplement the executable policy; native Cocos
 // rendering/input and actual on-device animation are outside this fixture.
 for(const required of ['corum::ranked::ui::Red : corum::ranked::ui::Green','"black", 0.65f',
  'CCMoveBy::create(duration, {0, -8})','CCFadeTo::create(duration, 0)','CCMoveTo::create(.22f',
  'qualifyingCaption->getScaledContentSize().width + 10.0f'])
  if(!s.includes(required))throw new Error('Missing native presentation: '+required);
});
