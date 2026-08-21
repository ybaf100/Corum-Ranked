#include "AttemptScoring.hpp"

#include <algorithm>
#include <cmath>

namespace corum::ranked {

double calculateAttemptScore(double progressPercent, bool cleared, double qualifyingPercent) {
    auto const progress = std::clamp(progressPercent, 0.0, 100.0);
    auto const qualifying = std::clamp(qualifyingPercent, 0.0, 100.0);

    if (cleared) return 200.0;
    if (progress < qualifying) return 0.0;

    auto const wholeProgress = std::floor(progress);
    return wholeProgress >= 70.0 ? wholeProgress * 1.5 : wholeProgress;
}

} // namespace corum::ranked
