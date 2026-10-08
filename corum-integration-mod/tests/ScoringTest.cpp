#include "../src/Scoring.hpp"

#include <cassert>
#include <cmath>

namespace {

bool closeTo(double actual, double expected) {
    return std::abs(actual - expected) < 0.000001;
}

} // namespace

int main() {
    assert(closeTo(corum::baseScore(1), 350.0));
    assert(closeTo(corum::baseScore(2), 300.0));
    assert(closeTo(corum::baseScore(5), 220.0));
    assert(closeTo(corum::baseScore(6), 200.0));
    assert(closeTo(corum::baseScore(10), 140.0));
    assert(closeTo(corum::baseScore(11), 130.0));
    assert(closeTo(corum::baseScore(25), 50.0));
    assert(closeTo(corum::baseScore(26), 45.0));
    assert(closeTo(corum::baseScore(27), 42.3));

    assert(closeTo(corum::recordScore(1, 49.0, 50.0), 0.0));
    assert(closeTo(corum::recordScore(1, 50.0, 50.0), 35.0));
    assert(closeTo(
        corum::recordScore(1, 75.0, 50.0),
        350.0 * std::sqrt(5.0) / 10.0
    ));
    assert(closeTo(corum::recordScore(1, 100.0, 50.0), 350.0));

    assert(closeTo(corum::recordScore(1, 99.0, 0.0), 0.0));
    assert(closeTo(corum::recordScore(1, 100.0, 0.0), 350.0));

    auto const rank15Maximum = corum::baseScore(15);
    assert(closeTo(
        corum::recordScore(15, 100.0, 50.0),
        rank15Maximum
    ));
    assert(corum::recordScore(15, 75.0, 50.0) < rank15Maximum);
}
