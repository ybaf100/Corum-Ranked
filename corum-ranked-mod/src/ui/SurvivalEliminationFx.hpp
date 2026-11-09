#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <vector>
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
// Slice executor. With several victims the 1st place icon flies one planned
// route: it swoops through each card in elimination order, arriving as that
// card's cut lands, and the swipe alternates direction (an X rhythm) before
// it returns home. The cuts are scheduled per card (scheduleSliceCut), so the
// route never has to wait for or cancel one.
struct SliceStop {
    cocos2d::CCPoint victim;  // icon centre in the ghost's parent space
    float at = 0.0f;          // seconds from now when this card's cut lands
    bool mirrored = false;    // swipe top-right -> bottom-left instead
};
inline constexpr float kSliceCutDelay = 0.1f; // reveal -> cut
cocos2d::CCPoint sliceEnter(bool mirrored);
cocos2d::CCPoint sliceLeave(bool mirrored);
cocos2d::CCPoint sliceSwipe(bool mirrored);
void runSliceRoute(cocos2d::CCNode* ghost, std::vector<SliceStop> const& stops, cocos2d::CCPoint home);
// Runs `cut` on `host` after kSliceCutDelay (the host owns the pending cut).
void scheduleSliceCut(cocos2d::CCNode* host, std::function<void()> cut);

} // namespace corum::ranked::ui
