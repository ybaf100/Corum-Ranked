import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const visuals=readFileSync(join(root,'src/ui/RankedVisuals.hpp'),'utf8');
const method=(src,sig)=>{const at=src.indexOf(sig);assert(at>=0,sig);let depth=0,i=src.indexOf('{',at);for(;i<src.length;i++){if(src[i]==='{')depth++;else if(src[i]==='}'&&--depth===0)break;}return src.slice(at,i+1);};

test('Windows mipmaps each Corum font atlas once, so small menu text stays smooth at 1080p',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-font-mipmap-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include <cassert>
#include <string_view>
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
struct ccTexParams{unsigned minFilter,magFilter,wrapS,wrapT;};
struct Texture{bool mip=false;int generated=0;unsigned minFilter=GL_LINEAR,magFilter=0;
 bool hasMipmaps(){return mip;}void generateMipmap(){mip=true;++generated;}void setTexParameters(ccTexParams* p){minFilter=p->minFilter;magFilter=p->magFilter;}};
struct CCLabelBMFont{char const* file;Texture* texture;char const* getFntFile(){return file;}Texture* getTexture(){return texture;}};
${method(visuals,'inline void smoothCorumFontTexture(')}
int main(){
 Texture corum,native;CCLabelBMFont a{"hwanhee1.corum_ranked/CorumSansBold.fnt",&corum},b{"hwanhee1.corum_ranked/CorumSansBold.fnt",&corum},n{"bigFont.fnt",&native};
 smoothCorumFontTexture(&a);smoothCorumFontTexture(&b);smoothCorumFontTexture(&n);smoothCorumFontTexture(nullptr);
#if defined(GEODE_IS_WINDOWS)
 // The shared atlas is mipmapped once and sampled trilinearly; GD's own fonts are untouched.
 assert(corum.generated==1&&corum.minFilter==GL_LINEAR_MIPMAP_LINEAR&&corum.magFilter==GL_LINEAR);
#else
 // GLES 2 cannot mipmap the non-power-of-two atlases: unchanged elsewhere.
 assert(corum.generated==0&&corum.minFilter==GL_LINEAR);
#endif
 assert(native.generated==0);
}
`);
  for(const flags of [['-DGEODE_IS_WINDOWS=1'],[]]){
   execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...flags,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
   execFileSync(join(dir,'test'),[],{stdio:'pipe'});
  }
 }finally{rmSync(dir,{recursive:true,force:true});}
 // Every Corum label passes through setLabelScale, which smooths its atlas first.
 assert.match(method(visuals,'inline void setLabelScale('),/smoothCorumFontTexture\(caption\);\s*caption->setScale\(fontScale\(caption, logicalScale\)\);/);
});
