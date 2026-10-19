// Execute the production rebuild boundary; subsequent Cocos node creation is
// outside this harness. A removed difficulty node must never be reused, and
// newly created labels must not inherit the old nodes' metadata cache hits.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
test('production selected-panel rebuild retires removed nodes and invalidates creator, grade, stars and readiness caches',()=>{
 const source=readFileSync(join(root,'src/ui/SurvivalSelectedMapPanel.cpp'),'utf8');
 const begin=source.indexOf('void SurvivalSelectedMapPanel::build('),end=source.indexOf('    auto const& r=m_layout;',begin);
 assert(begin>=0&&end>begin);
 const rebuild=source.slice(begin,end).replace('void SurvivalSelectedMapPanel::build','void build')+'}';
 const dir=mkdtempSync(join(tmpdir(),'survival-panel-rebuild-'));
 try{
  writeFileSync(join(dir,'test.cpp'),String.raw`
#include "domain/MapMetadata.hpp"
#include <cassert>
#include <cstdint>
#include <string>
struct CCSize{float width,height;};struct Node{};
int survivalSelectionLayout(float,float,bool,bool){return 1;}void cancelMenuTouch(Node*){}
struct Panel{
 Node menu,oldDifficulty;Node*m_menu=&menu,*m_difficulty=&oldDifficulty;
 int m_levelId=42,m_layout=0;std::uint64_t m_metadataRevision=7;
 bool m_thumbnail=false,m_ldmSupported=false,m_built=false,removed=false;
 corum::ranked::MapMetadata m_mapMetadata;
 std::string m_creatorText="by Creator",m_statusText="MAP READY / AUDIO READY";
 void removeAllChildrenWithCleanup(bool){removed=true;}
 `+rebuild+String.raw`
};
int main(){Panel p;p.m_mapMetadata.kind="NORMAL";p.m_mapMetadata.stars=5;p.m_mapMetadata.epic=3;
 const auto metadata=p.m_mapMetadata;
 for(bool thumbnail:{true,false})for(bool ldm:{true,false}){
  p.m_difficulty=&p.oldDifficulty;p.m_levelId=42;p.m_metadataRevision=7;p.m_mapMetadata=metadata;
  p.m_creatorText="by Creator";p.m_statusText="MAP READY / AUDIO READY";
  p.build({568,320},thumbnail,ldm);assert(p.removed&&p.m_built);
  assert(p.m_difficulty==nullptr); // No future removeFromParent on a destroyed node.
  assert(p.m_levelId!=42||p.m_metadataRevision!=7||p.m_mapMetadata!=metadata);
  assert(p.m_creatorText.empty()&&p.m_statusText.empty());
  assert(p.m_thumbnail==thumbnail&&p.m_ldmSupported==ldm);
 }
}`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
