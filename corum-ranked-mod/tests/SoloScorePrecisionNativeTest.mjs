import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
function method(source,name){const start=source.indexOf(name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
test('actual Solo point and signed-difference formatters preserve tenths without changing Survival progress or DM precision',()=>{
 const source=readFileSync(join(root,'src/GameplayUx.cpp'),'utf8'),dir=mkdtempSync(join(tmpdir(),'solo-tenths-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/ScoreDisplay.hpp"
#include <cassert>
#include <limits>
${method(source,'std::string formatNumber(')}
${method(source,'std::string signedValue(')}
int main(){
 assert(formatNumber(118.5)=="118.5"&&formatNumber(73)=="73.0");
 assert(formatNumber(382.06)=="382.1");
 assert(signedValue(118.5-118.4,"")=="+0.1");
 assert(signedValue(118.4-118.5,"")=="-0.1");
 assert(signedValue(0,"")=="0.0"&&signedValue(-.001,"")=="0.0");
 assert(formatNumber(std::numeric_limits<double>::quiet_NaN())=="0.0");
 assert(corum::ranked::rankedScoreText(59.46)=="59");
 assert(corum::ranked::rankedScoreText(94.3705,true)=="94.371");
}`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
