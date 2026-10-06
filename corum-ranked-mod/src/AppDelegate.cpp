#include "diagnostic/CrashHookDiagnostics.hpp"
#include "RankedAudioManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/AppDelegate.hpp>

using namespace geode::prelude;

class $modify(CorumRankedAppDelegate, AppDelegate) {
    static void onModify(auto& self) {
#if defined(CORUM_CRASH_DIAG)
        if (corum::ranked::diagnostic::excludeGameplayGroup(self, "CorumRankedAppDelegate")) return;
#endif
#if defined(CORUM_CRASH_DIAG)
        CORUM_CRASH_MODIFY(self, "CorumRankedAppDelegate");
#endif
    }

    void pauseSound() {
        AppDelegate::pauseSound();
        corum::ranked::RankedAudioManager::get().onApplicationPause();
    }

    void resumeSound() {
        AppDelegate::resumeSound();
        corum::ranked::RankedAudioManager::get().onApplicationResume();
    }
};
