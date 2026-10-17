#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {
struct SurvivalSelectionContext {
    std::string matchId, roundKey;
    int mapGeneration=0;
    std::uint64_t levelId=0, sceneGeneration=0;
    bool operator==(SurvivalSelectionContext const&) const=default;
    std::string key() const {
        return matchId+":"+roundKey+":"+std::to_string(mapGeneration)+":"+std::to_string(levelId);
    }
};
enum class SurvivalRevealStage { PendingSelection, Rolling, Locking, Landing, Details, Settled, Cancelled };
struct SurvivalRevealFrame {
    SurvivalRevealStage stage=SurvivalRevealStage::Cancelled;
    double elapsed=0, landing=0, details=0;
    std::size_t locked=0;
    bool settled=false, skipped=false;
};
inline double survivalEase(double v) { v=std::clamp(v,0.,1.);return 1-std::pow(1-v,3); }
class SurvivalMapSelectionTimeline {
    SurvivalSelectionContext m_context;
    std::int64_t m_began=0;
    bool m_valid=false, m_skip=false;
    std::size_t m_consumedLocks=0;
public:
    static constexpr std::int64_t DurationMs=4000;
    bool observe(SurvivalSelectionContext context,std::int64_t monotonicNow) {
        if(m_valid && context==m_context)return false;
        m_context=std::move(context);m_began=monotonicNow;
        m_valid=!m_context.matchId.empty() && !m_context.roundKey.empty();
        m_skip=false;m_consumedLocks=0;return true;
    }
    void cancel(){m_valid=false;m_consumedLocks=0;}
    void skip(){if(m_context.levelId>0)m_skip=true;}
    void followServer(std::string const& state,std::optional<std::int64_t> startsIn,std::int64_t now) {
        if(state=="CANCELLED" || state=="FINAL_RESULT"){cancel();return;}
        // Never spend the remaining server countdown on presentation. Reserve
        // half a second for native entry; no local deadline is invented.
        auto const remaining=std::max<std::int64_t>(0,DurationMs-(now-m_began));
        if(state=="PLAYING" || (state=="COUNTDOWN" && startsIn && *startsIn<=remaining+500))skip();
    }
    SurvivalSelectionContext const& context()const{return m_context;}
    std::string digits()const{return m_context.levelId?std::to_string(m_context.levelId):"";}
    double lockTime(std::size_t index)const {
        auto n=digits().size();auto step=n>1?std::min(.18,1.65/static_cast<double>(n-1)):0.;
        return 1.05+index*step;
    }
    SurvivalRevealFrame frame(std::int64_t now)const {
        SurvivalRevealFrame f;if(!m_valid)return f;
        if(!m_context.levelId){f.stage=SurvivalRevealStage::PendingSelection;f.elapsed=std::max<std::int64_t>(0,now-m_began)/1000.;return f;}
        f.skipped=m_skip;f.elapsed=m_skip?4.:std::clamp((now-m_began)/1000.,0.,4.);
        auto const n=digits().size();for(std::size_t i=0;i<n;++i)if(f.elapsed>=lockTime(i))++f.locked;
        f.landing=survivalEase((f.elapsed-2.70)/.50);f.details=std::clamp((f.elapsed-3.20)/.80,0.,1.);
        f.settled=f.elapsed>=4.;if(f.settled){f.landing=1;f.details=1;}
        f.stage=f.settled?SurvivalRevealStage::Settled:f.elapsed>=3.2?SurvivalRevealStage::Details:
            f.elapsed>=2.7?SurvivalRevealStage::Landing:f.elapsed>=1.05?SurvivalRevealStage::Locking:SurvivalRevealStage::Rolling;
        return f;
    }
    std::vector<std::size_t> takeLocks(SurvivalRevealFrame const& frame,bool numericVisible) {
        std::vector<std::size_t> result;
        if(!numericVisible || frame.skipped){m_consumedLocks=std::max(m_consumedLocks,frame.locked);return result;}
        while(m_consumedLocks<frame.locked)result.push_back(m_consumedLocks++);
        return result;
    }
};
struct SurvivalReelFrame { int digit=0;double offset=0,impact=0;bool locked=false; };
inline SurvivalReelFrame survivalReel(SurvivalMapSelectionTimeline const& timeline,SurvivalRevealFrame const& frame,std::size_t i) {
    auto digits=timeline.digits();SurvivalReelFrame reel;
    if(i>=digits.size())return reel;
    reel.locked=i<frame.locked;
    if(reel.locked){reel.digit=digits[i]-'0';auto age=frame.elapsed-timeline.lockTime(i);
        reel.impact=age>=0 && age<.14?std::sin(age/.14*3.141592653589793):0.;
    }else{auto phase=frame.elapsed*(34.+i*3.1)+i*1.73;reel.digit=static_cast<int>(std::floor(phase))%10;reel.offset=phase-std::floor(phase);}
    return reel;
}
}
