#include "RankedVolumeControl.hpp"
#include "RankedAudioManager.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace corum::ranked {
namespace {
float normalizedVolume(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f;
}
}

RankedVolumeControl::RankedVolumeControl() {
    m_volume = normalizedVolume(Mod::get()->getSavedValue<float>("ranked-volume", 1.0f));
}

RankedVolumeControl& RankedVolumeControl::get() {
    static auto* control = new RankedVolumeControl;
    return *control;
}

void RankedVolumeControl::setVolume(float volume) {
    auto const value = normalizedVolume(volume);
    if (value == m_volume) return;
    m_volume = value;
    Mod::get()->setSavedValue<float>("ranked-volume", value);
    RankedAudioManager::get().refreshVolume();
}

} // namespace corum::ranked
