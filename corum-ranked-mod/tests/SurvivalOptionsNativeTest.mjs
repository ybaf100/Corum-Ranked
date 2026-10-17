import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(join(root,'src/SurvivalOptionsPreferences.cpp'),'utf8').replace(/^#include.*$/gm,'');
for(const mode of ['fresh','legacy','already-migrated'])test(`actual saved Survival defaults: ${mode}, persistence, round overrides and replacement native objects`,()=>{
 const dir=mkdtempSync(join(tmpdir(),'survival-preferences-'));try{
 writeFileSync(join(dir,'test.cpp'),String.raw`
#include "SurvivalOptionsPreferences.hpp"
#include "domain/SurvivalNativeOptions.hpp"
#include <cassert>
#include <map>
#include <string>
struct GJGameLevel {bool m_lowDetailMode=false,m_lowDetailModeToggled=true,m_disableShakeToggled=false;};
namespace geode::prelude {
struct Mod {std::map<std::string,bool> saved;int writes=0;static Mod*get(){static Mod m;return &m;}
 template<class T>T getSavedValue(std::string key,T fallback){return saved.contains(key)?static_cast<T>(saved[key]):fallback;}
 template<class T>void setSavedValue(std::string key,T value){saved[key]=value;++writes;}};
}
`+source+String.raw`
int main(){using namespace corum::ranked;auto* store=geode::prelude::Mod::get();
`+(mode==='legacy'?String.raw`store->saved={{"survival-low-detail",true},{"survival-disable-shake",true}};`:
 mode==='already-migrated'?String.raw`store->saved={{"survival-low-detail",false},{"survival-disable-shake",false},{"survival-defaults-migrated",true},{"survival-default-ldm",true},{"survival-default-shake",true}};`:'')+String.raw`
 auto& p=SurvivalOptionsPreferences::get();auto defaults=p.defaults();
`+(mode==='fresh'?'assert(!defaults.lowDetail&&!defaults.disableShake);':'assert(defaults.lowDetail&&defaults.disableShake);')+String.raw`
 assert(store->saved["survival-defaults-migrated"]);
 auto round=p.current("match:R1:1");assert(round==defaults);auto writes=store->writes;
 p.setRoundLowDetail("match:R1:1",!defaults.lowDetail);p.setRoundDisableShake("match:R1:1",!defaults.disableShake);
 assert(store->writes==writes&&p.defaults()==defaults);GJGameLevel first,replaced;
 p.apply(&first,"match:R1:1");assert(!first.m_lowDetailModeToggled);assert(first.m_disableShakeToggled==!defaults.disableShake);
 first.m_lowDetailMode=true;replaced.m_lowDetailMode=true;
 p.apply(&first,"match:R1:1");p.apply(&replaced,"match:R1:1");assert(first.m_lowDetailModeToggled==!defaults.lowDetail&&replaced.m_lowDetailModeToggled==first.m_lowDetailModeToggled);
 p.setDefaultLowDetail(defaults.lowDetail);p.setDefaultDisableShake(defaults.disableShake);
 assert(p.current("match:R1:1").lowDetail==!defaults.lowDetail); // visiting Settings cannot reset this round
 assert(p.current("match:R2:1")==defaults);assert(p.current("match:R2:2")==defaults); // new generation inherits
 p.apply(nullptr,"match:R2:2");p.apply(&replaced,"");
 p.setDefaultLowDetail(true);p.setDefaultDisableShake(false);
 assert(store->saved["survival-default-ldm"]&&!store->saved["survival-default-shake"]);
 assert((p.current("another:R1:1")==SurvivalNativeOptions{true,false}));
}`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
