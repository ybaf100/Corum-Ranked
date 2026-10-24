import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const adapter=readFileSync(join(root,'src/SurvivalMapValidation.cpp'),'utf8').replace(/^#include.*$/gm,'');

test('Survival native download attestation enforces the exact server ID, version and all 15 difficulty ranges',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-survival-map-'));
    try {
        writeFileSync(join(dir,'test.cpp'),`
#include <cassert>
#include <string>
#include "domain/SurvivalMapPolicy.hpp"
enum class DemonDifficultyType{HardDemon=0,EasyDemon=3,MediumDemon=4,InsaneDemon=5,ExtremeDemon=6};
enum class GJLevelType{Saved,SearchResult,Main,Editor};
struct Value{int number;int value()const{return number;}};
struct GJGameLevel{Value m_levelID{40000000},m_stars{4},m_demon{0};int m_levelVersion=1,m_demonDifficulty=0,m_levelLength=2;
 bool m_autoLevel=false,m_platformer=false,m_levelNotDownloaded=false;GJLevelType m_levelType=GJLevelType::Saved;
 std::string m_levelString="native-downloaded-data";bool isPlatformer(){return m_platformer;}};
${adapter}
int main(){using namespace corum::ranked;
 // Explicit approved table: min/max ordinary stars and min/max Demon ordinal.
 int ranges[3][5][4]={{{4,6,0,0},{5,7,0,0},{6,8,0,0},{7,9,1,1},{0,0,2,2}},
                    {{5,7,0,0},{6,8,0,0},{7,9,0,0},{0,0,1,2},{0,0,3,3}},
                    {{6,8,0,0},{7,9,0,0},{8,9,1,1},{0,0,1,3},{0,0,4,4}}};
 for(int group=0;group<3;group++)for(int phase=0;phase<5;phase++){
  auto g=static_cast<SurvivalDifficultyGroup>(group);auto p=static_cast<SurvivalMapPhase>(phase);auto*r=ranges[group][phase];
  for(int stars=0;stars<=10;stars++)assert(survivalMapAllowed({40000000,stars,1,SurvivalDemon::None,false,false},g,p)==(r[0]>0&&stars>=r[0]&&stars<=r[1]));
  for(int demon=1;demon<=5;demon++)assert(survivalMapAllowed({40000000,10,1,static_cast<SurvivalDemon>(demon),false,false},g,p)==(r[2]>0&&demon>=r[2]&&demon<=r[3]));
 }
 for(int group=0;group<3;group++)for(int stars=1;stars<=10;stars++)for(int demon=0;demon<=5;demon++){
  auto g=static_cast<SurvivalDifficultyGroup>(group);SurvivalMapMetadata metadata{40000000,stars,1,static_cast<SurvivalDemon>(demon),false,false};
  assert(survivalMapAllowed(metadata,g,SurvivalMapPhase::Round4)==survivalMapAllowed(metadata,g,SurvivalMapPhase::Round3));
 }
 GJGameLevel level;SurvivalMapMetadata selected{40000000,4,1,SurvivalDemon::None,false,false};
 auto matches=[&]{return survivalNativeMapMatches(&level,selected,SurvivalDifficultyGroup::RedBronze,SurvivalMapPhase::Round1,40000000);};
 assert(matches());assert(!survivalNativeMapMatches(nullptr,selected,SurvivalDifficultyGroup::RedBronze,SurvivalMapPhase::Round1,40000000));
 level.m_levelID.number=39999999;assert(!matches());level.m_levelID.number=40000001;assert(!matches());level.m_levelID.number=40000000;
 level.m_levelVersion=2;assert(!matches());level.m_levelVersion=1;
 level.m_stars.number=0;assert(!matches());level.m_stars.number=4;
 for(int length=0;length<4;length++){level.m_levelLength=length;assert(matches());}
 for(int length:{4,5,-1}){level.m_levelLength=length;assert(!matches());}level.m_levelLength=2;
 level.m_autoLevel=true;assert(!matches());level.m_autoLevel=false;level.m_platformer=true;assert(!matches());level.m_platformer=false;
 level.m_levelNotDownloaded=true;assert(!matches());level.m_levelNotDownloaded=false;
 for(auto type:{GJLevelType::SearchResult,GJLevelType::Main,GJLevelType::Editor}){level.m_levelType=type;assert(!matches());}level.m_levelType=GJLevelType::Saved;
 level.m_levelString.clear();assert(!matches());level.m_levelString="native-downloaded-data";
 assert(!survivalNativeMapMatches(&level,selected,SurvivalDifficultyGroup::RedBronze,SurvivalMapPhase::Round1,40000001));
 level.m_stars.number=10;level.m_demon.number=1;level.m_demonDifficulty=5;
 selected.stars=10;selected.demon=SurvivalDemon::Insane;
 assert(survivalNativeMapMatches(&level,selected,SurvivalDifficultyGroup::Gold,SurvivalMapPhase::Tiebreak,40000000));
 level.m_demonDifficulty=6;assert(!survivalNativeMapMatches(&level,selected,SurvivalDifficultyGroup::Gold,SurvivalMapPhase::Tiebreak,40000000));
 assert(survivalDemonFromNativeCode(0)==SurvivalDemon::Hard&&survivalDemonFromNativeCode(3)==SurvivalDemon::Easy&&survivalDemonFromNativeCode(1)==SurvivalDemon::Invalid);
}
`);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
        execFileSync(join(dir,'test'),[],{stdio:'pipe'});
    }catch(e){throw new Error(e.stderr?.toString()||e.message);}
    finally{rmSync(dir,{recursive:true,force:true});}
});
