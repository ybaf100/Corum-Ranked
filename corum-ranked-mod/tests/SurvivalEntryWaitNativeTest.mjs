import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const source=readFileSync(new URL('../src/RankedPopup.cpp',import.meta.url),'utf8');
function body(name){const at=source.indexOf(name);assert(at>=0,name);let end=source.indexOf('{',at)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(at,end);}

test('the lobby waits for the Survival profile once, so its card never shows a provisional UNRANKED',()=>{
 // The main lobby renders the loading view while waiting, and the wait is part of the re-render key.
 const main=body('void renderMain(RuntimeView const& view)'),wait=main.indexOf('if (awaitingSurvivalProfile(view)) {');
 assert(wait>=0&&main.indexOf('addLoadingView(view',wait)>wait&&main.indexOf('return;',wait)<main.indexOf('makeLobbyGroup',wait));
 assert.match(source,/:sur-wait:\{\}",[^\n]*awaitingSurvivalProfile\(view\)\)/);
 const dir=mkdtempSync(join(tmpdir(),'corum-survival-wait-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include <cassert>
#include <chrono>
#include <optional>
#include <string>
enum class RuntimeStage{Loading,Ready,Queued};
struct RuntimeView{RuntimeStage stage=RuntimeStage::Loading;bool survivalConfigReady=true;struct{bool loaded=false;std::string error;}survival;};
struct Lobby{
 mutable std::optional<std::chrono::steady_clock::time_point> m_survivalWaitSince;
 mutable bool m_survivalWaitDone=false;
 ${body('bool awaitingSurvivalProfile(RuntimeView const& view) const')}
};
int main(){
 RuntimeView view;Lobby lobby;
 // Not ready yet: the normal loading view, and the wait has not started.
 assert(!lobby.awaitingSurvivalProfile(view)&&!lobby.m_survivalWaitDone&&!lobby.m_survivalWaitSince);
 // Ready without the Survival profile: wait.
 view.stage=RuntimeStage::Ready;assert(lobby.awaitingSurvivalProfile(view)&&lobby.m_survivalWaitSince);
 // Loaded: show the lobby, and never wait again in this visit (a refetch clears loaded / error).
 view.survival.loaded=true;assert(!lobby.awaitingSurvivalProfile(view)&&lobby.m_survivalWaitDone);
 view.survival.loaded=false;assert(!lobby.awaitingSurvivalProfile(view));
 // An error ends the wait (the card shows RETRY).
 {Lobby l;view.survival.error="timeout";assert(!l.awaitingSurvivalProfile(view)&&l.m_survivalWaitDone);view.survival.error.clear();}
 // Survival not configured: nothing to wait for.
 {Lobby l;view.survivalConfigReady=false;assert(!l.awaitingSurvivalProfile(view));view.survivalConfigReady=true;}
 // A profile that never arrives: the lobby shows after 5 s.
 {Lobby l;assert(l.awaitingSurvivalProfile(view));l.m_survivalWaitSince=std::chrono::steady_clock::now()-std::chrono::seconds(6);
  assert(!l.awaitingSurvivalProfile(view)&&l.m_survivalWaitDone);}
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
