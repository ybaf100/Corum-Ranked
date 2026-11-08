#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include "../domain/SurvivalEliminationEffects.hpp"

namespace corum::ranked::ui {

using EliminationIconFactory = std::function<cocos2d::CCNode*()>;

// One eliminated player's card icon. `slot` is the tile's avatar node (the
// effect hides or moves it); pieces are added to `host` (the tile root) at
// `center`, the icon centre in host space.
struct EliminationFxTarget {
    cocos2d::CCNode* slot = nullptr;
    cocos2d::CCNode* host = nullptr;
    cocos2d::CCPoint center{};
    float iconScale = 1.0f;
    EliminationIconFactory icon;
    int order = 0;
};

// Total length of every effect's motion; matches the default fall.
float eliminationFxSeconds();
// Plays a non-default effect on the target. `fall` is how far pieces drop;
// `executor` is the executor icon centre in host space (Vortex pulls there).
void playEliminationFx(EliminationEffect effect, EliminationFxTarget const& target, float fall, cocos2d::CCPoint executor);
// Slice: the halves split along the swipe direction `swipe` (host space).
void playSliceFx(EliminationFxTarget const& target, float fall, cocos2d::CCPoint swipe);
// The default fall (move, spin, shrink, hide) of a card icon `slot`.
void playDefaultFall(cocos2d::CCNode* slot, int order, float fall);
// Slice executor: the 1st place icon `ghost` (in its parent's space) swoops
// over `victim` and returns `home`; a new call restarts the motion from the
// ghost's current position. The cut itself is scheduled per victim with
// scheduleSliceCut, so a following victim never cancels a pending cut.
inline const cocos2d::CCPoint kSliceEnter{-18.0f, 28.0f};
inline const cocos2d::CCPoint kSliceLeave{18.0f, -28.0f};
inline constexpr float kSliceCutDelay = 0.1f; // the ghost reaches the victim
void runSliceGhost(cocos2d::CCNode* ghost, cocos2d::CCPoint victim, cocos2d::CCPoint home);
// Runs `cut` on `host` after kSliceCutDelay (the host owns the pending cut).
void scheduleSliceCut(cocos2d::CCNode* host, std::function<void()> cut);

} // namespace corum::ranked::ui
