#pragma once

// Temporary diagnostic branch only. No Tulip internals or extra native hooks.
#if defined(CORUM_CRASH_DIAG)
#include <Geode/Geode.hpp>
#include <atomic>

namespace corum::ranked::diagnostic {
inline std::atomic<unsigned> priorityFailures{0};
inline std::atomic<unsigned> modifyClasses{0};

template<class Modify>
inline void recordModifyClass(Modify& self, char const* name) {
    if (!self.m_hooks.empty()) modifyClasses.fetch_add(1, std::memory_order_relaxed);
    geode::log::info("[CorumCrashDiag] modify={} claimed-candidates={}", name, self.m_hooks.size());
}

inline void priorityFailure(char const* target) {
    priorityFailures.fetch_add(1, std::memory_order_relaxed);
    geode::log::warn("[CorumCrashDiag] priority-failed target={}", target);
}

// Geode 5.10.1 ModifyBase calls onModify BEFORE Mod::claimHook. These
// unclaimed Hook objects have not called Hook::enable / Tulip createHandler.
// Keep the PlayLayer modify type/Fields available to sibling groups, but
// exclude its entire detour set from installation for the group-E control.
template<class Modify>
inline bool excludeGameplayGroup(Modify& self, char const* name) {
#if defined(CORUM_CRASH_DIAG_NO_GAMEPLAY_HOOKS)
    geode::log::info("[CorumCrashDiag] excluded-before-claim class={} functions={}", name, self.m_hooks.size());
    self.m_hooks.clear();
    return true;
#else
    (void)self;
    (void)name;
    return false;
#endif
}

inline void traceEntry(std::atomic<unsigned>& seen, char const* target,
                       bool initialized, bool spectator, bool ranked, bool practice) {
    if (ranked || spectator || !practice) return;
    // Saturate: no steady-state atomics/logs after the first three entries.
    auto count = seen.load(std::memory_order_relaxed);
    if (count >= 3 || !seen.compare_exchange_strong(count, count + 1, std::memory_order_relaxed)) return;
    geode::log::info("[CorumCrashDiag] scene=normal-practice hook={} rankedInitialized={} rankedLevel={} spectator={} entry={}",
                    target, initialized, ranked, spectator, count + 1);
}
} // namespace corum::ranked::diagnostic

#define CORUM_CRASH_TRACE(owner, target) do { \
    static std::atomic<unsigned> seen{0}; \
    auto* diagnosticOwner = (owner); \
    if (diagnosticOwner) corum::ranked::diagnostic::traceEntry(seen, target, \
        diagnosticOwner->m_fields->rankedInitialized, \
        diagnosticOwner->m_fields->spectatorMode || diagnosticOwner->m_fields->spectatorPending, \
        diagnosticOwner->m_fields->rankedLevel, diagnosticOwner->m_isPracticeMode); \
} while (false)
#define CORUM_CRASH_PRIORITY_FAILURE(target) corum::ranked::diagnostic::priorityFailure(target)
#define CORUM_CRASH_MODIFY(self, name) corum::ranked::diagnostic::recordModifyClass(self, name)
#else
#define CORUM_CRASH_TRACE(owner, target) ((void)0)
#define CORUM_CRASH_PRIORITY_FAILURE(target) ((void)0)
#define CORUM_CRASH_MODIFY(self, name) ((void)0)
#endif
