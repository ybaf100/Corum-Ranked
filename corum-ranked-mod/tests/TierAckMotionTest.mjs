import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');
const method=(src,sig)=>{const at=src.indexOf(sig);assert(at>=0,sig);let depth=0,i=src.indexOf('{',at);for(;i<src.length;i++){if(src[i]==='{')depth++;else if(src[i]==='}'&&--depth===0)break;}return src.slice(at,i+1);};

test('lobby plays an unseen tier change (other players moved the ladder) once, never for the own result',()=>{
 const popup=read('src/RankedPopup.cpp');
 const unseen=method(popup,'    void maybePlayUnseenTierChange() {');
 // Solo ladder titles (MASTER / CHAMPION) are final only once the standing arrived.
 assert.match(unseen,/view\.profileScoreVisible && view\.standing\.loaded && view\.placementGames >= view\.placementGamesRequired/);
 // The first visit records silently; placement and unknown tiers never play here.
 assert.match(unseen,/if \(!seen \|\| \*seen == now \|\| seen->empty\(\) \|\| \*seen == "UNRANKED"/);
 assert.match(unseen,/corum::ranked::ui::writeTierAck\(what, now\);/);
 assert.match(unseen,/rankTierOrder\(now\) > corum::ranked::rankTierOrder\(\*seen\)/);
 assert.match(method(popup,'    bool playUnseenTierTransition('),/up \? RankTransitionType::Promotion : RankTransitionType::Demotion/);
 // The own Solo result and the own Survival result mark their tier as seen.
 {const at=popup.indexOf('    bool playResultRankTransition(');assert.match(popup.slice(at,at+900),/writeTierAck\("tier-solo", upper\(after\.displayedTier\)\);\s*auto const transition = authoritativeTransitionType/);}
 const survival=read('src/ui/SurvivalGameLayer.cpp');
 assert.match(method(survival,'    void beginResultMotion() {'),/ackSurvivalTier\(input->afterTier\)/);
 assert.match(method(survival,'    void beginPromotionMotion() {'),/ackSurvivalTier\(input->afterTier\)/);
 // The motions are full-screen overlays: lobby menus stay suspended underneath.
 assert.match(method(popup,'    static bool fullScreenOverlay('),/RankTransitionLayer\*>\(node\)/);
});

test('achievement grade up plays the core motion only upward and only once',()=>{
 const popup=read('src/RankedPopup.cpp');
 const grade=method(popup,'    void maybePlayAchievementGradeUp() {');
 assert.match(grade,/if \(!achievements\.loaded \|\| achievements\.loading\) return;/);
 assert.match(grade,/writeTierAck\("achievement-grade", grade\);/);
 assert.match(grade,/if \(!seen \|\| \*seen == grade\) return;/);
 assert.match(grade,/if \(order\(grade\) <= order\(\*seen\)\) return;/);
 const layer=read('src/ui/AchievementGradeUpLayer.cpp');
 // Cores only, never rotated (the artwork is already a 45-degree square), and no motion without assets.
 assert.match(layer,/rankResourceSprite\("rank-" \+ \(known \? lower\(grade\) : std::string\("unranked"\)\) \+ "-core\.png"\)/);
 assert.doesNotMatch(layer,/core\([^)]*\)->setRotation|fresh->setRotation|old->setRotation/);
 assert.match(layer,/if \(m_new\.empty\(\) \|\| m_old == m_new \|\| !core\(m_new, 10\.0f\) \|\| !core\(m_old, 10\.0f\)\) return false;/);
});
