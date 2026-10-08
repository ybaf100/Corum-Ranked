#include "TransitionTrace.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace corum::ranked {

TransitionTrace& TransitionTrace::get() {
    static TransitionTrace trace;
    return trace;
}

void TransitionTrace::begin(std::string context, std::string_view event) {
    m_context = std::move(context);
    m_startedAt = Clock::now();
    m_lastAt = m_startedAt;
    log::info("[RankedTransition] context='{}' event='{}' total=0us delta=0us", m_context, event);
}

void TransitionTrace::mark(std::string_view event) {
    if (m_startedAt == Clock::time_point{}) return;
    auto const now = Clock::now();
    auto const total = std::chrono::duration_cast<std::chrono::microseconds>(now - m_startedAt).count();
    auto const delta = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastAt).count();
    log::info(
        "[RankedTransition] context='{}' event='{}' total={}us delta={}us",
        m_context,
        event,
        total,
        delta
    );
    m_lastAt = now;
}

void TransitionTrace::mark(std::string context, std::string_view event) {
    if (m_startedAt == Clock::time_point{} || context != m_context) {
        begin(std::move(context), event);
        return;
    }
    mark(event);
}

void TransitionTrace::reset() {
    m_context.clear();
    m_startedAt = {};
    m_lastAt = {};
}

} // namespace corum::ranked
