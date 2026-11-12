#pragma once

#include <functional>
#include <string>
#include <vector>

namespace corum::ranked {

// Result of proving the logged-in GD account to the Ranked server.
struct ArgonOutcome {
    std::string token;  // empty on failure
    std::string error;  // human readable reason when token is empty
};

// Argon (https://github.com/GlobedGD/argon) issues a GD account ownership token
// without ever sending the GJP or password to a third party. Tokens are stored
// globally by Argon, so after the first verification this completes at once.
// Main thread only; concurrent requests share one verification.
class RankedArgon final {
public:
    using Callback = std::function<void(ArgonOutcome const&)>;
    static RankedArgon& get();
    void request(Callback done);
    // The Ranked server refused the token: drop it so the next request re-verifies.
    void invalidate();
private:
    RankedArgon() = default;
    struct State;
    State& state();
};

} // namespace corum::ranked
