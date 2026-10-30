#pragma once
#include <matjson.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace corum::ranked::replay {
using CbfJson=matjson::Value;
inline constexpr int CbfBoundarySchema=1;
inline constexpr std::size_t CbfMaxRoots=4096,CbfMaxOperations=1024;
inline std::string cbfText(CbfJson const& value,char const* key){return value[key].asString().unwrapOr("");}
inline bool cbfFinite(CbfJson const& value){auto n=value.asDouble();return n.isOk()&&std::isfinite(n.unwrap());}
inline bool cbfVectorValid(CbfJson const& value){auto a=value.asArray();if(a.isErr()||a.unwrap().size()>32)return false;for(auto const& v:a.unwrap())if(!v.isBool()&&!cbfFinite(v))return false;return true;}
inline bool cbfOperationValid(CbfJson const& op){
    auto name=cbfText(op,"op");
    if(name!="UPDATE"&&name!="ROTATE"&&name!="COLLISION"&&name!="INPUT"&&name!="SHIP"&&name!="SLERP")return false;
    auto player=op["player"].asInt();if(player.isErr()||player.unwrap()<0||player.unwrap()>1||!op["external"].isBool())return false;
    if(!cbfVectorValid(op["before"])||op["before"].size()!=13||!cbfVectorValid(op["after"])||op["after"].size()!=13)return false;
    if(op["nativeStep"].asInt().unwrapOr(-1)<0||op["nativeTick"].asInt().unwrapOr(-1)<0||!cbfFinite(op["levelTime"])||!cbfFinite(op["fraction"])||op["fraction"].asDouble().unwrap()<0||op["fraction"].asDouble().unwrap()>1)return false;
    if(name=="INPUT")return op["down"].isBool()&&op["button"].asInt().unwrapOr(0)>=1&&op["button"].asInt().unwrapOr(0)<=3&&cbfFinite(op["timestamp"]);
    if(name=="SLERP"){auto args=op["args"].asArray();return args.isOk()&&args.unwrap().size()==3&&cbfFinite(args.unwrap()[0])&&cbfFinite(args.unwrap()[1])&&cbfFinite(args.unwrap()[2])&&cbfFinite(op["result"]);}
    if(!cbfFinite(op["dt"])||op["dt"].asDouble().unwrap()<0||op["dt"].asDouble().unwrap()>240)return false;
    if(name=="COLLISION")return op["ignoreDamage"].isBool()&&op["groundRepair"].isBool();
    if(name=="SHIP")return cbfFinite(op["requestedDt"]);
    return true;
}
// Lossless references remove repeated evidence, never synthesize a state.
inline CbfJson cbfExpandedRoot(CbfJson root){
    if(!root["ops"].isArray())return root;
    for(auto& op:root["ops"])for(auto key:{"before","after"})if(op[key].isString()){
        auto reference=op[key].asString().unwrap();
        if(reference=="$before")op[key]=root["before"];
        else if(reference=="$after")op[key]=root["after"];
    }
    return root;
}
inline CbfJson cbfCompactFrame(CbfJson frame){
    for(auto& root:frame["roots"])for(auto& op:root["ops"])for(auto key:{"before","after"}){
        if(op[key]==root["before"])op[key]="$before";
        else if(op[key]==root["after"])op[key]="$after";
    }
    return frame;
}
inline bool cbfRootValid(CbfJson const& root){
    auto method=cbfText(root,"method");if(method!="UPDATE"&&method!="ROTATE")return false;
    auto player=root["player"].asInt();if(player.isErr()||player.unwrap()<0||player.unwrap()>1||!cbfFinite(root["dt"])||root["dt"].asDouble().unwrap()<0)return false;
    if(!cbfVectorValid(root["before"])||root["before"].size()!=13||!cbfVectorValid(root["after"])||root["after"].size()!=13)return false;
    if(!cbfVectorValid(root["lastPositionBefore"])||root["lastPositionBefore"].size()!=2||!cbfVectorValid(root["lastPositionAfter"])||root["lastPositionAfter"].size()!=2||!root["split"][0].isBool()||!root["split"][1].isBool())return false;
    auto ops=root["ops"].asArray();if(ops.isErr()||ops.unwrap().size()>CbfMaxOperations)return false;
    auto expanded=cbfExpandedRoot(root);
    for(auto const& op:expanded["ops"])if(!cbfOperationValid(op))return false;
    return true;
}
inline bool cbfFrameValid(CbfJson const& frame){
    if(frame["schema"].asInt().unwrapOr(0)!=CbfBoundarySchema)return false;
    auto roots=frame["roots"].asArray();if(roots.isErr()||roots.unwrap().size()>CbfMaxRoots)return false;
    for(auto const& root:roots.unwrap())if(!cbfRootValid(root))return false;
    auto clocks=frame["clocks"].asArray();if(clocks.isErr()||clocks.unwrap().size()>16)return false;
    for(auto const& clock:clocks.unwrap())if(!cbfFinite(clock["in"])||!cbfFinite(clock["out"])||!clock["queuedAfter"].isArray())return false;
    return true;
}
// Returns the first differing leaf for the single first-failure diagnostic.
inline std::string cbfFirstDifference(CbfJson const& expected,CbfJson const& actual,std::string path="$"){
    if(expected==actual)return {};
    if(expected.isObject()&&actual.isObject()){
        for(auto const& value:expected){auto key=value.getKey().value_or("");if(auto d=cbfFirstDifference(value,actual[key],path+"."+key);!d.empty())return d;}
    }else if(expected.isArray()&&actual.isArray()){
        auto const a=expected.asArray().unwrap(),b=actual.asArray().unwrap();
        if(a.size()!=b.size())return path+".length";
        for(std::size_t i=0;i<a.size();++i)if(auto d=cbfFirstDifference(a[i],b[i],path+"["+std::to_string(i)+"]");!d.empty())return d;
    }
    return path;
}
// A tape cursor verifies observed calls. Only external operations are dispatched
// by the owned driver; Native's own nested calls consume their entries naturally.
class CbfBoundaryCursor {
public:
    void reset(CbfJson root){m_root=cbfExpandedRoot(std::move(root));m_index=0;m_error.clear();m_expected=m_actual=nullptr;}
    CbfJson const* next()const{auto const ops=m_root["ops"].asArray();return ops.isOk()&&m_index<ops.unwrap().size()?&ops.unwrap()[m_index]:nullptr;}
    std::size_t index()const{return m_index;}
    std::string const& error()const{return m_error;}
    CbfJson const& expected()const{return m_expected;}CbfJson const& actual()const{return m_actual;}
    void fail(std::string reason,CbfJson expected=nullptr,CbfJson actual=nullptr){if(m_error.empty()){m_error=std::move(reason);m_expected=std::move(expected);m_actual=std::move(actual);}}
    std::size_t enter(CbfJson const& actual){
        auto const* expected=next();auto index=m_index;
        if(!expected){fail("CBF_EXTRA_NATIVE_CALL",nullptr,actual);return index;}
        for(auto key:{"op","player","external","dt","requestedDt","down","button","timestamp","ignoreDamage","groundRepair","nativeStep","nativeTick","levelTime","fraction","before","args"})
            if((*expected)[key]!=actual[key]){fail("CBF_NATIVE_CALL:"+std::string(key),*expected,actual);return index;}
        ++m_index;return index;
    }
    void leave(std::size_t index,CbfJson const& after,CbfJson result=nullptr){
        auto const ops=m_root["ops"].asArray().unwrap();if(index>=ops.size())return;
        if(ops[index]["after"]!=after)fail("CBF_NATIVE_STATE:"+cbfFirstDifference(ops[index]["after"],after),ops[index]["after"],after);
        if(!result.isNull()&&ops[index]["result"]!=result)fail("CBF_NATIVE_RESULT",ops[index]["result"],result);
    }
    void finish(){if(next())fail("CBF_UNCONSUMED_NATIVE_CALL",*next(),nullptr);}
private:
    CbfJson m_root,m_expected,m_actual;std::size_t m_index=0;std::string m_error;
};
}
