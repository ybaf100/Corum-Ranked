#include "RankedVolumeControl.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
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
    // Process lifetime, like the audio/resource managers. The scheduler is
    // installed only during a Ranked session; it carries no scene pointers.
    static auto* control = new RankedVolumeControl;
    return *control;
}

std::uint64_t RankedVolumeControl::beginSession() {
    if (m_session) endSession(m_session);
    m_session = ++m_generation;
    m_failureLogged = false;
    m_retryAt = {};
    CCScheduler::get()->scheduleUpdateForTarget(this, 0, false);
    apply();
    return m_session;
}

void RankedVolumeControl::endSession(std::uint64_t session) {
    if (!session || session != m_session) return;
    m_session = 0;
    CCScheduler::get()->unscheduleUpdateForTarget(this);
    detach();
}

void RankedVolumeControl::setVolume(float volume) {
    auto const value = normalizedVolume(volume);
    if (value == m_volume) return;
    m_volume = value;
    Mod::get()->setSavedValue<float>("ranked-volume", value);
    if (m_session) apply();
}

void RankedVolumeControl::update(float) {
    if (m_session) apply();
}

void RankedVolumeControl::apply() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;
    if (m_system && m_system != engine->m_system) {
        // A released System owns/reclaims its DSPs. Do not dereference handles
        // from that system after an audio-engine replacement.
        m_gain = nullptr; m_output = nullptr; m_appliedVolume = -1.0f;
    }
    m_system = engine->m_system;
    if (!m_gain) {
        auto const now = std::chrono::steady_clock::now();
        if (now < m_retryAt) return;
        FMOD::ChannelGroup* output = nullptr;
        FMOD::DSP* gain = nullptr;
        auto result = m_system->getMasterChannelGroup(&output);
        if (result == FMOD_OK && output) result = m_system->createDSPByType(FMOD_DSP_TYPE_FADER, &gain);
        if (result == FMOD_OK && gain) {
            // FADER defaults to 0 dB. Wet/output gain is a linear multiplier,
            // including exact silence at 0, independent of every channel's
            // setVolume and DSP fade points (both sides of a crossfade).
            result = gain->setWetDryMix(1.0f, m_volume, 0.0f);
            if (result == FMOD_OK) result = output->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, gain);
        }
        if (result != FMOD_OK || !output || !gain) {
            if (gain) gain->release();
            m_retryAt = now + std::chrono::seconds(1);
            if (!m_failureLogged) {
                m_failureLogged = true;
                log::warn("[RankedVolume] output gain unavailable result={}", static_cast<int>(result));
            }
            return;
        }
        m_output = output; m_gain = gain; m_appliedVolume = m_volume;
    }
    if (m_appliedVolume != m_volume && m_gain->setWetDryMix(1.0f, m_volume, 0.0f) == FMOD_OK)
        m_appliedVolume = m_volume;
}

void RankedVolumeControl::detach() {
    if (!m_gain) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_system != m_system) {
        m_gain = nullptr; m_output = nullptr; m_system = nullptr; m_appliedVolume = -1.0f;
        return;
    }
    // Restore unity first, even if detaching temporarily fails. Ordinary GD
    // must never inherit Ranked attenuation; only our own DSP is removed.
    m_gain->setWetDryMix(1.0f, 1.0f, 0.0f);
    m_appliedVolume = 1.0f;
    if (m_output && m_output->removeDSP(m_gain) == FMOD_OK) {
        m_gain->release();
        m_gain = nullptr; m_output = nullptr; m_system = nullptr; m_appliedVolume = -1.0f;
    }
}

} // namespace corum::ranked
