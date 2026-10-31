#include "SurvivalReplayCbfNative.hpp"
#include "SurvivalReplayCbfTrace.hpp"
#include "SurvivalReplayCollisionIdentity.hpp"
#include <Geode/binding/GameObject.hpp>
#include <unordered_set>
#include "SurvivalReplayRuntime.hpp"
#include "../SurvivalSpectatorRuntime.hpp"
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/loader/Hook.hpp>
#include <fstream>
#include <set>
using namespace geode::prelude;
namespace corum::ranked::replay {
namespace {
struct CollisionCandidateEvidence {int objectId=-1,runtimeId=-1;float x=0,y=0;};
struct State {
    PlayLayer* layer=nullptr;Playback* playback=nullptr;
    bool frame=false,root=false,dispatching=false;int depth=0;std::uint64_t epoch=0;
    Json rootData,data;std::vector<Json> operations;CbfBoundaryCursor cursor;
    std::size_t rootIndex=0,clockIndex=0;
    std::array<CCPoint,2> previousPositions{};
    std::array<bool,2> startingGround{},split{};
    std::array<unsigned,2> updates{},totalUpdates{};std::array<float,2> simulated{};PlayerObject* shipPlayer=nullptr;PlayerObject* nativePlayer=nullptr;
    std::string fault;std::vector<Hook*> leased;
    bool collisionIdentity=false;std::array<std::unordered_set<GameObject*>,2> collisionCandidates;std::unordered_set<std::uint64_t> collisionWitnessKeys;
    std::array<std::array<CollisionCandidateEvidence,16>,2> candidateEvidence{},lastCandidateEvidence{};
    std::array<std::size_t,2> candidateEvidenceCount{},lastCandidateEvidenceCount{};
    std::array<std::array<bool,4>,2> lastCollisionKeys{};
    float shipFactor=0,shipRequested=0;
    std::uintptr_t slerpAddress=0;Hook* slerpObserver=nullptr;
};
State& state(){static auto* value=new State;return *value;}
struct InputClockState {PlayLayer* owner=nullptr;Playback* replay=nullptr;ReplayInputClock clock;bool active=false;std::uint64_t generation=0;std::size_t frame=NoClockBoundary;};
InputClockState& inputClock(){static auto* value=new InputClockState;return *value;}
void checkInputClock(){auto& s=inputClock();if(s.replay&&!s.clock.error().empty())s.replay->reject(s.clock.error(),nullptr,s.clock.observations(),"$.inputClock");}
Mod* cbfMod(){for(auto* mod:Loader::get()->getAllMods())if(mod->isLoaded()&&mod->getID()=="syzzi.click_between_frames")return mod;return nullptr;}
int playerIndex(PlayerObject* player){return state().layer&&player==state().layer->m_player2?1:0;}
PlayerObject* playerAt(int index){return !state().layer?nullptr:index?state().layer->m_player2:state().layer->m_player1;}
void fail(std::string reason,Json expected=nullptr,Json actual=nullptr);
struct CollisionWorld {
    PlayLayer* owner=nullptr;std::string error;ReplayCollisionIdentity identities;std::vector<Ref<GameObject>> retained;
};
CollisionWorld& collisionWorld(){static auto* value=new CollisionWorld;return *value;}
Json collisionDefinition(GameObject* object){
    if(!object)return nullptr;
    for(float v:{object->m_startPosition.x,object->m_startPosition.y,object->m_startRotationX,object->m_startRotationY,object->m_startScaleX,object->m_startScaleY})if(!std::isfinite(v))return nullptr;
    auto definition=Json::array();definition.push(object->m_objectID);definition.push(object->m_startPosition.x);definition.push(object->m_startPosition.y);
    for(float v:{object->m_startRotationX,object->m_startRotationY,object->m_startScaleX,object->m_startScaleY})definition.push(v);
    auto groups=Json::array();if(object->m_groupCount<0||object->m_groupCount>4096||(!object->m_groups&&object->m_groupCount))return nullptr;
    auto* values=reinterpret_cast<short const*>(object->m_groups);for(int i=0;i<object->m_groupCount;++i)groups.push(values[i]);definition.push(std::move(groups));
    return definition;
}
ReplayCollisionIdentity::Object collisionObject(PlayLayer* layer,std::size_t index){
    if(!layer||!layer->m_objects||index>=layer->m_objects->count())return {};
    auto* object=typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(static_cast<unsigned>(index)));
    return {reinterpret_cast<std::uintptr_t>(object),object?object->m_uniqueID:-1,collisionDefinition(object)};
}
void prepareCollisionWorld(PlayLayer* layer,bool reset=false){
    auto& world=collisionWorld();if(world.owner==layer&&!reset)return;
    world.retained.clear();world.error.clear();world.owner=layer;std::vector<ReplayCollisionIdentity::Object> objects;
    if(layer&&layer->m_objects&&layer->m_objects->count()>500000){world.identities.reset({});world.error="CBF_MAP_OBJECT_BUDGET";return;}
    if(layer&&layer->m_objects){objects.reserve(layer->m_objects->count());world.retained.reserve(layer->m_objects->count());
        for(unsigned i=0;i<layer->m_objects->count();++i){auto* object=typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));world.retained.emplace_back(object);objects.push_back(collisionObject(layer,i));}}
    world.identities.reset(std::move(objects));
}
std::array<int,4> collisionRaw(PlayerObject* p){return {p->m_lastCollisionBottom,p->m_lastCollisionTop,p->m_lastCollisionLeft,p->m_lastCollisionRight};}
Json collisionRawJson(PlayerObject* p){auto values=Json::array();if(p)for(auto raw:collisionRaw(p))values.push(raw);return values;}
Json collisionReference(int raw,int player,int side){
    if(raw==-1)return -1;
    auto& world=collisionWorld();auto at=world.identities.index(raw);auto current=at?collisionObject(world.owner,*at):ReplayCollisionIdentity::Object{};
    auto ref=world.identities.reference(raw,current,player,side);
    if(!ref){Json actual;actual["runtimeId"]=raw;actual["player"]=player;actual["side"]=side;actual["map"]=world.identities.manifest();actual["candidates"]=Json::array();auto const& s=state();
        for(std::size_t i=0;i<s.lastCandidateEvidenceCount[player];++i){auto const& c=s.lastCandidateEvidence[player][i];Json item;item["objectId"]=c.objectId;item["runtimeUniqueId"]=c.runtimeId;item["originX"]=c.x;item["originY"]=c.y;actual["candidates"].push(item);}
        actual["directionalDictionaryKeys"]=Json::array();for(auto key:s.lastCollisionKeys[player])actual["directionalDictionaryKeys"].push(key);
        fail(world.identities.error(),nullptr,actual);return raw;}

    return *ref;
}
struct NativeCollisionQuery {
    PlayerObject* player=nullptr;std::uint64_t epoch=0;bool engaged=false;std::unordered_set<GameObject*> previous;std::array<CollisionCandidateEvidence,16> previousEvidence{};std::size_t previousCount=0;
    explicit NativeCollisionQuery(PlayerObject* p):player(p){auto& s=state();engaged=s.frame&&s.collisionIdentity&&p&&(p==s.layer->m_player1||p==s.layer->m_player2);if(engaged){epoch=s.epoch;previous=std::move(s.collisionCandidates[playerIndex(p)]);s.collisionCandidates[playerIndex(p)].clear();previousEvidence=s.candidateEvidence[playerIndex(p)];previousCount=s.candidateEvidenceCount[playerIndex(p)];s.candidateEvidenceCount[playerIndex(p)]=0;}}
    void finish(){
        if(!engaged)return;
        engaged=false;auto& s=state();if(s.epoch!=epoch||!s.frame)return;
        auto raw=collisionRaw(player);std::array<CCDictionary*,4> logs{player->m_collisionLogBottom,player->m_collisionLogTop,player->m_collisionLogLeft,player->m_collisionLogRight};auto& world=collisionWorld();
        auto actor=playerIndex(player);s.lastCandidateEvidence[actor]=s.candidateEvidence[actor];s.lastCandidateEvidenceCount[actor]=s.candidateEvidenceCount[actor];
        for(std::size_t side=0;side<raw.size();++side){s.lastCollisionKeys[actor][side]=raw[side]>=0&&logs[side]&&logs[side]->objectForKey(raw[side]);if(!s.lastCollisionKeys[actor][side])continue;
            auto at=world.identities.index(raw[side]);if(!at)continue;auto current=collisionObject(s.layer,*at);auto* object=reinterpret_cast<GameObject*>(current.instance);
            if(world.identities.witness(raw[side],*at,s.collisionCandidates[playerIndex(player)].contains(object),true,current,actor,static_cast<int>(side))&&!s.playback){
                auto key=(static_cast<std::uint64_t>(raw[side])<<4)|(static_cast<unsigned>(playerIndex(player))<<2)|side;
                if(s.collisionWitnessKeys.insert(key).second&&s.data["collisionWitnesses"].size()<64){Json proof;proof["side"]=static_cast<int>(side);proof["player"]=playerIndex(player);proof["runtimeUniqueId"]=raw[side];proof["targetIndex"]=static_cast<std::int64_t>(*at);proof["objectId"]=object->m_objectID;proof["nativeStep"]=s.layer->m_currentStep;proof["nativeTick"]=static_cast<std::int64_t>(Recorder::get().tick());proof["callback"]="GJBaseGameLayer.checkCollisions/collisionCheckObjects";proof["dictionaryKeyObserved"]=true;s.data["collisionWitnesses"].push(proof);}
            }
        }
        s.collisionCandidates[actor]=std::move(previous);s.candidateEvidence[actor]=previousEvidence;s.candidateEvidenceCount[actor]=previousCount;
    }
    ~NativeCollisionQuery(){finish();}
};
Json motion(PlayerObject* p){
    auto v=Json::array();if(!p)return v;
    v.push(p->getPositionX());v.push(p->getPositionY());v.push(p->getRotation());v.push(p->m_yVelocity);
    v.push(p->m_shipRotation.x);v.push(p->m_shipRotation.y);v.push(p->m_jumpBuffered);v.push(p->m_isOnGround);
    int holds=0;for(int b=0;b<=3;++b){auto i=p->m_holdingButtons.find(b);if(i!=p->m_holdingButtons.end()&&i->second)holds|=1<<b;}v.push(holds);
    auto raw=collisionRaw(p);for(std::size_t side=0;side<raw.size();++side)v.push(state().collisionIdentity?collisionReference(raw[side],playerIndex(p),static_cast<int>(side)):Json(raw[side]));
    return v;
}
Json point(CCPoint p){auto v=Json::array();v.push(p.x);v.push(p.y);return v;}
void fail(std::string reason,Json expected,Json actual){auto& s=state();if(!s.fault.empty())return;s.fault=std::move(reason);if(s.playback)s.playback->reject(s.fault,std::move(expected),std::move(actual));else log::warn("[SurvivalReplayNativeEvidence] error={} nativeStep={} nativeTime={} evidence={}",s.fault,s.layer?s.layer->m_currentStep:-1,s.layer?static_cast<double>(s.layer->m_gameState.m_levelTime):0,actual.dump(matjson::NO_INDENTATION));}
bool active(){auto& s=state();return s.frame&&s.layer==PlayLayer::get()&&s.fault.empty();}
void clearCollisionLog(PlayerObject* p){
    // CBF MIT: the inlined resetCollisionLog boundary, not a pose correction.
    p->m_collisionLogTop->removeAllObjects();p->m_collisionLogBottom->removeAllObjects();
    p->m_collisionLogLeft->removeAllObjects();p->m_collisionLogRight->removeAllObjects();
    p->m_lastCollisionBottom=p->m_lastCollisionTop=p->m_lastCollisionLeft=p->m_lastCollisionRight=-1;
}
void checkCursor(){auto& s=state();if(!s.cursor.error().empty())fail(s.cursor.error(),s.cursor.expected(),s.cursor.actual());}
struct Operation {
    bool engaged=false,external=false,clear=false;std::size_t index=0;std::uint64_t epoch=0;PlayerObject* player=nullptr;PlayerObject* previousPlayer=nullptr;
    Operation(std::string name,PlayerObject* p,float dt,Json fields=Json::object()):player(p){
        auto& s=state();if(!active()||!s.root||(name!="SLERP"&&(!p||(p!=s.layer->m_player1&&p!=s.layer->m_player2))))return;
        engaged=true;epoch=s.epoch;external=s.depth==0;
        fields["rawCollisionBefore"]=collisionRawJson(p);fields["op"]=name;fields["player"]=playerIndex(p);fields["external"]=external;fields["before"]=motion(p);
        fields["nativeStep"]=s.layer->m_currentStep;fields["nativeTick"]=s.playback?s.playback->recordedTick():static_cast<std::int64_t>(Recorder::get().tick());fields["levelTime"]=static_cast<double>(s.layer->m_gameState.m_levelTime);
        auto rootDelta=number(s.rootData,"dt");fields["fraction"]=rootDelta>0?std::clamp(static_cast<double>(std::max(s.simulated[0],s.simulated[1]))/rootDelta,0.,1.):0.;
        if(name!="INPUT"&&name!="SLERP")fields["dt"]=static_cast<double>(dt);
        if(name=="COLLISION"){
            bool repair=external&&cbfText(s.rootData,"method")=="UPDATE"&&s.updates[playerIndex(p)]==1&&((p->m_yVelocity<0)^p->m_isUpsideDown);
            fields["groundRepair"]=repair;
            if(s.playback&&repair){p->m_isOnGround=s.startingGround[playerIndex(p)];fields["before"]=motion(p);}
        }
        clear=external&&name=="ROTATE"&&cbfText(s.rootData,"method")=="UPDATE";
        if(s.playback){index=s.cursor.enter(fields);checkCursor();}
        else {index=s.operations.size();if(index>=CbfMaxOperations){fail("CBF_OPERATION_BUDGET");engaged=false;return;}s.operations.push_back(std::move(fields));}
        previousPlayer=s.nativePlayer;s.nativePlayer=p;++s.depth;if(name=="UPDATE"){++s.totalUpdates[playerIndex(p)];if(external){++s.updates[playerIndex(p)];s.simulated[playerIndex(p)]+=dt;}}
    }
    void finish(Json result=nullptr){
        if(!engaged)return;
        auto& s=state();if(s.epoch!=epoch){engaged=false;return;}--s.depth;
        if(s.playback){s.cursor.leave(index,motion(player),result);checkCursor();if(clear&&s.fault.empty())clearCollisionLog(player);}
        else {s.operations[index]["rawCollisionAfter"]=collisionRawJson(player);s.operations[index]["after"]=motion(player);if(!result.isNull())s.operations[index]["result"]=std::move(result);}
        s.nativePlayer=previousPlayer;engaged=false;
    }
    ~Operation(){finish();}
};
#if defined(GEODE_IS_WINDOWS)
struct ShipRequest {
    float previous;
    explicit ShipRequest(float dt):previous(state().shipRequested){state().shipRequested=dt;}
    ~ShipRequest(){state().shipRequested=previous;}
};
float nativeShipDelta(float requested){
    auto& s=state();if(!active()||!s.playback||!s.root)return requested;
    auto const* expected=s.cursor.next();
    if(!expected||str(*expected,"op")!="SHIP"||(*expected)["requestedDt"]!=Json(static_cast<double>(requested))){fail("CBF_SHIP_REQUEST");return requested;}
    auto native=static_cast<float>(number(*expected,"dt"));
    if(native!=requested&&native!=1.f/1024.f){fail("CBF_SHIP_TIMING");return requested;}
    // CBF applies this even to a buffering/non-Dual actor during an input
    // step. Use its captured Native boundary, not the actor's split flag.
    if(native==1.f/1024.f)s.shipFactor=requested;
    return native;
}
#endif
void executeExternal(Json const& op){
    auto& s=state();auto* p=playerAt(static_cast<int>(integer(op,"player")));if(!p){fail("CBF_PLAYER_MISSING");return;}
    auto name=str(op,"op");auto dt=static_cast<float>(number(op,"dt"));
    if(name=="UPDATE")p->update(dt);
    else if(name=="ROTATE")p->updateRotation(dt);
    else if(name=="COLLISION")s.layer->checkCollisions(p,dt,op["ignoreDamage"].asBool().unwrap());
    else if(name=="INPUT")s.layer->handleButton(op["down"].asBool().unwrap(),static_cast<int>(integer(op,"button")),integer(op,"player")==0);
    else fail("CBF_UNEXPECTED_EXTERNAL_OPERATION",op);
}
struct Root {
    bool captured=false,handled=false;std::uint64_t epoch=0;
    Root(PlayerObject* p,char const* method,float dt){
        auto& s=state();if(s.frame&&s.playback&&s.layer==PlayLayer::get()&&!s.fault.empty()){handled=true;return;}
        if(!active()||s.root||!p||(p!=s.layer->m_player1&&p!=s.layer->m_player2))return;
        captured=true;epoch=s.epoch;s.root=true;s.depth=0;s.updates={};s.simulated={};s.rootData=Json::object();
        s.rootData["rawCollisionBefore"]=collisionRawJson(p);s.rootData["method"]=method;s.rootData["player"]=playerIndex(p);s.rootData["dt"]=static_cast<double>(dt);s.rootData["before"]=motion(p);s.rootData["lastPositionBefore"]=point(p->m_lastPosition);
        if(std::string_view(method)=="UPDATE"&&playerIndex(p)==0){
            s.previousPositions={playerAt(0)->getPosition(),playerAt(1)->getPosition()};s.startingGround={playerAt(0)->m_isOnGround,playerAt(1)->m_isOnGround};
        }
        if(!s.playback){s.operations.clear();return;}
        handled=true;auto roots=s.data["roots"].asArray().unwrap();
        if(s.rootIndex>=roots.size()){fail("CBF_EXTRA_ROOT",nullptr,s.rootData);return;}
        auto expected=roots[s.rootIndex++];
        if(cbfLegacyCollision(expected["before"])||cbfLegacyCollision(s.rootData["before"])){fail("CBF_LEGACY_COLLISION_IDENTITY_UNAVAILABLE",expected,s.rootData);return;}
        for(auto key:{"method","player","dt","before","lastPositionBefore"})if(expected[key]!=s.rootData[key]){fail(std::string(key)=="before"&&cbfCollisionDifference(expected[key],s.rootData[key])?"CBF_COLLISION_TARGET_MISMATCH":"CBF_ROOT:"+std::string(key),expected,s.rootData);return;}
        if(std::string_view(method)=="UPDATE"&&playerIndex(p)==0){s.split={expected["split"][0].asBool().unwrap(),expected["split"][1].asBool().unwrap()};}
        s.cursor.reset(expected);
        while(s.fault.empty()){
            auto const* next=s.cursor.next();if(!next)break;
            if(!(*next)["external"].asBool().unwrap()){fail("CBF_MISSING_PARENT_NATIVE_CALL",*next);break;}
            auto index=s.cursor.index();executeExternal(*next);if(s.epoch!=epoch)return;
            if(index==s.cursor.index()&&s.fault.empty()){fail("CBF_NATIVE_OBSERVER_NOT_CALLED");break;}
        }
        s.cursor.finish();checkCursor();
        if(std::string_view(method)=="ROTATE"&&s.split[playerIndex(p)])p->m_lastPosition=s.previousPositions[playerIndex(p)];
        if(cbfLegacyCollision(expected["after"])||cbfLegacyCollision(motion(p)))fail("CBF_LEGACY_COLLISION_IDENTITY_UNAVAILABLE",expected["after"],motion(p));
        else if(expected["after"]!=motion(p))fail(cbfCollisionDifference(expected["after"],motion(p))?"CBF_COLLISION_TARGET_MISMATCH":"CBF_ROOT_STATE:"+cbfFirstDifference(expected["after"],motion(p)),expected["after"],motion(p));
        if(expected["lastPositionAfter"]!=point(p->m_lastPosition))fail("CBF_LAST_POSITION",expected["lastPositionAfter"],point(p->m_lastPosition));
        s.rootData=std::move(expected);
    }
    ~Root(){
        if(!captured)return;
        auto& s=state();if(s.epoch!=epoch)return;
        auto p=playerAt(static_cast<int>(integer(s.rootData,"player")));
        if(!s.playback){s.rootData["rawCollisionAfter"]=collisionRawJson(p);s.rootData["after"]=motion(p);s.rootData["lastPositionAfter"]=point(p->m_lastPosition);s.rootData["ops"]=Json::array();for(auto& op:s.operations)s.rootData["ops"].push(std::move(op));}
        if(!s.playback&&cbfText(s.rootData,"method")=="UPDATE"&&integer(s.rootData,"player")==0){
            std::array<unsigned,2> count{};std::array<bool,2> fractional{};auto const ops=s.rootData["ops"].asArray().unwrap();
            for(auto const& op:ops)if(str(op,"op")=="UPDATE"&&op["external"].asBool().unwrap()){auto i=static_cast<int>(integer(op,"player"));++count[i];fractional[i]=fractional[i]||op["dt"]!=s.rootData["dt"];}
            for(int i=0;i<2;++i)s.split[i]=count[i]>1||fractional[i];
        }
        if(!s.playback){auto split=Json::array();split.push(s.split[0]);split.push(s.split[1]);s.rootData["split"]=split;if(s.data["roots"].size()>=CbfMaxRoots)fail("CBF_ROOT_BUDGET");else s.data["roots"].push(s.rootData);}
        s.root=false;s.operations.clear();
    }
};
bool leasedName(std::string_view name){
    return name=="PlayerObject::update"||name=="PlayerObject::updateRotation"||name=="PlayerObject::updateShipRotation"||name=="GJBaseGameLayer::getModifiedDelta"||name=="GJBaseGameLayer::processCommands"
#if defined(GEODE_IS_MACOS)
        ||name=="GJBaseGameLayer::update"
#endif
        ;
}
#if defined(GEODE_IS_WINDOWS)
float observeSlerp(float a,float b,float coefficient){
    auto& s=state();auto args=Json::array();args.push(a);args.push(b);
    // Same float operations as CBF's MIT Windows ship correction. Its target
    // comes from the loaded CBF Hook, never a fixed executable offset.
    if(active()&&s.playback&&s.shipFactor!=0){s.shipFactor*=coefficient*1024.f;coefficient=s.shipFactor;}
    args.push(coefficient);Json fields;fields["args"]=std::move(args);Operation op("SLERP",active()?(s.shipPlayer?s.shipPlayer:s.nativePlayer):nullptr,0,std::move(fields));
    auto result=reinterpret_cast<float(*)(float,float,float)>(s.slerpAddress)(a,b,coefficient);op.finish(result);return result;
}
#endif
}
Json cbfNativeCapability(){
    Json result;result["driver"]=CbfBoundarySchema;result["verified"]=false;auto* mod=cbfMod();if(!mod)return result;
    std::set<std::string> enabled;for(auto* hook:mod->getHooks())if(hook->isEnabled()||std::find(state().leased.begin(),state().leased.end(),hook)!=state().leased.end())enabled.insert(std::string(hook->getDisplayName()));
    bool complete=enabled.contains("PlayerObject::update")&&enabled.contains("PlayerObject::updateRotation")&&enabled.contains("GJBaseGameLayer::processCommands");
    // Prove that our source observer is inside CBF and the replay owner is
    // outside it. Existing spectator safety remains at Priority::First.
    for(auto name:{"PlayerObject::update","PlayerObject::updateRotation"
#if defined(GEODE_IS_WINDOWS)
        ,"PlayerObject::updateShipRotation"
#endif
    }){
        int cbfPriority=0;bool found=false;
        for(auto* hook:mod->getHooks())if(hook->getDisplayName()==name){cbfPriority=hook->getPriority();found=true;}
        bool outer=false,inner=false;
        for(auto* hook:Mod::get()->getHooks())if(hook->isEnabled()&&hook->getDisplayName()==name){outer|=hook->getPriority()<cbfPriority&&hook->getPriority()>Priority::First;inner|=hook->getPriority()>cbfPriority;}
        complete=complete&&found&&outer&&inner;
    }
    for(auto name:{"GJBaseGameLayer::handleButton","GJBaseGameLayer::checkCollisions"}){
        bool inner=false;for(auto* hook:Mod::get()->getHooks())if(hook->isEnabled()&&hook->getDisplayName()==name&&hook->getPriority()>0)inner=true;
        complete=complete&&inner;
    }

#if defined(GEODE_IS_MACOS)
    complete=complete&&enabled.contains("GJBaseGameLayer::update");result["platform"]="mac";
#elif defined(GEODE_IS_IOS)
    complete=complete&&enabled.contains("GJBaseGameLayer::getModifiedDelta");result["platform"]="ios";
#elif defined(GEODE_IS_ANDROID)
    complete=complete&&enabled.contains("GJBaseGameLayer::getModifiedDelta");result["platform"]="android";
#else
    complete=complete&&enabled.contains("GJBaseGameLayer::getModifiedDelta")&&enabled.contains("PlayerObject::updateShipRotation");result["platform"]="win";
#endif
#if defined(GEODE_IS_WINDOWS)
    if(!state().slerpObserver){for(auto* hook:mod->getHooks())if(hook->getDisplayName()=="Slerp2D"){
        state().slerpAddress=hook->getAddress();auto created=Mod::get()->hook(reinterpret_cast<void*>(state().slerpAddress),&observeSlerp,"CorumReplay::SlerpObserver",tulip::hook::TulipConvention::Default);
        if(created){state().slerpObserver=created.unwrap();state().slerpObserver->setPriority(Priority::Last);}break;
    }}
    bool shipObserver=false;for(auto* hook:Mod::get()->getHooks())if(hook->isEnabled()&&hook->getDisplayName()=="PlayerObject::updateShipRotation"&&hook->getPriority()>0)shipObserver=true;
    complete=complete&&shipObserver&&state().slerpObserver&&state().slerpObserver->isEnabled();result["wine"]=mod->getSavedValue<bool>("is-linux",false);if(result["wine"].asBool().unwrap())complete=false;
#endif
#if !defined(GEODE_IS_MACOS)
    bool clockObserver=false;int cbfClockPriority=0;
    for(auto* hook:mod->getHooks())if(hook->getDisplayName()=="GJBaseGameLayer::getModifiedDelta")cbfClockPriority=hook->getPriority();
    for(auto* hook:Mod::get()->getHooks())if(hook->isEnabled()&&hook->getDisplayName()=="GJBaseGameLayer::getModifiedDelta"&&hook->getPriority()<cbfClockPriority)clockObserver=true;
    complete=complete&&clockObserver;
#endif
    static std::string binaryHash;static std::filesystem::path binaryPath;
    if(binaryHash.empty()||binaryPath!=mod->getBinaryPath()){
        binaryHash.clear();binaryPath=mod->getBinaryPath();std::error_code ec;auto size=std::filesystem::file_size(binaryPath,ec);
        if(!ec&&size>0&&size<64*1024*1024){std::ifstream in(binaryPath,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});if(in.eof()||in.good())binaryHash=sha256Text(bytes);}
    }
#if defined(__aarch64__) || defined(_M_ARM64)
    result["architecture"]="arm64";
#elif defined(__arm__) || defined(_M_ARM)
    result["architecture"]="arm32";
#else
    result["architecture"]="x64";
#endif
    result["binarySha256"]=binaryHash;result["verified"]=complete&&hashValid(binaryHash);result["leaseHeld"]=cbfReplayHooksHeld();return result;
}
void beginNativeInputClock(PlayLayer* owner,Playback* replay){
    auto& s=inputClock();++s.generation;s.owner=owner;s.replay=replay;s.active=true;
    if(replay)s.clock.replay(replay->current()->clock,replay->frameEntryClock(),replay->frameExitClock());else s.clock.source();
    s.frame=s.clock.enter("FRAME",owner->m_timestamp);checkInputClock();
}
std::size_t enterNativeInputClock(char const* phase,double& timestamp){
    auto& s=inputClock();if(!s.active||s.owner!=PlayLayer::get())return NoClockBoundary;
    auto index=s.clock.enter(phase,timestamp,s.replay?s.replay->nextInputClock():std::numeric_limits<double>::quiet_NaN());checkInputClock();return index;
}
std::uint64_t nativeInputClockGeneration(){return inputClock().generation;}
void leaveNativeInputClock(std::size_t index,double& timestamp,std::uint64_t generation){auto& s=inputClock();if(!s.active||(generation&&generation!=s.generation))return;s.clock.leave(index,timestamp);checkInputClock();}
Json finishNativeInputClock(){auto& s=inputClock();if(!s.active||!s.owner)return nullptr;s.clock.leave(s.frame,s.owner->m_timestamp);auto result=s.clock.finish();checkInputClock();s.active=false;return result;}
Json nativeInputClockDiagnostics(){return inputClock().clock.observations();}
std::string nativeInputClockError(){return inputClock().clock.error();}
std::string acquireCbfReplayHooks(Json const& environment){
    auto& s=state();if(!environment["cbf"]["loaded"].asBool().unwrapOr(false)||environment["cbf"]["disabled"].asBool().unwrapOr(true))return {};
    if(!s.leased.empty())return "CBF_REPLAY_LEASE_ALREADY_HELD";
    auto* mod=cbfMod();if(!mod)return "CBF_MOD_MISSING";
    auto capability=cbfNativeCapability();if(!capability["verified"].asBool().unwrapOr(false))return "CBF_NATIVE_BOUNDARIES_UNAVAILABLE";
    for(auto* hook:mod->getHooks())if(leasedName(hook->getDisplayName())){
        if(!hook->isEnabled()){releaseCbfReplayHooks();return "CBF_PHYSICS_HOOK_ALREADY_DISABLED";}
        if(auto result=hook->disable();!result){releaseCbfReplayHooks();return "CBF_REPLAY_LEASE_FAILED";}s.leased.push_back(hook);
    }
    return {};
}
void releaseCbfReplayHooks(){
    auto& s=state();std::vector<Hook*> failed;
    for(auto it=s.leased.rbegin();it!=s.leased.rend();++it)if(auto result=(*it)->enable();!result){failed.push_back(*it);log::error("[SurvivalReplayCBF] CBF hook restoration failed: {}",(*it)->getDisplayName());}
    s.leased=std::move(failed);++s.epoch;s.frame=s.root=false;s.layer=nullptr;s.playback=nullptr;s.nativePlayer=s.shipPlayer=nullptr;s.depth=0;
}
bool cbfReplayHooksHeld(){return !state().leased.empty();}
void beginCbfNativeFrame(PlayLayer* layer,Playback* replay){
    auto& s=state();prepareCollisionWorld(layer);++s.epoch;s.layer=layer;s.playback=replay;s.frame=true;s.root=false;s.depth=0;s.nativePlayer=s.shipPlayer=nullptr;s.rootIndex=s.clockIndex=0;s.totalUpdates={};s.fault.clear();s.operations.clear();s.collisionWitnessKeys.clear();for(auto& c:s.collisionCandidates)c.clear();s.candidateEvidenceCount={};s.lastCandidateEvidenceCount={};s.lastCollisionKeys={};
    if(!collisionWorld().error.empty())fail(collisionWorld().error);
    if(replay){s.data=replay->current()->cbf;s.collisionIdentity=s.data["objectIdentity"].isObject();if(!cbfFrameValid(s.data))fail("CBF_FRAME_TAPE_INVALID");
        if(s.collisionIdentity&&(s.data["objectIdentity"]!=collisionWorld().identities.manifest()||s.data["objectIdentity"]!=replay->initial()["cbfInitial"]["objects"]))fail("CBF_MAP_OBJECT_IDENTITY_MISMATCH",s.data["objectIdentity"],collisionWorld().identities.manifest());}
    else{s.collisionIdentity=true;s.data=Json::object();s.data["objectIdentity"]=collisionWorld().identities.manifest();s.data["collisionWitnesses"]=Json::array();s.data["kind"]="cbf";s.data["schema"]=CbfBoundarySchema;s.data["roots"]=Json::array();s.data["clocks"]=Json::array();}
}
void noteCbfNativeCollisionCandidates(PlayLayer* owner,PlayerObject* player,GameObject* const* objects,std::size_t count){
    auto& s=state();if(!s.frame||!s.collisionIdentity||s.layer!=owner||!player||(player!=owner->m_player1&&player!=owner->m_player2)||!objects)return;
    auto& candidates=s.collisionCandidates[playerIndex(player)];for(std::size_t i=0;i<count;++i)if(objects[i]){if(!candidates.contains(objects[i])&&candidates.size()>=8192){fail("CBF_COLLISION_CANDIDATE_BUDGET");return;}auto inserted=candidates.insert(objects[i]).second;auto actor=playerIndex(player);if(inserted&&s.candidateEvidenceCount[actor]<16){auto* object=objects[i];s.candidateEvidence[actor][s.candidateEvidenceCount[actor]++]={object->m_objectID,object->m_uniqueID,object->m_startPosition.x,object->m_startPosition.y};}}
}
void retireNativeReplayFrame(PlayLayer* owner){
    auto& world=collisionWorld();if(world.owner==owner){world.retained.clear();world.owner=nullptr;world.identities.reset({});}
    auto& c=inputClock();if(c.owner==owner){++c.generation;c.active=false;c.owner=nullptr;c.replay=nullptr;}
    auto& s=state();if(s.layer==owner){++s.epoch;s.frame=s.root=false;s.layer=nullptr;s.playback=nullptr;s.nativePlayer=s.shipPlayer=nullptr;s.depth=0;}
}
Json finishCbfNativeFrame(){auto& s=state();if(!s.frame)return nullptr;if(s.playback){if(s.rootIndex!=s.data["roots"].size()||s.clockIndex!=s.data["clocks"].size())fail("CBF_UNCONSUMED_FRAME_BOUNDARIES");}s.frame=s.root=false;return s.playback?s.data:cbfCompactFrame(s.data);}
std::string cbfNativeFrameError(){return state().fault;}
Json cbfNativeUpdateCounts(){auto v=Json::array();for(auto n:state().totalUpdates)v.push(static_cast<std::int64_t>(n));return v;}
Json captureCbfInitial(PlayLayer* layer,bool newSourceAttempt){
    Json value;value["schema"]=CbfBoundarySchema;value["players"]=Json::array();
    if(!layer)return value;
    if(newSourceAttempt&&(layer!=PlayLayer::get()||state().frame))return value;
    prepareCollisionWorld(layer,newSourceAttempt);value["objects"]=collisionWorld().identities.manifest();
    for(auto* p:{layer->m_player1,layer->m_player2}){if(!p){value["players"].push(nullptr);continue;}Json item;item["lastPosition"]=point(p->m_lastPosition);auto logs=Json::array();for(auto* log:{p->m_collisionLogTop,p->m_collisionLogBottom,p->m_collisionLogLeft,p->m_collisionLogRight})logs.push(log?static_cast<int>(log->count()):-1);item["collisionLogs"]=logs;value["players"].push(item);}return value;
}
std::string prepareCbfInitialCollisionLogs(PlayLayer* layer,Json const& value,bool nativeResetOwned){
    if(!nativeResetOwned||!layer||layer!=PlayLayer::get())return "CBF_INITIAL_RESET_NOT_OWNED";
    // The supported initial schema contains empty dictionaries only. Restore
    // that explicitly recorded state after a completed owned Native reset;
    // an unknown/nonempty source log can never authorize clearing anything.
    if(!cbfInitialDataValid(value))return "CBF_INITIAL_STATE_UNAVAILABLE";
    for(auto* p:{layer->m_player1,layer->m_player2})if(!p||!p->m_collisionLogTop||!p->m_collisionLogBottom||!p->m_collisionLogLeft||!p->m_collisionLogRight)return "CBF_INITIAL_COLLISION_LOG_UNAVAILABLE";
    prepareCollisionWorld(layer,true);
    if(!collisionWorld().error.empty())return collisionWorld().error;
    if(value["objects"].isObject()&&value["objects"]!=collisionWorld().identities.manifest())return "CBF_MAP_OBJECT_IDENTITY_MISMATCH";
    auto before=captureCbfInitial(layer);
    for(auto* p:{layer->m_player1,layer->m_player2})for(auto* log:{p->m_collisionLogTop,p->m_collisionLogBottom,p->m_collisionLogLeft,p->m_collisionLogRight})log->removeAllObjects();
    log::info("[SurvivalReplayCollisionReset] owned=true before={} expected={} after={} rawP1={} rawP2={}",before.dump(matjson::NO_INDENTATION),value.dump(matjson::NO_INDENTATION),captureCbfInitial(layer).dump(matjson::NO_INDENTATION),collisionRawJson(layer->m_player1).dump(matjson::NO_INDENTATION),collisionRawJson(layer->m_player2).dump(matjson::NO_INDENTATION));
    // lastCollision scalars are restored separately from the verified initial
    // player snapshot; never erase live collision targets to pass a frame.
    return {};
}
std::string validateCbfInitial(PlayLayer* layer,Json const& value){
    if(!cbfInitialDataValid(value))return "CBF_INITIAL_STATE_UNAVAILABLE";
    auto current=captureCbfInitial(layer);for(int i=0;i<2;++i){if(!replayPhysicsPointValid(value["players"][i]["lastPosition"]))return "CBF_INITIAL_POSITION_INVALID";if(value["players"][i]["collisionLogs"]!=current["players"][i]["collisionLogs"])return "CBF_INITIAL_COLLISION_LOG_MISMATCH";}
    return {};
}
void restoreCbfInitial(PlayLayer* layer,Json const& value){auto& s=state();s.split={};s.previousPositions={};s.shipFactor=0;for(int i=0;i<2;++i){auto p=value["players"][i]["lastPosition"].asArray().unwrap();(i?layer->m_player2:layer->m_player1)->m_lastPosition=CCPoint(static_cast<float>(p[0].asDouble().unwrap()),static_cast<float>(p[1].asDouble().unwrap()));}}
}
// The early boundary surrounds CBF; the late boundary observes the actual
// native calls made by CBF, including its intermediate updates and rotations.
class $modify(CorumCbfReplayRoot,PlayerObject){
    static void onModify(auto& self){for(auto name:{"PlayerObject::update","PlayerObject::updateRotation"
#if defined(GEODE_IS_WINDOWS)
        ,"PlayerObject::updateShipRotation"
#endif
    })if(!self.setHookPriorityPre(name,Priority::Early))log::error("CBF replay root priority unavailable: {}",name);}
    void update(float dt){corum::ranked::replay::Root root(this,"UPDATE",dt);if(!root.handled)PlayerObject::update(dt);}
    void updateRotation(float dt){corum::ranked::replay::Root root(this,"ROTATE",dt);if(!root.handled)PlayerObject::updateRotation(dt);}
#if defined(GEODE_IS_WINDOWS)
    void updateShipRotation(float dt){corum::ranked::replay::ShipRequest request(dt);PlayerObject::updateShipRotation(dt);}
#endif
};
class $modify(CorumCbfReplayNativePlayer,PlayerObject){
    static void onModify(auto& self){for(auto name:{"PlayerObject::update","PlayerObject::updateRotation"
#if defined(GEODE_IS_WINDOWS)
        ,"PlayerObject::updateShipRotation"
#endif
    })if(!self.setHookPriorityPre(name,Priority::Last))log::error("CBF Native observer priority unavailable: {}",name);}
    void update(float dt){corum::ranked::replay::Operation operation("UPDATE",this,dt);PlayerObject::update(dt);}
    void updateRotation(float dt){corum::ranked::replay::Operation operation("ROTATE",this,dt);PlayerObject::updateRotation(dt);}
#if defined(GEODE_IS_WINDOWS)
    void updateShipRotation(float dt){using namespace corum::ranked::replay;auto& s=state();auto requested=dt;
        auto* previousPlayer=s.shipPlayer;s.shipPlayer=this;
        dt=nativeShipDelta(requested);Json fields;fields["requestedDt"]=static_cast<double>(s.shipRequested);
        Operation operation("SHIP",this,dt,std::move(fields));PlayerObject::updateShipRotation(dt);s.shipFactor=0;s.shipPlayer=previousPlayer;
    }
#endif
};
class $modify(CorumCbfReplayNativeWorld,GJBaseGameLayer){
    static void onModify(auto& self){
        (void)self.setHookPriorityPre("GJBaseGameLayer::checkCollisions",Priority::Last);
        (void)self.setHookPriorityPre("GJBaseGameLayer::handleButton",Priority::Last);
#if !defined(GEODE_IS_MACOS)
        (void)self.setHookPriorityPre("GJBaseGameLayer::getModifiedDelta",Priority::First);
#endif
    }
    int checkCollisions(PlayerObject* p,float dt,bool ignoreDamage){corum::ranked::replay::Json fields;fields["ignoreDamage"]=ignoreDamage;corum::ranked::replay::Operation operation("COLLISION",p,dt,std::move(fields));corum::ranked::replay::NativeCollisionQuery query(p);auto result=GJBaseGameLayer::checkCollisions(p,dt,ignoreDamage);query.finish();return result;}
    void handleButton(bool down,int button,bool player1){corum::ranked::replay::Json fields;fields["down"]=down;fields["button"]=button;fields["timestamp"]=m_timestamp;corum::ranked::replay::Operation operation("INPUT",player1?m_player1:m_player2,0,std::move(fields));GJBaseGameLayer::handleButton(down,button,player1);}
#if !defined(GEODE_IS_MACOS)
    double getModifiedDelta(float dt){using namespace corum::ranked::replay;NativeInputClockScope clockScope("DELTA",m_timestamp);auto result=GJBaseGameLayer::getModifiedDelta(dt);auto& s=state();if(!active())return result;
        Json clock;clock["in"]=static_cast<double>(dt);clock["out"]=result;clock["queuedAfter"]=encodeReplayButtons(m_queuedButtons);
        if(s.playback){result=static_cast<float>(result);clock["out"]=result;auto clocks=s.data["clocks"].asArray().unwrap();if(s.clockIndex>=clocks.size()){fail("CBF_EXTRA_CLOCK");return result;}auto expected=clocks[s.clockIndex++];if(expected["in"]!=clock["in"]||expected["out"]!=clock["out"]){fail("CBF_NATIVE_CLOCK",expected,clock);return result;}
            // Explicit source CBF queue handoff, not inferred consumption.
            auto queued=expected["queuedAfter"].asArray().unwrap();m_queuedButtons.clear();for(auto const& input:queued)m_queuedButtons.push_back(decodeReplayButton<PlayerButtonCommand>(input));
        }else s.data["clocks"].push(clock);return result;
    }
#endif
};
