import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,mkdirSync,rmSync,cpSync,readdirSync} from 'node:fs';
import {execFileSync,spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../..',import.meta.url));
const read=p=>readFileSync(join(root,p),'utf8');
const ui=JSON.parse(read('ranked/translations/ui.json'));
const achievements=JSON.parse(read('ranked/translations/achievements.json'));

test('all editable en/ko keys and 48 achievement names/descriptions resolve through the actual native catalog',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-translations-native-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/TranslationCatalog.hpp"
#include <cassert>
#include <string>
using namespace corum::ranked;
int main(){for(auto const& entry:kTranslations){assert(catalogTranslation(entry.key)==&entry);assert(std::string(catalogText(entry.key,false))==entry.english);assert(std::string(catalogText(entry.key,true))==entry.korean);}
 assert(!catalogTranslation("future-server-error"));assert(std::string(catalogText("future-server-error",true,"Server fallback"))=="Server fallback");
 assert(std::string(catalogText("achievement.ranked_wins.name",true))=="승리자");
 assert(std::string(catalogText("close",false))=="CLOSE");assert(std::string(catalogText("close",true))=="닫기");}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'corum-ranked-mod/src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
 assert.equal(Object.keys(achievements).length,48);
 execFileSync('node',[join(root,'ranked/tools/localization/build.mjs'),'--check'],{stdio:'pipe'});
});

test('editing text regenerates native and server wording and font coverage; invalid/duplicate entries fail the build',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-translations-build-'));
 try{
  const builder=join(dir,'ranked/tools/localization/build.mjs');mkdirSync(dirname(builder),{recursive:true});cpSync(join(root,'ranked/tools/localization/build.mjs'),builder);
  cpSync(join(root,'ranked/translations'),join(dir,'ranked/translations'),{recursive:true});mkdirSync(join(dir,'corum-ranked-mod'),{recursive:true});cpSync(join(root,'corum-ranked-mod/mod.json'),join(dir,'corum-ranked-mod/mod.json'));
  const catalog=structuredClone(ui);catalog.close={en:'Close the panel',ko:'힣 닫기'};
  const path=join(dir,'ranked/translations/ui.json');writeFileSync(path,JSON.stringify(catalog,null,2)+'\n');
  const ach=structuredClone(achievements);ach.ranked_wins.name.en='Changed in text';writeFileSync(join(dir,'ranked/translations/achievements.json'),JSON.stringify(ach,null,2));
  execFileSync('node',[builder],{stdio:'pipe'});
  assert.match(readFileSync(join(dir,'corum-ranked-mod/src/domain/TranslationCatalog.generated.hpp'),'utf8'),/Close the panel/);
  assert.match(readFileSync(join(dir,'ranked/packages/rules/src/achievement-translations.generated.ts'),'utf8'),/Changed in text/);
  const manifest=JSON.parse(readFileSync(join(dir,'corum-ranked-mod/mod.json'),'utf8'));
  for(const name of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack'])assert(manifest.resources.fonts[name].charset.split(',').includes('55203'));
  catalog.close.en='Missing {value}';writeFileSync(path,JSON.stringify(catalog,null,2));assert.notEqual(spawnSync('node',[builder]).status,0);
  writeFileSync(path,'{\n  "duplicate": {"en":"A","ko":"A"},\n  "duplicate": {"en":"B","ko":"B"}\n}\n');assert.notEqual(spawnSync('node',[builder]).status,0);
  writeFileSync(path,JSON.stringify({missing:{en:'No Korean'}}));assert.notEqual(spawnSync('node',[builder]).status,0);
 }finally{rmSync(dir,{recursive:true,force:true});}
});

test('translated copy lives in text files, every literal translation key is cataloged, and all packaged font weights cover it',()=>{
 const keys=new Set(Object.keys(ui));for(const id of Object.keys(achievements))for(const field of ['name','description'])keys.add(`achievement.${id}.${field}`);
 function walk(path){for(const item of readdirSync(path,{withFileTypes:true})){const file=join(path,item.name);if(item.isDirectory())walk(file);else if(/\.(cpp|hpp)$/.test(file)&&!item.name.includes('.generated.')){
  const source=readFileSync(file,'utf8');assert.doesNotMatch(source,/[가-힣]/u,`Embedded Korean copy: ${file}`);
  for(const match of source.matchAll(/(?:\btr|\bi18n::text|\bliteral|\bcatalogText)\(\s*"([^"\n]*)"\s*[,)]/g))assert(keys.has(match[1]),`Missing catalog key ${match[1]} in ${file}`);
 }}}
 walk(join(root,'corum-ranked-mod/src'));
 const copy=Object.values(ui).flatMap(pair=>[pair.en,pair.ko]).join('')+Object.values(achievements).flatMap(v=>Object.values(v).flatMap(pair=>[pair.en,pair.ko])).join('');
 const fonts=JSON.parse(read('corum-ranked-mod/mod.json')).resources.fonts;
 for(const name of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
  const glyphs=new Set();for(const range of fonts[name].charset.split(',')){const [start,end=start]=range.split('-').map(Number);for(let n=start;n<=end;n++)glyphs.add(n);}
  for(const char of copy)if(!['\n','\r','\t'].includes(char))assert(glyphs.has(char.codePointAt(0)),`${name} missing ${char}`);
 }
 assert.match(read('corum-ranked-mod/CMakeLists.txt'),/add_dependencies\(\$\{PROJECT_NAME\} corum_translations\)/);
 assert.match(read('ranked/packages/rules/package.json'),/"prebuild".*localization\/build.mjs/);
});
