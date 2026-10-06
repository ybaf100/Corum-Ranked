#if defined(CORUM_CRASH_DIAG)
#include "CrashHookDiagnostics.hpp"
#include "PlayerHookSelection.hpp"
#include <Geode/loader/ModEvent.hpp>
#include <set>

using namespace geode::prelude;

$on_mod(Loaded) {
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
    log::info("[CorumCrashDiag] world-owned-presentation=true visible/special-detours=absent device-acceptance=pending");
#endif
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY)
    log::info("[CorumCrashDiag] world-owned-visibility=true set-visible-detour=absent update-special-detour=retained device-acceptance=pending");
#endif
    auto const hooks = Mod::get()->getHooks();
    std::set<std::string_view> classes;
    std::set<std::uintptr_t> targets;
    unsigned debugOnly = 0, enabled = 0;
    std::array<corum::ranked::diagnostic::PlayerHookRegistration, 7> playerHooks{};
    unsigned unknownPlayerHooks = 0;
    for (auto* hook : hooks) {
        auto const name = hook->getDisplayName();
        classes.insert(name.substr(0, name.find("::")));
        targets.insert(hook->getAddress());
        bool const isProbe = name == "EffectGameObject::triggerObject" ||
            (name.starts_with("GJBaseGameLayer::") && name != "GJBaseGameLayer::update" &&
             name != "GJBaseGameLayer::updateCamera" && name != "GJBaseGameLayer::teleportPlayer");
        // teleportPlayer has one extra debug probe detour on the same target.
        if (isProbe) ++debugOnly;
        if (hook->isEnabled()) ++enabled;
        if (name.starts_with("PlayerObject::")) {
            bool known = false;
            for (std::size_t i = 0; i < playerHooks.size(); ++i) {
                if (name != corum::ranked::diagnostic::playerHookExpectations[i].target) continue;
                known = true;
                ++playerHooks[i].registered;
                if (hook->isEnabled()) ++playerHooks[i].enabled;
            }
            if (!known) ++unknownPlayerHooks;
        }
        log::info("[CorumCrashDiag] hook={} address={:#x} enabled={} priority={}",
            name, hook->getAddress(), hook->isEnabled(), hook->getPriority());
    }
    for (std::size_t i = 0; i < playerHooks.size(); ++i) {
        auto const& expected = corum::ranked::diagnostic::playerHookExpectations[i];
        log::info("[CorumCrashDiag] player-selection target={} expected={} registered={} enabled={}",
            expected.target, expected.selected, playerHooks[i].registered, playerHooks[i].enabled);
    }
    bool const playerRegistryMatches = unknownPlayerHooks == 0 &&
        corum::ranked::diagnostic::playerHookRegistryMatches(playerHooks);
    log::info("[CorumCrashDiag] player-registry-match={} unknownPlayerHooks={}",
        playerRegistryMatches, unknownPlayerHooks);
    if (!playerRegistryMatches)
        log::warn("[CorumCrashDiag] INVALID COMPARISON: PlayerObject registry differs from compiled selection");
#if defined(CORUM_RANKED_DEBUG_SPECTATE) && !defined(CORUM_CRASH_DIAG_NO_PROBES_HOOKS)
    ++debugOnly;
#endif
    log::info("[CorumCrashDiag] variant={} version={} baseline=58d643d21734243e2f3a45e3006d27f178cad83e modifyClasses={} functionHooks={} enabled={} uniqueTargets={} targetClasses={} debugProbeHooks={} priorityFailures={}",
        CORUM_CRASH_DIAG_VARIANT, Mod::get()->getVersion().toVString(),
        corum::ranked::diagnostic::modifyClasses.load(std::memory_order_relaxed), hooks.size(), enabled,
        targets.size(), classes.size(), debugOnly,
        corum::ranked::diagnostic::priorityFailures.load(std::memory_order_relaxed));
}
#endif
