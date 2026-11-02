import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function run(code){const dir=mkdtempSync(join(tmpdir(),'corum-survival-vote-'));try{
 writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('difficulty vote stages: hidden until open, choosing, chosen after a ballot, closed after resolution',()=>run(`
#include "${join(root,'src/domain/SurvivalVotePresentation.hpp')}"
#include <cassert>
int main(){using namespace corum::ranked;
 assert(survivalVoteStage(false,false,true,"",0)==SurvivalVoteStage::Hidden);
 assert(survivalVoteStage(true,false,false,"",0)==SurvivalVoteStage::Hidden);
 assert(survivalVoteStage(true,false,true,"",1200)==SurvivalVoteStage::Hidden);
 assert(survivalVoteStage(true,false,true,"",std::nullopt)==SurvivalVoteStage::Hidden);
 assert(survivalVoteStage(true,false,true,"",0)==SurvivalVoteStage::Choosing);
 assert(survivalVoteStage(true,false,true,"HARD",-500)==SurvivalVoteStage::Chosen);
 assert(survivalVoteStage(true,true,true,"",-9000)==SurvivalVoteStage::Closed);
 // Easy / Hard / Demon / N/A faces, in card order.
 assert(SurvivalVoteCards[0].nativeDifficulty==1&&SurvivalVoteCards[1].nativeDifficulty==3&&SurvivalVoteCards[2].nativeDifficulty==6&&SurvivalVoteCards[3].nativeDifficulty==0);
 assert(survivalVoteCardIndex("NONE")==3&&survivalVoteCardIndex("NORMAL")==1&&survivalVoteCardIndex("")==-1);
 // Cards fade in while rising, left to right.
 assert(survivalVoteCardEntry(0,0)==0&&survivalVoteCardEntry(.42f,0)==1&&survivalVoteCardEntry(.42f,3)<1&&survivalVoteCardEntry(.09f,1)==0);
 assert(survivalVoteSecondsLeft(9001)==10&&survivalVoteSecondsLeft(-5)==0&&survivalVoteSecondsLeft(std::nullopt)==0);
 static_assert(SurvivalMatchFoundHoldMs==1000);
}
`));

test('client sends one ballot, shows only the voted count, and holds MATCH FOUND in the queue for a second',()=>{
 const runtime=read('src/SurvivalRuntime.cpp');
 assert.match(runtime,/event\("DIFFICULTY_VOTE"\);body\["ballot"\]=ballot/);
 assert.match(runtime,/!m_view\.vote\.myBallot\.empty\(\)\)return;/);
 assert.match(runtime,/m_matchFoundOpenAt=std::chrono::steady_clock::now\(\)\+std::chrono::milliseconds\(SurvivalMatchFoundHoldMs\)/);
 // The client never reads ballots, tallies or weights: only the count.
 assert.doesNotMatch(runtime,/"weights"|"ballots"/);
 const layer=read('src/ui/SurvivalGameLayer.cpp');
 assert.match(layer,/survivalVoteStage\(view\.vote\.present,view\.vote\.resolved,intro/);
 assert.match(layer,/m_grid->setScale\(1-\.60f\*ease\)/);
 const vote=read('src/ui/SurvivalDifficultyVote.cpp');
 assert.match(vote,/survival-vote-title/);assert.match(vote,/GJDifficultySprite::create\(spec\.nativeDifficulty/);
 const lobby=read('src/RankedPopup.cpp');
 assert.match(lobby,/survival\.matchFoundHold\(\)/);assert.match(lobby,/bool soloMatchFoundHold\(\)/);
 // The opponent stays anonymous in the queue window until the versus intro.
 assert.match(lobby,/m_queueOpponentMark = makeLabel\("\?"/);
});
