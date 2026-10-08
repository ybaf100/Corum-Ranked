#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace corum::ranked {

class TransitionTrace final {
public:
    static TransitionTrace& get();

    void begin(std::string context, std::string_view event);
    void mark(std::string_view event);
    void mark(std::string context, std::string_view event);
    void reset();

private:
    using Clock = std::chrono::steady_clock;
    std::string m_context;
    Clock::time_point m_startedAt {};
    Clock::time_point m_lastAt {};
};

} // namespace corum::ranked
