#include "RankedAudioManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/AppDelegate.hpp>

using namespace geode::prelude;

class $modify(CorumRankedAppDelegate, AppDelegate) {
    void pauseSound() {
        AppDelegate::pauseSound();
        corum::ranked::RankedAudioManager::get().onApplicationPause();
    }

    void resumeSound() {
        AppDelegate::resumeSound();
        corum::ranked::RankedAudioManager::get().onApplicationResume();
    }
};
