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
// `finale`: the last cut (bigger slash, wider sparks, slower fall).
void playSliceFx(EliminationFxTarget const& target, float fall, cocos2d::CCPoint swipe, bool finale = false);
// The default fall (move, spin, shrink, hide) of a card icon `slot`.
void playDefaultFall(cocos2d::CCNode* slot, int order, float fall);
// Slice ("gather and cut"): the executor's icon turns gold and leaves its
// card; the victims' icons leave their cards one after another (each card
// turns red as its icon leaves) and gather in one pile; the gold icon then
// cuts the pile one player at a time in a few big sweeps (about five cuts per
// sweep, in blade order) trailed by two afterimages, each cut a white flash,
// halves popping apart and sparks, the last player after a slow-motion beat
// with a bigger swing, shockwave ring and flash, and flies back to its card.
// Everything runs on one stage node in `layer`.
struct SliceGatherVictim {
    cocos2d::CCNode* slot = nullptr;        // card icon, hidden as it leaves (held weakly)
    cocos2d::CCPoint from;                  // icon centre in layer space
    EliminationIconFactory icon;            // a copy of the icon
    float iconScale = 1.0f;
    std::function<void()> detach;           // the card turns red
};
struct SliceGatherExecutor {
    cocos2d::CCNode* slot = nullptr;        // the executor's card icon, hidden while away (held weakly)
    cocos2d::CCPoint home;                  // its centre in layer space
    EliminationIconFactory icon;
    float iconScale = 1.0f;
};
// Seconds from the start until the gold icon is back home.
float sliceGatherSeconds(std::size_t victims);
// `shakeTarget` (the card grid) shakes once per sweep and hard on the finale.
void runSliceGathering(cocos2d::CCNode* layer, SliceGatherExecutor const& executor,
    std::vector<SliceGatherVictim> const& victims, cocos2d::CCPoint pile, float fall, cocos2d::CCNode* shakeTarget = nullptr);

} // namespace corum::ranked::ui
