#include "../src/ClientVersion.hpp"

#include <cassert>

int main() {
    using corum::parseSemanticVersion;
    using corum::versionMeetsMinimum;

    assert(parseSemanticVersion("v1.0.0").has_value());
    assert(parseSemanticVersion("  V0.2.40  ").has_value());
    assert(parseSemanticVersion("1.0").has_value() == false);
    assert(parseSemanticVersion("1.0.0.1").has_value() == false);
    assert(parseSemanticVersion("not-a-version").has_value() == false);

    assert(versionMeetsMinimum("v1.0.0", "v1.0.0"));
    assert(versionMeetsMinimum("v1.0.1", "v1.0.0"));
    assert(versionMeetsMinimum("v0.2.40", "v0.2.9"));
    assert(!versionMeetsMinimum("v0.2.39", "v0.2.40"));
    assert(!versionMeetsMinimum("v0.2.40", "v1.0.0"));
    assert(!versionMeetsMinimum("v1.0.0-beta.1", "v1.0.0"));
    assert(versionMeetsMinimum("v1.0.0", "v1.0.0-beta.1"));

    // Invalid remote policy must fail open instead of locking every client.
    assert(versionMeetsMinimum("v0.1.0", "invalid"));
}
