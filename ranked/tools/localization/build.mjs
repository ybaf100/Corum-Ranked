import {readFileSync,writeFileSync,mkdirSync,existsSync,readdirSync} from 'node:fs';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

// UTF-8 JSON is the only editable source of localized UI and achievement copy.
// No runtime file IO, network request or writable resource directory is needed.
const ranked=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
const args=process.argv.slice(2),check=args.includes('--check');
const target=args.includes('--target')?args[args.indexOf('--target')+1]:'all';
if(!['all','client','rules'].includes(target))throw new Error('Unknown localization target');
function catalog(file){
 const raw=readFileSync(resolve(ranked,'translations',file),'utf8');
 // Detect duplicate JSON keys rather than silently accepting the last value.
 const keys=[...raw.matchAll(/^  "((?:\\.|[^"\\])+)":/gm)].map(m=>JSON.parse(`"${m[1]}"`));
 if(new Set(keys).size!==keys.length)throw new Error(`Duplicate key in ${file}`);
 return JSON.parse(raw);
}
const ui=catalog('ui.json'),achievements=catalog('achievements.json');
// Loading tips: ranked/translations/loading-tips.md, "N." then one EN and one
// KR line per tip; "#" and ">" lines are notes. Strict, so a typo fails the
// build instead of silently dropping a tip.
function loadingTips(){
 const file='loading-tips.md',tips=[];let current=null;
 readFileSync(resolve(ranked,'translations',file),'utf8').split(/\r?\n/).forEach((raw,index)=>{
  const line=raw.trim(),where=`${file}:${index+1}`;
  if(!line||line.startsWith('#')||line.startsWith('>'))return;
  const number=line.match(/^(\d+)\.$/);
  if(number){
   if(current&&(!current.en||!current.ko))throw new Error(`${file}: tip ${current.number} needs one EN and one KR line`);
   if(tips.some(tip=>tip.number===Number(number[1])))throw new Error(`${where}: duplicate tip number ${number[1]}`);
   current={number:Number(number[1])};tips.push(current);return;
  }
  const text=line.match(/^(EN|KR)\s+(\S.*)$/);
  if(!text||!current)throw new Error(`${where}: expected "N.", "EN text" or "KR text"`);
  const key=text[1]==='EN'?'en':'ko';
  if(current[key])throw new Error(`${where}: tip ${current.number} has two ${text[1]} lines`);
  if(/[\u0000-\u001f]/u.test(text[2]))throw new Error(`${where}: control character`);
  current[key]=text[2];
 });
 if(current&&(!current.en||!current.ko))throw new Error(`${file}: tip ${current.number} needs one EN and one KR line`);
 if(!tips.length)throw new Error(`${file}: no tips`);
 return tips.sort((a,b)=>a.number-b.number);
}
const tips=loadingTips();
const placeholders=s=>[...s.matchAll(/(?<!\{)\{([^{}]*)\}(?!\})/g)].map(m=>m[1]).sort().join('|');
function validate(key,pair){
 if(!pair||typeof pair.en!=='string'||typeof pair.ko!=='string'||!pair.en||!pair.ko)throw new Error(`Missing en/ko: ${key}`);
 if(placeholders(pair.en)!==placeholders(pair.ko))throw new Error(`Placeholder mismatch: ${key}`);
 if(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/u.test(pair.en+pair.ko))throw new Error(`Control character: ${key}`);
 for(const value of [pair.en,pair.ko])for(let i=0;i<value.length;i++){
  if(value[i]==='{'&&value[i+1]==='{'){i++;continue;}
  if(value[i]==='}'&&value[i+1]==='}'){i++;continue;}
  if(value[i]==='{'){
   const close=value.indexOf('}',i+1);
   if(close<0||value.slice(i+1,close).includes('{'))throw new Error(`Unbalanced placeholder: ${key}`);
   i=close;
  }else if(value[i]==='}')throw new Error(`Unbalanced placeholder: ${key}`);
 }
}
for(const [key,pair] of Object.entries(ui))validate(key,pair);
for(const [id,entry] of Object.entries(achievements))for(const field of ['name','description',...('hint' in entry?['hint']:[])]){
 const key=`achievement.${id}.${field}`;validate(key,entry[field]);
 if(key in ui)throw new Error(`Duplicate achievement key: ${key}`);ui[key]=entry[field];
}
const quote=s=>JSON.stringify(s).replaceAll('\u2028','\\u2028').replaceAll('\u2029','\\u2029');
function output(path,contents){
 const old=existsSync(path)?readFileSync(path,'utf8'):'';
 if(old===contents)return;
 if(check)throw new Error(`Generated translations are stale: ${path}`);
 mkdirSync(dirname(path),{recursive:true});writeFileSync(path,contents);
}
if(target!=='rules'){
 const manifestPath=resolve(ranked,'../corum-ranked-mod/mod.json');
 const manifest=JSON.parse(readFileSync(manifestPath,'utf8'));
 const required=new Set([...Object.values(ui).map(pair=>pair.en+pair.ko).join('')+tips.map(tip=>tip.en+tip.ko).join('')]
     .filter(c=>!['\n','\r','\t'].includes(c)).map(c=>c.codePointAt(0)));
 for(const name of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
  const font=manifest.resources.fonts[name],present=new Set();
  for(const range of font.charset.split(',')){
   const [start,end=start]=range.split('-').map(Number);
   for(let value=start;value<=end;value++)present.add(value);
  }
  const missing=[...required].filter(value=>!present.has(value)).sort((a,b)=>a-b);
  // The four Hangul weights' SD/HD/UHD atlases share one RGBA budget that
  // holds 352 syllables (FontTypographyNativeTest, verify-font-package.py).
  const hangul=value=>value>=0xac00&&value<=0xd7a3;
  const total=[...present,...missing].filter(hangul).length;
  if(total>352){
   const added=missing.filter(hangul).map(value=>String.fromCodePoint(value)).join('');
   throw new Error(`${name}: ${total} Korean syllables, the font atlas holds 352. New: ${added}. `+
    'Reword the new ui.json / loading-tips.md Korean text with syllables the UI already uses.');
  }
  if(missing.length)font.charset+=','+missing.join(',');
 }
 output(manifestPath,JSON.stringify(manifest,null,2)+'\n');
}
const notice='Generated by ranked/tools/localization/build.mjs. Edit ranked/translations/*.json.';
if(target!=='rules'){
 const sourceRoot=resolve(ranked,'../corum-ranked-mod/src');
 function validateSource(path){for(const entry of readdirSync(path,{withFileTypes:true})){
  const file=resolve(path,entry.name);
  if(entry.isDirectory())validateSource(file);
  else if(/\.(cpp|hpp)$/.test(entry.name)&&!entry.name.includes('.generated.')){
   const source=readFileSync(file,'utf8');
   if(/[가-힣]/u.test(source))throw new Error(`Move embedded Korean copy into the translation catalog: ${file}`);
   for(const match of source.matchAll(/(?:\btr|\bi18n::text|\bliteral|\bcatalogText)\(\s*"([^"\n]*)"\s*[,)]/g))
    if(!(match[1] in ui))throw new Error(`Missing translation key ${match[1]}: ${file}`);
  }
 }}
 if(existsSync(sourceRoot))validateSource(sourceRoot);
}
if(target!=='rules')output(resolve(ranked,'../corum-ranked-mod/src/domain/TranslationCatalog.generated.hpp'),
 `// ${notice}\n#pragma once\n#include <array>\nnamespace corum::ranked {\nstruct CatalogTranslation {char const* key;char const* english;char const* korean;};\ninline constexpr std::array<CatalogTranslation, ${Object.keys(ui).length}> kTranslations {{\n`+
 Object.entries(ui).sort(([a],[b])=>a<b?-1:a>b?1:0).map(([key,value])=>`    CatalogTranslation{${quote(key)}, ${quote(value.en)}, ${quote(value.ko)}}`).join(',\n')+
 '\n}};\n} // namespace corum::ranked\n');
if(target!=='rules')output(resolve(ranked,'../corum-ranked-mod/src/domain/LoadingTips.generated.hpp'),
 `// Generated by ranked/tools/localization/build.mjs. Edit ranked/translations/loading-tips.md.\n#pragma once\n#include <array>\nnamespace corum::ranked {\nstruct LoadingTip {char const* english;char const* korean;};\ninline constexpr std::array<LoadingTip, ${tips.length}> kLoadingTips {{\n`+
 tips.map(tip=>`    LoadingTip{${quote(tip.en)}, ${quote(tip.ko)}}`).join(',\n')+'\n}};\n} // namespace corum::ranked\n');
if(target!=='client')output(resolve(ranked,'packages/rules/src/achievement-translations.generated.ts'),
 `// ${notice}\nconst translations = ${JSON.stringify(achievements,null,2)} as const;\n`+
 'export function achievementText(id: keyof typeof translations) {\n  const text=translations[id];\n  return {nameKo:text.name.ko,nameEn:text.name.en,descriptionKo:text.description.ko,descriptionEn:text.description.en,...("hint" in text?{hintKo:text.hint.ko,hintEn:text.hint.en}:{})};\n}\n');
console.log(`Translations: ${Object.keys(ui).length} UI keys, ${Object.keys(achievements).length} achievements, ${tips.length} loading tips (${target}, ${check?'checked':'generated'})`);
