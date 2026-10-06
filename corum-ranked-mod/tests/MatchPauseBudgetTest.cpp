#include "../src/domain/MatchPauseBudget.hpp"
#include "../src/domain/SpectatorPublishQueue.hpp"
#include <cassert>
#include <iostream>
using namespace corum::ranked;
int main() {
    MatchPauseBudget budget;
    budget.bind("match-A"); budget.begin(1000); assert(budget.remaining(13000)==18000);
    budget.end(13000); budget.bind("match-A"); // next round / new scene
    budget.observe(25000,14000); assert(budget.remaining(14000)==18000); // stale server observation cannot refill
    budget.begin(20000); budget.observe(17900,20100); budget.end(25000); assert(budget.remaining(25000)==13000);
    budget.begin(30000); assert(budget.remaining(43000)==0); budget.end(50000);
    budget.bind("match-A"); assert(budget.remaining(50000)==0); budget.bind("match-B"); assert(budget.remaining(50000)==30000);
    for(int fps : {60,120,144,240}) {
        MatchPauseBudget clock; clock.bind("same"); clock.begin(0);
        for(int i=0;i<=fps*30;++i) assert(clock.remaining(i*1000.0/fps)>=0);
        assert(clock.remaining(30000)==0);
    }
    // More than 20 Spider boundaries are captured at event time regardless of
    // an in-flight network ACK. No event is replaced with a coalesced tail.
    SpectatorPublishQueue queue; SpectatorGhostPose pose; pose.mode=SpectatorGameMode::Spider;
    for(int i=0;i<24;++i) {
        pose.x=i*30; pose.y=i%2?740:80; pose.upsideDown=i%2;
        if(i)pose.discontinuity=SpectatorSnapEvidence{SpectatorDiscontinuity::SpiderSnap,pose.x, i%2?80.0f:740.0f, pose.x,pose.y};
        assert(queue.capture(i*10,pose)); // urgent native events bypass 50ms rate gate
    }
    auto first=queue.batch(); assert(first.size()==16);queue.acknowledge(first.back().sequence);
    auto second=queue.batch();assert(second.size()==8&&second.front().sequence==16);queue.acknowledge(second.back().sequence);assert(queue.empty());
    pose.discontinuity.reset();pose.dead=true;assert(queue.capture(240,pose));assert(!queue.capture(300,pose));
    std::cout<<"Match-wide 30s pause and urgent capture tests passed\n";
}
