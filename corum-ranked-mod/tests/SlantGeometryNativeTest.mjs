import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');

test('a leaning button face keeps its top rule on the slanted top edge (layers lean about the face centre)',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-slant-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/SlantGeometry.hpp"
#include <cassert>
#include <cmath>
#include <initializer_list>
using namespace corum::ranked;
// A CCLayerColor at (x, y), h tall, skewed about its own centre: where its
// point at local height ly lands, as cocos2d places it.
struct Layer{float x,y,w,h;};
float at(Layer const& l,float lx,float ly,float skew){return l.x+lx+(ly-l.h/2.0f)*std::tan(skew*0.017453292f);}
Layer lean(Layer l,float faceH,float skew){l.x+=leanAboutFaceCentre(l.y,l.h,faceH,skew);return l;}
bool near(float a,float b){return std::fabs(a-b)<1e-3f;}
int main(){
 for(float skew:{8.0f,12.0f,20.0f})for(float H:{18.0f,26.0f,40.0f}){
  float const W=120.0f;
  auto const body=lean(Layer{0,0,W,H},H,skew);                 // the frame: full height, unchanged
  auto const rule=lean(Layer{0,H-1.5f,W,1.5f},H,skew);         // the top rule
  auto const fill=lean(Layer{1,1,W-2,H-2},H,skew);             // the inset fill
  auto const shadow=lean(Layer{1.5f,-2,W,H},H,skew);           // the drop shadow, 2 pt lower
  assert(body.x==0.0f&&fill.x==1.0f);
  // The rule's top corners are the body's top corners, at both ends.
  assert(near(at(rule,0,1.5f,skew),at(body,0,H,skew))&&near(at(rule,W,1.5f,skew),at(body,W,H,skew)));
  // ... and its bottom edge follows the same slanted side 1.5 pt lower.
  assert(near(at(rule,0,0,skew),at(body,0,H-1.5f,skew)));
  // The inset fill keeps an even 1 pt margin along the slanted sides.
  assert(near(at(fill,0,H-2,skew),at(body,0,H-1,skew)+1.0f));
  // The shadow is the body moved 1.5 right and 2 down, on the same slant.
  assert(near(at(shadow,0,0,skew),at(body,0,-2,skew)+1.5f));
  // Before the fix (each layer about its own centre) the rule sat left of the top edge by (H-1.5)/2*tan.
  Layer const old{0,H-1.5f,W,1.5f};
  assert(near(at(body,0,H,skew)-at(old,0,1.5f,skew),(H-1.5f)/2.0f*std::tan(skew*0.017453292f)));
 }
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
 const fx=read('src/ui/ButtonFx.hpp');
 // Every layer of a leaning face is shifted to lean about the face centre, once.
 assert.match(fx,/if \(!layer \|\| layer->getSkewX\(\) != 0\.0f\) continue;\s*layer->setSkewX\(SlabSkew\);\s*layer->setPositionX\(layer->getPositionX\(\) \+ corum::ranked::leanAboutFaceCentre\(\s*layer->getPositionY\(\), layer->getContentSize\(\)\.height, size\.height, SlabSkew\)\);/);
 // The highlight over the face leans with it.
 assert.match(fx,/m_glow->setSkewX\(leaned \? SlabSkew : m_skew\);/);
});
