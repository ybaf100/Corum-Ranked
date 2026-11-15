#pragma once

#include <cmath>

namespace corum::ranked {

// A CCLayerColor skews about its own centre (a layer places itself by its
// bottom-left corner, and its anchor is the centre): a point at height y in
// the layer moves (y - h/2)·tan(skew) to the right. Leaning the layers of one
// face separately therefore tilts each about a different height, and a thin
// rule at the top of a card stays almost upright while the card's top edge
// moves right by (H/2)·tan(skew).
//
// Shifting a layer right by this many points makes it lean about the face's
// centre instead, so every layer of the face shares one slanted outline: the
// rule sits on the top edge, a strip at the bottom on the bottom edge.
inline float leanAboutFaceCentre(float layerY, float layerHeight, float faceHeight, float skewDegrees) {
    return (layerY + layerHeight / 2.0f - faceHeight / 2.0f) * std::tan(skewDegrees * 0.017453292f);
}

} // namespace corum::ranked
