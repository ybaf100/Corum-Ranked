#pragma once

namespace corum::ranked {

struct PlacementStageMergeTimeline {
    float sourceDelay = 0.16f;
    float sourceMove = 0.16f;
    float sourceFade = 0.12f;
    float finalReveal = 0.44f;
    float popUp = 0.14f;
    float popSettle = 0.12f;
    float controlsUnlock = 0.76f;
};

[[nodiscard]] constexpr PlacementStageMergeTimeline placementStageMergeTimeline() {
    return {};
}

[[nodiscard]] constexpr bool validPlacementStageMergeTimeline(
    PlacementStageMergeTimeline const& timeline
) {
    return timeline.sourceDelay >= 0.0f &&
        timeline.sourceMove > 0.0f &&
        timeline.sourceFade > 0.0f &&
        timeline.finalReveal >= timeline.sourceDelay + timeline.sourceMove &&
        timeline.controlsUnlock >=
            timeline.finalReveal + timeline.popUp + timeline.popSettle;
}

} // namespace corum::ranked
