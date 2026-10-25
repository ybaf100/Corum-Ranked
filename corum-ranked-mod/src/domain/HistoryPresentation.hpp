#pragma once
#include <matjson.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {
inline std::vector<matjson::Value> historyArray(matjson::Value const& value) {
    auto const array=value.asArray();
    if(array.isOk())return array.unwrap();
    return {};
}
inline std::optional<double> historyNumber(matjson::Value const& value) {
    auto n=value.asDouble();if(n.isErr() || !std::isfinite(n.unwrap()))return {};
    return n.unwrap();
}
inline int historyLevelId(matjson::Value const& value) {
    auto n=historyNumber(value);return n && *n>0 && *n<=2147483647 && std::trunc(*n)==*n?static_cast<int>(*n):0;
}
struct HistoryStanding {
    std::string id,name,tier;int rank=0,attempts=0,tiedPlayers=0,accountId=0;bool bot=false;
    double progress=0;bool own=false,qualified=false,cleared=false,forfeited=false,cutoffBoundary=false;
};
struct HistoryRound {
    std::string key,label,title,creator,difficulty,length,result,winnerSide,grade;
    int levelId=0,stars=0,target=0,qualifiedCount=0,nextDeathmatch=0;
    std::optional<int> featureScore,epic;
    bool completed=false,deathmatch=false;
    std::optional<double> bestA,bestB,cutoff;
    std::optional<bool> ownQualified;
    std::optional<int> ownRank;
    bool clearedA=false,clearedB=false;
    std::vector<HistoryStanding> standings;
};
inline std::vector<HistoryRound> historyRounds(matjson::Value const& record,bool survival) {
    std::vector<HistoryRound> result;
    auto append=[&](matjson::Value const& r,bool dm){
        HistoryRound v;v.deathmatch=dm;v.completed=r["completed"].asBool().unwrapOr(false);
        auto const map=survival?r["map"]:r;
        v.levelId=historyLevelId(map["levelId"]);v.title=map[survival?"title":"mapTitle"].asString().unwrapOr("");
        v.creator=map["creator"].asString().unwrapOr("");v.difficulty=map["difficulty"].asString().unwrapOr("");
        v.length=map["length"].asString().unwrapOr("");v.stars=static_cast<int>(map["stars"].asInt().unwrapOr(0));
        auto const epic=map["epic"].asInt();auto const featured=map["featureScore"].asInt();
        if(epic.isOk())v.epic=static_cast<int>(epic.unwrap());
        if(featured.isOk())v.featureScore=static_cast<int>(featured.unwrap());
        if(epic.isOk())v.grade=epic.unwrap()>=3?"MYTHIC":epic.unwrap()==2?"LEGENDARY":epic.unwrap()==1?"EPIC":featured.isOk()&&featured.unwrap()>0?"FEATURED":"";
        else if(featured.isOk()&&featured.unwrap()>0)v.grade="FEATURED";
        v.result=r["result"].asString().unwrapOr("");v.winnerSide=r["winnerSide"].asString().unwrapOr("");
        if(survival){
            v.key=r["key"].asString().unwrapOr("");auto const phase=r["phase"].asString().unwrapOr("");
            auto const ordinal=static_cast<int>(r["ordinal"].asInt().unwrapOr(0));v.label=phase=="ROUND_1"?"R1":phase=="ROUND_2"?"R2":phase=="ROUND_3"?"R3":phase=="ROUND_4"?"R4":phase=="FINAL"?"FINAL":phase=="TIEBREAK"?"TB "+std::to_string(1+std::count_if(result.begin(),result.end(),[](auto const& p){return p.deathmatch;})):"R"+std::to_string(ordinal);
            v.deathmatch=phase=="TIEBREAK";v.cutoff=historyNumber(r["cutoffScore"]);
            auto const ownQualified=r["ownQualified"].asBool();if(ownQualified.isOk())v.ownQualified=ownQualified.unwrap();
            if(v.completed)if(auto rank=r["ownRank"].asInt();rank.isOk()&&rank.unwrap()>0)v.ownRank=static_cast<int>(rank.unwrap());
            v.target=static_cast<int>(r["target"].asInt().unwrapOr(0));v.qualifiedCount=static_cast<int>(r["qualifiedCount"].asInt().unwrapOr(0));
            // Unfinished rankings are never treated as historical results,
            // including a malformed or older server response.
            if(v.completed)for(auto const& p:historyArray(r["standings"])){
                HistoryStanding s;s.id=p["participantId"].asString().unwrapOr("");s.name=p["name"].asString().unwrapOr("");
                s.bot=p["isBot"].asBool().unwrapOr(false);s.tier=p["displayedTier"].asString().unwrapOr("");
                auto account=p["gdAccountId"].asString().unwrapOr("");
                if(!s.bot && !account.empty() && account.size()<=10 && std::all_of(account.begin(),account.end(),[](char c){return c>='0'&&c<='9';})){
                    auto const n=std::stoll(account);if(n>0&&n<=2147483647)s.accountId=static_cast<int>(n);
                }
                s.rank=static_cast<int>(p["rank"].asInt().unwrapOr(0));s.attempts=static_cast<int>(p["attemptCount"].asInt().unwrapOr(0));
                s.tiedPlayers=static_cast<int>(p["tiedPlayers"].asInt().unwrapOr(0));s.progress=historyNumber(p["highestProgress"]).value_or(0);
                s.own=p["own"].asBool().unwrapOr(false);s.qualified=p["qualified"].asBool().unwrapOr(false);
                s.cleared=p["cleared"].asBool().unwrapOr(false);s.forfeited=p["forfeited"].asBool().unwrapOr(false);s.cutoffBoundary=p["cutoffBoundary"].asBool().unwrapOr(false);
                v.standings.push_back(std::move(s));
            }
        }else{
            auto const n=static_cast<int>(r[dm?"sequence":"roundNumber"].asInt().unwrapOr(0));
            v.key=(dm?"dm-":"r-")+std::to_string(n);v.label=(dm?"DM ":"R")+std::to_string(n);
            v.bestA=historyNumber(r["bestProgressA"]);v.bestB=historyNumber(r["bestProgressB"]);
            v.clearedA=r["clearedA"].asBool().unwrapOr(false);v.clearedB=r["clearedB"].asBool().unwrapOr(false);
            v.nextDeathmatch=static_cast<int>(r["nextDeathmatchSequence"].asInt().unwrapOr(0));
        }
        result.push_back(std::move(v));
    };
    for(auto const& r:historyArray(record["rounds"]))if(r.isObject())append(r,false);
    if(!survival)for(auto const& r:historyArray(record["deathmatches"]))if(r.isObject())append(r,true);
    return result;
}
inline std::vector<std::string> historyResultFilters(bool solo,bool survival) {
    std::vector<std::string> result{"ALL"};
    if(solo){result.push_back("WIN");result.push_back("LOSS");}
    if(survival)for(auto value:{"CHAMPION","PODIUM","ELIMINATED","IN_PROGRESS"})result.push_back(value);
    return result;
}
struct HistoryLayout {
    float width=0,height=0,mapWidth=0,boardWidth=0,ownWidth=0;bool split=false;
};
inline bool historyReconnectNeeded(bool historyPage,bool error,bool hasSession,bool conflict) {return historyPage&&error&&!hasSession&&!conflict;}
inline float historyMapPanelHeight(float width,bool thumbnail) {return thumbnail?(width>=300?220.f:285.f):200.f;}
inline HistoryLayout historyLayout(float screenWidth,float screenHeight) {
    HistoryLayout v;v.width=std::max(160.f,std::min(920.f,screenWidth-48));v.height=std::max(80.f,screenHeight-132);
    v.split=v.width>=660;v.mapWidth=v.split?std::clamp(v.width*.24f,156.f,206.f):v.width;
    v.ownWidth=v.split?154.f:v.width;v.boardWidth=v.split?v.width-v.mapWidth-v.ownWidth-16:v.width;
    return v;
}
// Use this both before lookup and after its asynchronous response. Any queued
// or native match context blocks navigation; no scene-owned pointer is retained.
inline bool historyLevelNavigationAllowed(bool soloBusy,bool survivalBusy,bool nativePlaying) {
    return !soloBusy&&!survivalBusy&&!nativePlaying;
}
}
