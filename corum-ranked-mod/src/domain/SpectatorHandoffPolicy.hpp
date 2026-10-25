#pragma once
#include <string_view>
namespace corum::ranked {
// A delayed Start/End ACK cannot rewind the same round after a newer state poll.
inline bool acceptsRoundAckPhase(std::string_view current,std::string_view incoming){
    auto order=[](std::string_view phase){
        if(phase=="ROUND_PLAYING")return 1;
        if(phase=="FINAL_ATTEMPT_WINDOW" || phase=="LAST_ATTEMPT_WINDOW")return 2;
        if(phase=="ROUND_SETTLING")return 3;
        if(phase=="ROUND_RESULT")return 4;
        return 0;
    };
    return order(current)==0 || order(incoming)==0 || order(incoming)>=order(current);
}
}
