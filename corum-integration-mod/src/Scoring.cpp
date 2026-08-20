#include "Scoring.hpp"

#include <cmath>

namespace {

double normalizedMinimum(double minimumRecord) {
    if (
        !std::isfinite(minimumRecord) ||
        minimumRecord < 1.0 ||
        minimumRecord > 100.0
    ) {
        return 100.0;
    }
    return minimumRecord;
}

} // namespace

namespace corum {

double baseScore(int rank) {
    if (rank < 1) return 0.0;
    if (rank == 1) return 350.0;

    if (rank <= 5) {
        return 300.0 * std::pow(220.0 / 300.0, (rank - 2.0) / 3.0);
    }
    if (rank <= 10) {
        return 200.0 * std::pow(140.0 / 200.0, (rank - 6.0) / 4.0);
    }
    if (rank <= 25) {
        return 130.0 * std::pow(50.0 / 130.0, (rank - 11.0) / 14.0);
    }

    return 45.0 * std::pow(0.94, rank - 26.0);
}

double recordScore(int rank, double progress, double minimumRecord) {
    auto const score = baseScore(rank);
    auto const minimum = normalizedMinimum(minimumRecord);

    if (
        score <= 0.0 ||
        !std::isfinite(progress) ||
        progress < minimum
    ) {
        return 0.0;
    }
    if (progress >= 100.0) {
        return score;
    }

    return score *
        std::pow(5.0, (progress - minimum) / (100.0 - minimum)) /
        10.0;
}

} // namespace corum
