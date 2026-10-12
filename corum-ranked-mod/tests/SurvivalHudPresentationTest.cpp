#include "../src/domain/SurvivalHudPresentation.hpp"
#include "../src/domain/Sha256.hpp"
#include <cassert>
using namespace corum::ranked;
int main() {
    std::vector<SurvivalStanding> players;
    for(int i=1;i<=16;i++)players.push_back({std::to_string(i),"Bot "+std::to_string(i),i,0,0,100.0-i,i<=10,true,false});
    auto own=survivalHudRows(players,"15",false);assert(own.size()==4&&own.back().id=="15"&&own.back().rank==15);
    assert(survivalHudRows(players,"2",false).size()==3);
    assert(survivalHudRows(players,"15",true).size()==16);
    players[3].rank=3;players[3].passing=true;
    assert(survivalHudRows(players,"4",false).size()==3&&survivalHudRows(players,"4",false).back().id=="4");
    assert((survivalRankColor(1,true)==SurvivalHudColor{255,211,78}));
    assert((survivalRankColor(4,false)==SurvivalHudColor{255,82,105}));
    assert(survivalRankColor(4,true)!=survivalRankColor(4,false));
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
