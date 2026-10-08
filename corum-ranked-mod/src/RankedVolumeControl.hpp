#pragma once

#include <Geode/Geode.hpp>

namespace corum::ranked {

// Persisted personal gain for Ranked's own lobby/match/result BGM only.
// The audio manager applies it to its own channel group, without touching GD's
// gameplay music, sound effects or saved sliders.
class RankedVolumeControl final {
public:
    static RankedVolumeControl& get();
    float volume() const { return m_volume; }
    void setVolume(float volume);

private:
    RankedVolumeControl();
    float m_volume = 1.0f;
};

} // namespace corum::ranked
