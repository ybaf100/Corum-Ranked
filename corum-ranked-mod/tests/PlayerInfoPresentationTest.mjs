import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const read = path => readFileSync(new URL(path,import.meta.url),'utf8');
function body(source, name) {const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
test('Extreme totals exclude weekly/gauntlet duplicates and complete UTF-8 titles fit their reserved boxes',()=>{
  const domain=read('../src/domain/PlayerInfoPresentation.hpp').replace('#pragma once','');
  const fit=body(read('../src/ui/RankedVisuals.hpp'),'inline void fitFullLabel(');
  const cpp=`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
${domain}
struct Size{float width,height;};constexpr int kCCTextAlignmentCenter=1;
struct CCLabelBMFont{std::string original;float scale=1,width=1;bool wrap=false;void setAlignment(int){}void setLineBreakWithoutSpace(bool v){wrap=v;}void setWidth(float v){width=v;}float getScale()const{return scale;}
 Size getContentSize()const{int count=0;for(unsigned char c:original)if((c&0xC0)!=0x80)++count;int perLine=std::max(1,static_cast<int>(width/20));return {std::min(width,count*20.0f),40.0f*std::ceil(static_cast<float>(count)/perLine)};}};
void setLabelScale(CCLabelBMFont* v,float scale){v->scale=scale/2;} // actual 128px Corum font factor
${fit}
int main(){using corum::ranked::extremeDemonCount;
 assert(extremeDemonCount("155,11,7,21,80,7,2,0,0,0,22,8")==80);
 assert(extremeDemonCount("0,0,0,0,3,0,0,0,0,5,99,99")==8);
 for(auto value:{"","0,0,0,0,3","0,0,0,0,-1,0,0,0,0,0,0,0","0,0,0,0,x,0,0,0,0,0,0,0","0,0,0,0,1,0,0,0,0,0,0,0,5"})assert(!extremeDemonCount(value));
 for(auto const& title:{std::string("A long level title that must remain complete"),std::string(150,'W'),std::string("긴맵제목을생략하지않고전체표시")})
 for(auto bounds:{Size{85,26},Size{310,27}}){CCLabelBMFont text;text.original=title;fitFullLabel(&text,bounds.width,bounds.height,.31f);assert(text.original==title&&text.wrap);assert(text.getContentSize().height*text.scale<=bounds.height);assert(text.getContentSize().width*text.scale<=bounds.width+.001f);}
}
`;
  const dir=mkdtempSync(join(tmpdir(),'corum-player-info-'));
  try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});
