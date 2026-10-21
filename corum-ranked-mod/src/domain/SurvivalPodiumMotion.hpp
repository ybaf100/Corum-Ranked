#pragma once
#include <algorithm>
#include <cmath>
namespace corum::ranked {
struct SurvivalPodiumFrame {double rise=0,drop=0,stamp=0,stampScale=1,stampOpacity=0;bool avatar=false,interactive=false;double glowOpacity=0;};
inline SurvivalPodiumFrame survivalPodiumFrame(int rank,double elapsed){
    if(rank<1 || rank>3 || !std::isfinite(elapsed))return {};
    auto const age=elapsed-(.15+(3-rank)*1.30);
    auto const unit=[](double x){return std::clamp(x,0.,1.);};
    auto const rise=unit(age/.50),fall=unit((age-.50)/.36),stamp=unit((age-.90)/.20);
    return {1-std::pow(1-rise,3),1-fall*fall,stamp,1.20-.20*(1-std::pow(1-stamp,3)),stamp,age>=.50,age>=1.10,rank==1?([&]{auto t=unit((age-1.10)/.55);return t*t*(3-2*t);}()):0};
}
}
