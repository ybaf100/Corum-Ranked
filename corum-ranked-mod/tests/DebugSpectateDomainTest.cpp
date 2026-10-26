#include "../src/debug/DebugSpectatePolicy.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace corum::ranked;
    using namespace corum::ranked::debug;
    static_assert(kDebugSpectateLevelId == 88350599);
    static_assert(kSpectatorPublishIntervalMs == 50);
    assert(debugSpectateStartPercent("")==0.f);
    assert(debugSpectateStartPercent("0")==0.f&&debugSpectateStartPercent("100")==100.f);
    assert(debugSpectateStartPercent("55.5")==55.5f&&debugSpectateStartPercent("99.99").has_value());
    for(auto input:{"-1","101","nan","inf","1e2","1..2","1.",".","0.001","9999999"," 5"})assert(!debugSpectateStartPercent(input));
    DebugSpectateCapture record;
    SpectatorGhostPose pose{100,50,30,false,false};
    assert(record.capture(0,pose));
    assert(!record.capture(49,pose));
    assert(record.capture(50,pose));
    auto first=record.batch();assert(first.size()==2 && first[0].elapsedMs==0 && first[1].sequence==1);
    // ACK loss keeps the EXACT batch; no implicit deletion or resequencing.
    assert(record.batch()[0].sequence==first[0].sequence);
    pose.dead=true;assert(record.capture(51,pose,true)); // Terminal not lost between 50ms samples.
    assert(record.ended() && record.completed() && record.batch().back().pose.dead);
    assert(!record.capture(100,pose));record.acknowledge(1);assert(record.batch().size()==1);
    record.acknowledge(2);assert(record.empty());
    DebugSpectateCapture clear;pose.dead=false;pose.finished=true;
    assert(clear.capture(0,pose,true));assert(clear.completed() && clear.batch()[0].pose.finished);
    DebugSpectateCapture quit;pose.finished=false;assert(quit.capture(0,pose));quit.cancel();assert(quit.ended()&&!quit.completed());
    DebugSpectateCapture bounded;
    for(int i=0;i<256;++i)assert(bounded.capture(i*50,pose));
    assert(!bounded.capture(256*50,pose));assert(bounded.ended()&&!bounded.completed());
    assert(bounded.batch().size()==16);
    DebugSpectateCapture invalid;pose.x=std::numeric_limits<float>::infinity();
    assert(!invalid.capture(0,pose));assert(invalid.empty());
}
