#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <chrono>

namespace FMOD { class System; class ChannelGroup; class DSP; }

namespace corum::ranked {

// A Ranked session includes its retained lobby and native gameplay children.
// Its own master-output DSP scales both music and SFX without changing GD's
// saved sliders or restarting, seeking or reopening any channel.
class RankedVolumeControl final : public cocos2d::CCNode {
public:
    static RankedVolumeControl& get();
    std::uint64_t beginSession();
    void endSession(std::uint64_t session);
    float volume() const { return m_volume; }
    void setVolume(float volume);
    void update(float) override;

private:
    RankedVolumeControl();
    void apply();
    void detach();
    float m_volume = 1.0f;
    std::uint64_t m_session = 0, m_generation = 0;
    FMOD::System* m_system = nullptr;
    FMOD::ChannelGroup* m_output = nullptr;
    FMOD::DSP* m_gain = nullptr;
    float m_appliedVolume = -1.0f;
    bool m_failureLogged = false;
    std::chrono::steady_clock::time_point m_retryAt {};
};

} // namespace corum::ranked
