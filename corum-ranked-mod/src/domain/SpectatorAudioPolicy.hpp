#pragma once
#include "Sha256.hpp"
#include <string_view>
namespace corum::ranked {
// Native resource paths differ across operating systems. Never transmit them.
inline std::string spectatorTrackHash(std::string_view path){
    auto slash=path.find_last_of("/\\");return sha256Text(slash==std::string_view::npos?path:path.substr(slash+1));
}
}

#include <algorithm>
#include <cmath>
#include <string>
namespace corum::ranked {
enum class SpectatorAudioState { WaitingForAudio, InitialSeek, Ready, Playing,
    PausedForBuffering, ResyncRequired, TrackChanged, Terminal };
// All positions are from the rendered presentation sample, not the newest poll.
// A held sample cannot accumulate drift evidence or cause repeated seeks.
struct SpectatorAudioClockPolicy {
    SpectatorAudioState state=SpectatorAudioState::WaitingForAudio;
    std::string track;
    bool aligned=false;
    double lastSeek=-1e12,largeSince=-1,lastPresentation=-1;
    unsigned seeks=0;
    // P1-E metrics (aligned samples only): bounded counters, no allocation.
    double maxDrift=0,driftSum=0;unsigned driftSamples=0,failedSeeks=0;
    void reset(){*this={};}
    /** Presentation stopped (pause/seek/rebuffer/non-1x): re-align exactly once on resume. */
    void interrupt(){aligned=false;largeSince=-1;lastPresentation=-1;if(state!=SpectatorAudioState::WaitingForAudio)state=SpectatorAudioState::PausedForBuffering;}
    double averageDrift()const{return driftSamples?driftSum/driftSamples:0;}
    bool needsSeek(std::string const& hash,double expected,double actual,double presentation,double now){
        if(track!=hash){state=track.empty()?SpectatorAudioState::InitialSeek:SpectatorAudioState::TrackChanged;track=hash;aligned=false;largeSince=-1;}
        if(!std::isfinite(expected)||!std::isfinite(actual))return false;
        auto error=std::abs(expected-actual);
        if(!aligned){
            if(error<=20){aligned=true;state=SpectatorAudioState::Ready;return false;}
            return now-lastSeek>=1000;
        }
        if(presentation<=lastPresentation)return false;
        lastPresentation=presentation;
        maxDrift=std::max(maxDrift,error);driftSum+=error;++driftSamples;
        if(error<60){largeSince=-1;return false;}
        if(error<180)return false; // hysteresis: keep observing, never chase jitter
        if(largeSince<0)largeSince=now;
        if(now-largeSince<750 || now-lastSeek<2000)return false;
        state=SpectatorAudioState::ResyncRequired;return true;
    }
    void sought(double now,bool success){lastSeek=now;largeSince=-1;aligned=success;if(success){++seeks;state=SpectatorAudioState::Ready;}else ++failedSeeks;}
};
}
