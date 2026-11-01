#include "../src/domain/SurvivalHudPresentation.hpp"
#include "../src/domain/Sha256.hpp"
#include "../src/domain/CompetitiveHudLayout.hpp"
#include "../src/domain/ScoreDisplay.hpp"
#include <cassert>
using namespace corum::ranked;
int main() {
    assert(rankedScoreText(59.46)=="59" && rankedScoreText(99.999)=="99");
    assert(rankedScoreText(94.3705,true)=="94.371" && rankedScoreText(94.370,true)=="94.370");
    assert(rankedScoreText(-.4,false,true)=="0" && rankedScoreText(-.0004,true,true)=="0.000");
    assert(rankedScoreText(8,false,true)=="+8" && rankedScoreText(-8,false,true)=="-8");
    for(auto width:{430.0f,568.0f,800.0f}){
        auto pair=competitiveClockPair(width/2,100,60);
        assert(pair.clockX==width/2-45 && pair.deltaX==width/2+45);
        assert((pair.deltaX-30)-(pair.clockX+50)==10);
    }
    assert(competitiveClockPair(284,10,10).clockX==250);
    assert(std::abs(CompetitiveClockScale-1.02f)<.0001f);
    assert(SurvivalBeforeEliminationMilliseconds+SurvivalEliminationMilliseconds+SurvivalAfterEliminationMilliseconds==5500);
    assert(SurvivalRosterMilliseconds==5500 && SurvivalBeforeEliminationMilliseconds==1950 && SurvivalEliminationMilliseconds==1550 && SurvivalAfterEliminationMilliseconds==2000);
    // Sequential eliminations: strictly increasing starts, last fall ends inside 1.55 s, 0/1/16/64 players.
    assert(survivalEliminationStarts(0).empty() && survivalEliminationStarts(1)==std::vector<int>{0});
    for(std::size_t n:{2u,6u,15u,48u,63u}){
        auto starts=survivalEliminationStarts(n);assert(starts.size()==n && starts.front()==0);
        for(std::size_t i=1;i<n;i++)assert(starts[i]>starts[i-1]);
        assert(starts.back()+SurvivalEliminationFallMilliseconds<=SurvivalEliminationMilliseconds);
    }
    {   // Reverse board order: bottom-right card first, ties in displayed order (reversed); survivors excluded.
        std::vector<SurvivalStanding> round;
        for(int i=1;i<=16;i++)round.push_back({"p"+std::to_string(100+i),"P",i,0,0,0,i<=10,false,false});
        round[12].rank=14;round[13].rank=14; // p113/p114 tie at 14, displayed p113 then p114
        auto order=survivalEliminationOrder(round);
        assert((order==std::vector<std::string>{"p116","p115","p114","p113","p112","p111"}));
        // Reported board: 14 14 14 17 18 19 20(1) 20(2) 22 -> 22, 20(2), 20(1), 19, 18, 17, 14, 14, 14.
        std::vector<SurvivalStanding> board;int n=0;
        for(int rank:{14,14,14,17,18,19,20,20,22})board.push_back({"e"+std::to_string(++n),"E",rank,0,0,0,false,false,false});
        assert((survivalEliminationOrder(board)==std::vector<std::string>{"e9","e8","e7","e6","e5","e4","e3","e2","e1"}));
        std::vector<SurvivalStanding> big;
        for(int i=1;i<=64;i++)big.push_back({"b"+std::to_string(1000+i),"B",i,0,0,0,i<=16,true,false});
        auto bigOrder=survivalEliminationOrder(big);assert(bigOrder.size()==48 && bigOrder.front()=="b1064" && bigOrder.back()=="b1017");
        std::vector<SurvivalStanding> none{{"a","A",1,0,0,0,true,false,false}};assert(survivalEliminationOrder(none).empty());

    }
    std::vector<SurvivalStanding> players;
    for(int i=1;i<=16;i++)players.push_back({std::to_string(i),"Bot "+std::to_string(i),i,0,0,100.0-i,i<=10,true,false});
    auto own=survivalHudRows(players,"15",10);assert(own.size()==11&&own.back().id=="15"&&own.back().rank==15);
    assert(survivalHudRows(players,"2",10).size()==10);
    assert(survivalHudCutoffBoundary(own)==10);
    for(auto target:{6,3,1}){
        for(auto& p:players)p.passing=p.rank<=target;
        auto rows=survivalHudRows(players,"15",target);
        assert(rows.size()==11&&rows.back().id=="15");
        assert(survivalHudCutoffBoundary(rows)==static_cast<std::size_t>(target));
        assert(survivalHudRows(players,"1",target).size()==10);
    }
    // Cutoff ties are passing, remain normal-colored, and expand the visible
    // leader list only up to ten; my own row does not duplicate an existing ID.
    for(auto& p:players)p.passing=p.rank<=6;
    players[6].rank=6;players[6].passing=true;
    auto tied=survivalHudRows(players,"7",6);assert(tied.size()==10&&tied[6].id=="7");
    assert(survivalHudCutoffBoundary(tied)==7);
    for(auto& p:players){p.rank=1;p.passing=true;}
    assert(survivalHudRows(players,"15",3).size()==11);
    assert(survivalHudRows(players,"2",3).size()==10);
    assert(survivalHudRows({},"missing",10).empty());
    assert(survivalProgressMarkerX(0,200)==0&&survivalProgressMarkerX(50,200)==100);
    assert(survivalProgressMarkerX(100,200)==200&&survivalProgressMarkerX(101,200)==200);
    assert(survivalProgressMarkerX(-1,200)==0&&survivalProgressMarkerX(NAN,200)==0);
    assert((survivalRankColor(1,true)==SurvivalHudColor{255,211,78}));
    assert((survivalRankColor(4,false)==SurvivalHudColor{255,82,105}));
    assert(survivalRankColor(4,true)!=survivalRankColor(4,false));
    for(int rank=1;rank<=16;++rank)assert((survivalRankColor(rank,false)==SurvivalHudColor{255,82,105}));
    for(int rank=6;rank<=16;++rank)assert((survivalRankColor(rank,true)==SurvivalHudColor{90,239,133}));
    assert(survivalOrdinal(1)=="1st"&&survivalOrdinal(2)=="2nd"&&survivalOrdinal(3)=="3rd");
    assert(survivalOrdinal(11)=="11th"&&survivalOrdinal(12)=="12th"&&survivalOrdinal(13)=="13th"&&survivalOrdinal(15)=="15th");
    SurvivalScoreTween score{70,70};score.observe(94);score.advance(.1);assert(score.displayed>70&&score.displayed<94);
    auto previous=score.displayed;score.observe(98);score.advance(.1);assert(score.displayed>previous&&score.displayed<98);
    score.observe(50);assert(score.target==98);score.advance(-1);assert(score.displayed>previous);
    for(int i=0;i<100;i++) { score.advance(.1); }
    assert(score.displayed==98);
    assert(sha256Text("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(sha256Text("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
