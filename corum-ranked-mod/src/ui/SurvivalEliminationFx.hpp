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

} // namespace corum::ranked::ui
