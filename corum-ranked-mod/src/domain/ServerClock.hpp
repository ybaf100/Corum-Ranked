#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace corum::ranked {

std::optional<std::int64_t> parseIso8601Millis(std::string_view value);

class ServerClock {
public:
    bool observe(std::string_view serverNow, std::int64_t localNowMillis);
    [[nodiscard]] bool synchronized() const;
    [[nodiscard]] std::int64_t serverNowMillis(std::int64_t localNowMillis) const;
    [[nodiscard]] std::optional<std::int64_t> remainingSeconds(
        std::string_view deadlineAt,
        std::int64_t localNowMillis
    ) const;
    [[nodiscard]] std::optional<std::int64_t> remainingMillis(
        std::string_view deadlineAt,
        std::int64_t localNowMillis
    ) const;

private:
    bool m_synchronized = false;
    std::int64_t m_offsetMillis = 0;
};

} // namespace corum::ranked
