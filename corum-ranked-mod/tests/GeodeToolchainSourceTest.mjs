import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const root=new URL('../../',import.meta.url);
// SDK 5.8.2 was failing before Corum source compilation: moving bindings and
// the separately downloaded Codegen binary produced incompatible native types.
test('every Ranked platform build pairs the manifest SDK with pinned bindings and their source-built generator',()=>{
 const bindings='70986055f526da15318779703f0e904be822f162';
 let builds=0;
 for(const file of ['.github/workflows/build-ranked-mod.yml','.github/workflows/build-ranked-debug.yml']){
  const yaml=readFileSync(new URL(file,root),'utf8');
  for(const match of yaml.matchAll(/^(\s*)uses: geode-sdk\/build-geode-mod@[^\n]+\n([\s\S]*?)(?=^\s*- (?:name:|uses:)|^  [\w-]+:|$(?![\s\S]))/gm)){
   const block=match[0];++builds;
   assert.match(block,/sdk: given\b/,file+' must use the manifest SDK');
   assert(block.includes('bindings-ref: '+bindings),file+' must pin compatible bindings');
   assert.match(block,/configure-args:.*-DSKIP_BUILDING_CODEGEN=OFF\b/,file+' must compile Codegen from those same sources');
   assert.match(block,/configure-args:.*-DCORUM_RANKED_DEBUG_BOT_MATCH=(?:ON|OFF)\b/);
   assert.match(block,/configure-args:.*-DCORUM_RANKED_DEBUG_SPECTATE=(?:ON|OFF)\b/);
  }
 }
 assert.equal(builds,4,'Release matrix, Debug matrix, iOS OFF and other-platform OFF must all be covered');
});
