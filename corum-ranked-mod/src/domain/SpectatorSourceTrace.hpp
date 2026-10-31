#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace corum::ranked {
// Bounded observations for the actual failing Entropie window. No authority.
struct SpectatorSourceTrace {
    std::uint64_t frame=0,order=0,event=0;unsigned logs=0,criticalLogs=0;
    std::array<double,16> last{};
    static bool window(int level,double time,float x){return level==143308401&&std::isfinite(time)&&time>=70&&time<=73&&x>=25800&&x<=26000;}
    bool emit(unsigned path,int level,double time,float x,bool critical){
        if(path>=last.size()||!window(level,time,x))return false;
        if(critical){if(criticalLogs>=32)return false;++criticalLogs;return true;}
        if(logs>=160||(last[path]>0&&time-last[path]<.05))return false;
        last[path]=time;++logs;return true;
    }
};
}
