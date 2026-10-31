#pragma once
#include "../domain/Sha256.hpp"
#include <matjson.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace corum::ranked::replay {
using CollisionJson=matjson::Value;
// Runtime integers are never interpreted from their magnitude. The adapter
// must observe a real Native collision candidate AND its collision dictionary
// key matching that candidate's uniqueID before a reference is usable.
class ReplayCollisionIdentity final {
public:
    struct Object {std::uintptr_t instance=0;int runtimeId=-1;CollisionJson definition=nullptr;};
    void reset(std::vector<Object> objects){
        m_objects=std::move(objects);m_byRuntime.clear();m_witnessed.clear();m_ambiguousDefinitions.clear();m_error.clear();
        auto definitions=CollisionJson::array();std::unordered_set<std::string> seen;
        for(std::size_t i=0;i<m_objects.size();++i){auto const& e=m_objects[i];definitions.push(e.definition);
            if(!e.instance)continue;
            m_byRuntime[e.runtimeId].push_back(i);
            auto key=e.definition.dump(matjson::NO_INDENTATION);if(!seen.insert(key).second)m_ambiguousDefinitions.insert(key);
        }
        m_manifest=CollisionJson::object();m_manifest["schema"]=1;m_manifest["count"]=static_cast<std::int64_t>(m_objects.size());m_manifest["sha256"]=sha256Text(definitions.dump(matjson::NO_INDENTATION));
    }
    CollisionJson const& manifest()const{return m_manifest;}
    // Diagnostic only: bounded field/chunk hashes locate an unstable Native
    // descriptor without weakening the ordered, full manifest comparison.
    // Called once per Source Attempt/reset, never on every physics frame.
    CollisionJson diagnostics()const{
        CollisionJson result;result["manifest"]=m_manifest;
        result["fields"]=CollisionJson::object();
        char const* names[]={"objectId","startX","startY","rotationX","rotationY","scaleX","scaleY","groups"};
        for(std::size_t field=0;field<8;++field){auto values=CollisionJson::array();
            for(auto const& object:m_objects)values.push(object.definition.isArray()&&object.definition.size()>field?object.definition[field]:CollisionJson());
            result["fields"][names[field]]=sha256Text(values.dump(matjson::NO_INDENTATION));
        }
        result["chunks"]=CollisionJson::array();result["samples"]=CollisionJson::array();
        auto stride=std::max<std::size_t>(1,(m_objects.size()+31)/32);
        for(std::size_t first=0;first<m_objects.size();first+=stride){auto values=CollisionJson::array();
            for(auto i=first;i<std::min(first+stride,m_objects.size());++i)values.push(m_objects[i].definition);
            CollisionJson chunk;chunk["first"]=static_cast<std::int64_t>(first);chunk["count"]=static_cast<std::int64_t>(values.size());chunk["sha256"]=sha256Text(values.dump(matjson::NO_INDENTATION));result["chunks"].push(chunk);
        }
        // A group list may itself be large: retain only small sample definitions.
        for(std::size_t i=0;i<m_objects.size()&&result["samples"].size()<8;i+=std::max<std::size_t>(1,m_objects.size()/8)){
            CollisionJson sample;sample["index"]=static_cast<std::int64_t>(i);
            if(m_objects[i].definition.dump(matjson::NO_INDENTATION).size()<=512)sample["definition"]=m_objects[i].definition;
            result["samples"].push(sample);
        }
        result["ambiguousDefinitions"]=static_cast<std::int64_t>(m_ambiguousDefinitions.size());return result;
    }
    CollisionJson diagnosticTarget(int raw)const{
        CollisionJson result;auto at=index(raw);result["uniqueRuntimeIndex"]=at?CollisionJson(static_cast<std::int64_t>(*at)):CollisionJson();
        if(at){auto const& d=m_objects[*at].definition;
            result["ambiguousDefinition"]=m_ambiguousDefinitions.contains(d.dump(matjson::NO_INDENTATION));
            if(d.dump(matjson::NO_INDENTATION).size()<=512)result["definition"]=d;
        }return result;
    }
    std::optional<std::size_t> index(int raw)const{auto i=m_byRuntime.find(raw);return i!=m_byRuntime.end()&&i->second.size()==1?std::optional(i->second.front()):std::nullopt;}
    bool current(std::size_t index,Object const& actual)const{
        if(index>=m_objects.size())return false;
        auto const& saved=m_objects[index];
        return actual.instance&&actual.instance==saved.instance&&actual.runtimeId==saved.runtimeId&&actual.definition==saved.definition;
    }
    bool witness(int raw,std::size_t index,bool nativeCandidate,bool dictionaryKey,Object const& actual,int player=0,int side=0){
        if(player<0||player>1||side<0||side>3||raw<0||!nativeCandidate||!dictionaryKey||!current(index,actual)||actual.runtimeId!=raw||!actual.definition.isArray()||actual.definition.size()==0||actual.definition[0].asInt().unwrapOr(-1)<0)return false;
        // Identity is the map index inside the ordered, hash-verified manifest,
        // so byte-identical stacked objects (common in GD maps) stay distinct:
        // a Playback hit on the OTHER duplicate yields a different index and
        // fails the comparison. Rejecting them here only aborted valid Source
        // captures (beta.125 iOS log: index 142, CBF_COLLISION_IDENTITY_UNPROVEN
        // at 0.725 s). A runtime ID shared by two objects still fails closed.
        auto unique=this->index(raw);if(!unique||*unique!=index)return false;
        m_witnessed.insert(witnessKey(raw,player,side));return true;
    }
    std::optional<CollisionJson> reference(int raw,Object const& actual,int player=0,int side=0){
        if(raw==-1)return CollisionJson(-1);
        auto at=index(raw);
        if(player<0||player>1||side<0||side>3||raw<0||!at||!m_witnessed.contains(witnessKey(raw,player,side))){m_error="CBF_COLLISION_IDENTITY_UNPROVEN";return std::nullopt;}
        if(!current(*at,actual)){m_error="CBF_COLLISION_OBJECT_REPLACED";return std::nullopt;}
        auto result=CollisionJson::array();result.push(static_cast<std::int64_t>(*at));result.push(actual.definition[0]);return result;
    }
    std::string const& error()const{return m_error;}
private:
    static std::uint64_t witnessKey(int raw,int player,int side){return(static_cast<std::uint64_t>(raw)<<3)|(static_cast<unsigned>(player)<<2)|static_cast<unsigned>(side);}
    std::vector<Object> m_objects;
    std::unordered_map<int,std::vector<std::size_t>> m_byRuntime;
    std::unordered_set<std::uint64_t> m_witnessed;
    std::unordered_set<std::string> m_ambiguousDefinitions;
    CollisionJson m_manifest=nullptr;std::string m_error;
};
inline bool replayCollisionManifestValid(CollisionJson const& value){
    auto hash=value["sha256"].asString().unwrapOr("");
    return value["schema"].asInt().unwrapOr(0)==1&&value["count"].asInt().unwrapOr(-1)>=0&&value["count"].asInt().unwrapOr(-1)<=500000&&hash.size()==64&&std::all_of(hash.begin(),hash.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
inline bool replayCollisionReferenceValid(CollisionJson const& value){
    if(value.isNumber())return value.asInt().unwrapOr(-2)==-1;
    return value.isArray()&&value.size()==2&&value[0].asInt().unwrapOr(-1)>=0&&value[0].asInt().unwrapOr(-1)<500000&&value[1].asInt().unwrapOr(-1)>=0;
}
}
