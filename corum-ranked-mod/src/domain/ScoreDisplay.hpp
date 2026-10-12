#pragma once
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace corum::ranked {
// Presentation only. Server records, ordering and cutoff comparisons keep
// their original precision. Normal GD progress displays whole percentages.
inline std::string rankedScoreText(double value,bool deathmatch=false,bool signedValue=false) {
    if(!std::isfinite(value))value=0;
    auto normalized=deathmatch?std::round(value*1000.0)/1000.0:std::trunc(value);
    if(normalized==0)normalized=0; // Avoid negative zero after precision rounding.
    std::ostringstream output;
    if(signedValue && normalized>0)output<<'+';
    if(deathmatch)output<<std::fixed<<std::setprecision(3)<<normalized;
    else output<<static_cast<long long>(normalized);
    return output.str();
}
}
