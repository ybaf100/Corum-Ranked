#include "RankedArgon.hpp"

#include <argon/argon.hpp>
#include <Geode/Geode.hpp>
#include <memory>
#include <utility>

using namespace geode::prelude;

namespace corum::ranked {

struct RankedArgon::State {
    geode::async::TaskHolder<geode::Result<std::string>> task;
    std::vector<Callback> waiters;
    bool busy = false;
    int accountId = 0;
};

RankedArgon& RankedArgon::get() {
    static RankedArgon instance;
    return instance;
}

RankedArgon::State& RankedArgon::state() {
    static auto* value = new State;
    return *value;
}

void RankedArgon::request(Callback done) {
    auto& current = state();
    current.waiters.push_back(std::move(done));
    if (current.busy) return;
    auto account = argon::getGameAccountData();
    if (!account.valid()) {
        auto waiters = std::exchange(current.waiters, {});
        ArgonOutcome outcome{{}, "Sign in to a Geometry Dash account first."};
        for (auto& waiter : waiters) waiter(outcome);
        return;
    }
    current.busy = true;
    current.accountId = account.accountId;
    argon::AuthOptions options;
    options.account = std::move(account);
    options.progress = [](argon::AuthProgress progress) {
        log::info("[RankedArgon] {}", argon::authProgressToString(progress));
    };
    current.task.spawn(argon::startAuth(std::move(options)), [](geode::Result<std::string> result) {
        auto& current = RankedArgon::get().state();
        current.busy = false;
        ArgonOutcome outcome;
        if (result.isOk()) outcome.token = std::move(result).unwrap();
        else outcome.error = std::move(result).unwrapErr();
        if (outcome.token.empty() && outcome.error.empty()) outcome.error = "Argon returned an empty token.";
        // Never log the token itself.
        log::info("[RankedArgon] verification {}", outcome.token.empty() ? "failed: " + outcome.error : std::string("ready"));
        auto waiters = std::exchange(current.waiters, {});
        for (auto& waiter : waiters) waiter(outcome);
    });
}

void RankedArgon::invalidate() {
    auto& current = state();
    if (current.accountId > 0) argon::clearToken(current.accountId);
    else argon::clearToken();
}

} // namespace corum::ranked
