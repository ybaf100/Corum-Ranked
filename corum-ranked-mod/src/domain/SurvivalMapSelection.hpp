#pragma once
#include <algorithm>
#include <array>
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
    bool sameReveal(SurvivalSelectionContext const& other)const {
        return matchId==other.matchId && roundKey==other.roundKey && mapGeneration==other.mapGeneration;
    }
    std::string revealKey()const{return matchId+":"+roundKey+":"+std::to_string(mapGeneration);}
    std::string key() const {
        return matchId+":"+roundKey+":"+std::to_string(mapGeneration)+":"+std::to_string(levelId);
    }
};
// Slot count and occupied positions stay fixed while images arrive. Filling
// a placeholder never changes the modulo or moves an existing thumbnail.
class SurvivalThumbnailReel {
    std::array<std::uint64_t,8> m_ids{};
public:
    void observe(std::uint64_t id){
        if(!id || std::find(m_ids.begin(),m_ids.end(),id)!=m_ids.end())return;
        if(auto slot=std::find(m_ids.begin(),m_ids.end(),0);slot!=m_ids.end())*slot=id;
    }
    auto const& slots()const{return m_ids;}
};
enum class SurvivalRevealStage { PendingSelection, Rolling, Locking, Landing, Details, Settled, Cancelled };
class SurvivalRevealModeChoice {
    enum class Mode { Undecided, Thumbnail, Numeric } m_mode=Mode::Undecided;
public:
    bool choose(bool supported,bool previewsReady){
        // Network images are asynchronous. Never irrevocably select numeric at the
        // first frame solely because the cache is still empty.
        (void)previewsReady;
        if(m_mode==Mode::Undecided)m_mode=supported?Mode::Thumbnail:Mode::Numeric;
        return m_mode==Mode::Thumbnail;
    }
    void fallback(){m_mode=Mode::Numeric;}
};
struct SurvivalRevealFrame {
    SurvivalRevealStage stage=SurvivalRevealStage::Cancelled;
    double elapsed=0, landing=0, details=0, brakeAt=1.95, stopAt=2.75;
    std::size_t locked=0;
    bool settled=false, skipped=false;
};
inline double survivalEase(double v) { v=std::clamp(v,0.,1.);return 1-std::pow(1-v,3); }
class SurvivalMapSelectionTimeline {
    SurvivalSelectionContext m_context;
    std::int64_t m_began=0;
    double m_brakeAt=1.95;
    bool m_valid=false, m_skip=false;
    std::size_t m_consumedLocks=0;
public:
    static constexpr std::int64_t DurationMs=4000;
    bool observe(SurvivalSelectionContext context,std::int64_t monotonicNow) {
        if(m_valid && context.sameReveal(m_context)){
            // Resolving the server's unknown ID and recreating UI ownership
            // update metadata; neither begins a second logical selection.
            if(!m_context.levelId && context.levelId)m_brakeAt=std::max(1.95,(monotonicNow-m_began)/1000.);
            if(context.levelId==0)context.levelId=m_context.levelId;
            m_context=std::move(context);return false;
        }
        m_context=std::move(context);m_began=monotonicNow;m_brakeAt=1.95;
        m_valid=!m_context.matchId.empty() && !m_context.roundKey.empty();
        m_skip=false;m_consumedLocks=0;return true;
    }
    void restoreResolutionAge(std::int64_t age){m_brakeAt=std::max(1.95,age/1000.);}
    void cancel(){m_valid=false;m_consumedLocks=0;}
    void skip(){if(m_context.levelId>0)m_skip=true;}
    void followServer(std::string const& state,std::optional<std::int64_t> startsIn,std::int64_t now) {
        if(state=="CANCELLED" || state=="FINAL_RESULT"){cancel();return;}
        // Never spend the remaining server countdown on presentation. Reserve
        // half a second for native entry; no local deadline is invented.
        auto const remaining=std::max<std::int64_t>(0,static_cast<std::int64_t>((m_brakeAt+2.05)*1000)-(now-m_began));
        if(state=="PLAYING" || (state=="COUNTDOWN" && startsIn && *startsIn<=remaining+500))skip();
    }
    SurvivalSelectionContext const& context()const{return m_context;}
    std::string digits()const{return m_context.levelId?std::to_string(m_context.levelId):"";}
    double lockTime(std::size_t index)const {
        auto n=digits().size();auto step=n>1?std::min(.18,1.65/static_cast<double>(n-1)):0.;
        return 1.05+index*step+(m_brakeAt-1.95);
    }
    SurvivalRevealFrame frame(std::int64_t now)const {
        SurvivalRevealFrame f;if(!m_valid)return f;
        f.brakeAt=m_brakeAt;f.stopAt=m_brakeAt+.8;
        if(!m_context.levelId){f.stage=SurvivalRevealStage::PendingSelection;f.elapsed=std::max(0.,(now-m_began)/1000.);return f;}
        auto const duration=m_brakeAt+2.05;
        f.skipped=m_skip;f.elapsed=m_skip?duration:std::clamp((now-m_began)/1000.,0.,duration);
        auto const n=digits().size();for(std::size_t i=0;i<n;++i)if(f.elapsed>=lockTime(i))++f.locked;
        f.landing=survivalEase((f.elapsed-f.stopAt)/.45);f.details=std::clamp((f.elapsed-f.stopAt-.45)/.80,0.,1.);
        f.settled=f.elapsed>=duration-.000001;if(f.settled){f.landing=1;f.details=1;}
        f.stage=f.settled?SurvivalRevealStage::Settled:f.elapsed>=f.stopAt+.45?SurvivalRevealStage::Details:
            f.elapsed>=f.stopAt?SurvivalRevealStage::Landing:f.elapsed>=1.05?SurvivalRevealStage::Locking:SurvivalRevealStage::Rolling;
        return f;
    }
    std::vector<std::size_t> takeLocks(SurvivalRevealFrame const& frame,bool numericVisible) {
        std::vector<std::size_t> result;
        if(!numericVisible || frame.skipped){m_consumedLocks=std::max(m_consumedLocks,frame.locked);return result;}
        while(m_consumedLocks<frame.locked)result.push_back(m_consumedLocks++);
        return result;
    }
};
struct SurvivalThumbnailMotion {double phase=0;std::size_t winnerIndex=0;bool stopped=false;};
inline SurvivalThumbnailMotion survivalThumbnailMotion(SurvivalRevealFrame const& frame,bool selected){
    constexpr double speed=1./.14,duration=.8;
    auto const start=frame.brakeAt*speed;
    auto const finish=std::ceil(start+speed*duration/2);
    auto const distance=finish-start;
    auto const p=std::clamp((frame.elapsed-frame.brakeAt)/duration,0.,1.);
    auto const phase=!selected||frame.elapsed<frame.brakeAt?frame.elapsed*speed:frame.elapsed>=frame.stopAt?finish:
        std::min(finish,start+speed*duration*p+(3*distance-2*speed*duration)*p*p+(speed*duration-2*distance)*p*p*p);
    return {phase,static_cast<std::size_t>(finish)+2,selected&&frame.elapsed>=frame.stopAt};
}
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
