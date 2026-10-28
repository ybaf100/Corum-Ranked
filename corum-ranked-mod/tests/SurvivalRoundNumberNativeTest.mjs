import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
function method(source,name){const start=source.indexOf(name);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end);}
test('production round captions and debug checkbox callbacks preserve display order, independent difficulty, defaults and closure',()=>{
 const d=mkdtempSync(join(tmpdir(),'survival-round-labels-'));
 try{
 const debug=readFileSync(join(root,'src/debug/SurvivalDebugPopup.cpp'),'utf8'),fill=readFileSync(join(root,'src/debug/SurvivalFillBotsPopup.cpp'),'utf8');
 const source=`#include <cassert>
#include "${join(root,'src/domain/SurvivalRoundPresentation.hpp')}"
#include "${join(root,'src/domain/SurvivalMapPolicy.hpp')}"
struct CCObject{};
struct Debug{bool m_forceDeathMatch=false,m_closed=false,m_forceOnly=false;${method(debug,'void onDeathMatch(')}};
struct Fill{bool m_forceDeathMatch=false,m_closed=false;${method(fill,'void onDeathMatch(')}};
int main(){using namespace corum::ranked;
 for(auto phase:{"ROUND_1","ROUND_2","ROUND_3","ROUND_4"}){assert(survivalRoundLabel(phase,1)=="ROUND 1");assert(survivalRoundLabel(phase,2,true)=="R2");}
 for(int n:{1,2,4,5,6,7}){assert(survivalRoundLabel("FINAL",n,true)=="FINAL");assert(survivalRoundLabel("TIEBREAK",n)=="DEATH MATCH");}
 assert(survivalRoundLabel("ROUND_4",0)=="ROUND ?");
 Debug a;Fill b;assert(!a.m_forceDeathMatch&&!b.m_forceDeathMatch);a.onDeathMatch(nullptr);b.onDeathMatch(nullptr);assert(a.m_forceDeathMatch&&b.m_forceDeathMatch);
 a.onDeathMatch(nullptr);assert(!a.m_forceDeathMatch);a.m_forceOnly=true;a.onDeathMatch(nullptr);assert(!a.m_forceDeathMatch);
 b.m_closed=true;b.onDeathMatch(nullptr);assert(b.m_forceDeathMatch);Debug fresh;Fill another;assert(!fresh.m_forceDeathMatch&&!another.m_forceDeathMatch);
}`;
 writeFileSync(join(d,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror',join(d,'test.cpp'),'-o',join(d,'test')],{stdio:'pipe'});execFileSync(join(d,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(d,{recursive:true,force:true});}
});
