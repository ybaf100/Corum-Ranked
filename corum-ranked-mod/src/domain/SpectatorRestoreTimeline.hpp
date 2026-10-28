#pragma once
#include "SpectatorRemotePolicy.hpp"
#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

namespace corum::ranked {
// Validated protocol history only. No pointer, engine-state blob or gameplay event.
class SpectatorRestoreTimeline {
public:
    static constexpr std::size_t kMaxSamples=4096,kStepsPerFrame=8;
    void begin(SpectatorRemoteContext context,bool available){
        reset();m_context=std::move(context);m_active=available;
    }
    bool push(std::int64_t sequence,double elapsed,SpectatorGhostPose pose,bool snap){
        if(!m_active || !m_context || m_queue.size()>=kMaxSamples || sequence!=m_lastSequence+1 ||
           !std::isfinite(elapsed) || elapsed<m_lastTime || !finiteSpectatorPose(pose) ||
           m_terminal || (m_lastSequence<0 && elapsed>250))return false;
        if(snap)++m_segment;
        m_queue.push_back({*m_context,sequence,pose,elapsed,m_segment});
        m_lastSequence=sequence;m_lastTime=elapsed;m_terminal=pose.dead || pose.finished;return true;
    }
    void networkCaughtUp(bool ready){m_caughtUp=ready;finishIfCaughtUp();}
    std::vector<SpectatorRemoteFrame> takeBatch(std::size_t maxSteps=kStepsPerFrame){
        std::vector<SpectatorRemoteFrame> batch;
        if(!m_active)return batch;
        for(std::size_t i=0;i<std::min(kStepsPerFrame,maxSteps) && !m_queue.empty();++i){batch.push_back(std::move(m_queue.front()));m_queue.pop_front();}
        return batch;
    }
    bool applied(SpectatorRemoteFrame const& frame){
        if(!m_active || !m_context || frame.context!=*m_context || frame.sequence>m_lastSequence ||
           frame.sequence!=static_cast<std::int64_t>(m_applied) ||
           (m_presented && frame.sequence<=m_presented->sequence))return false;
        m_presented=frame;++m_applied;
        finishIfCaughtUp();
        return true;
    }
    std::optional<SpectatorRemoteFrame> frame()const{
        if(m_presented)return m_presented;
        return m_queue.empty()?std::nullopt:std::optional(m_queue.front());
    }
    bool active()const{return m_active;}
    std::size_t queued()const{return m_queue.size();}
    std::uint64_t appliedCount()const{return m_applied;}
    void reset(){m_context.reset();m_presented.reset();m_queue.clear();m_active=m_caughtUp=m_terminal=false;m_lastSequence=-1;m_lastTime=0;m_segment=m_applied=0;}
private:
    void finishIfCaughtUp(){if(m_caughtUp&&m_queue.empty()&&m_presented&&m_presented->sequence==m_lastSequence)m_active=false;}
    std::optional<SpectatorRemoteContext> m_context;
    std::optional<SpectatorRemoteFrame> m_presented;
    std::deque<SpectatorRemoteFrame> m_queue;
    bool m_active=false,m_caughtUp=false,m_terminal=false;
    std::int64_t m_lastSequence=-1;double m_lastTime=0;
    std::uint64_t m_segment=0,m_applied=0;
};
}
