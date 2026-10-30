#include "SurvivalReplayCbfNative.hpp"
#include "SurvivalReplayCbfTrace.hpp"
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
struct State {
    PlayLayer* layer=nullptr;Playback* playback=nullptr;
    bool frame=false,root=false,dispatching=false;int depth=0;
    Json rootData,data;std::vector<Json> operations;CbfBoundaryCursor cursor;
    std::size_t rootIndex=0,clockIndex=0;
    std::array<CCPoint,2> previousPositions{};
    std::array<bool,2> startingGround{},split{};
    std::array<unsigned,2> updates{},totalUpdates{};std::array<float,2> simulated{};PlayerObject* shipPlayer=nullptr;PlayerObject* nativePlayer=nullptr;
    std::string fault;std::vector<Hook*> leased;
    float shipFactor=0,shipRequested=0;
    std::uintptr_t slerpAddress=0;Hook* slerpObserver=nullptr;
};
State& state(){static auto* value=new State;return *value;}
Mod* cbfMod(){for(auto* mod:Loader::get()->getAllMods())if(mod->isLoaded()&&mod->getID()=="syzzi.click_between_frames")return mod;return nullptr;}
int playerIndex(PlayerObject* player){return state().layer&&player==state().layer->m_player2?1:0;}
PlayerObject* playerAt(int index){return !state().layer?nullptr:index?state().layer->m_player2:state().layer->m_player1;}
Json motion(PlayerObject* p){
    auto v=Json::array();if(!p)return v;
    v.push(p->getPositionX());v.push(p->getPositionY());v.push(p->getRotation());v.push(p->m_yVelocity);
    v.push(p->m_shipRotation.x);v.push(p->m_shipRotation.y);v.push(p->m_jumpBuffered);v.push(p->m_isOnGround);
    int holds=0;for(int b=0;b<=3;++b){auto i=p->m_holdingButtons.find(b);if(i!=p->m_holdingButtons.end()&&i->second)holds|=1<<b;}v.push(holds);
    v.push(p->m_lastCollisionBottom);v.push(p->m_lastCollisionTop);v.push(p->m_lastCollisionLeft);v.push(p->m_lastCollisionRight);return v;
}
Json point(CCPoint p){auto v=Json::array();v.push(p.x);v.push(p.y);return v;}
void fail(std::string reason,Json expected=nullptr,Json actual=nullptr){auto& s=state();if(!s.fault.empty())return;s.fault=std::move(reason);if(s.playback)s.playback->reject(s.fault,std::move(expected),std::move(actual));}
bool active(){auto& s=state();return s.frame&&s.layer==PlayLayer::get()&&s.fault.empty();}
void clearCollisionLog(PlayerObject* p){
    // CBF MIT: the inlined resetCollisionLog boundary, not a pose correction.
    p->m_collisionLogTop->removeAllObjects();p->m_collisionLogBottom->removeAllObjects();
    p->m_collisionLogLeft->removeAllObjects();p->m_collisionLogRight->removeAllObjects();
    p->m_lastCollisionBottom=p->m_lastCollisionTop=p->m_lastCollisionLeft=p->m_lastCollisionRight=-1;
}
void checkCursor(){auto& s=state();if(!s.cursor.error().empty())fail(s.cursor.error(),s.cursor.expected(),s.cursor.actual());}
struct Operation {
    bool engaged=false,external=false,clear=false;std::size_t index=0;PlayerObject* player=nullptr;PlayerObject* previousPlayer=nullptr;
    Operation(std::string name,PlayerObject* p,float dt,Json fields=Json::object()):player(p){
        auto& s=state();if(!active()||!s.root||(name!="SLERP"&&(!p||(p!=s.layer->m_player1&&p!=s.layer->m_player2))))return;
        engaged=true;external=s.depth==0;
        fields["op"]=name;fields["player"]=playerIndex(p);fields["external"]=external;fields["before"]=motion(p);
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
        auto& s=state();--s.depth;
        if(s.playback){s.cursor.leave(index,motion(player),result);checkCursor();if(clear&&s.fault.empty())clearCollisionLog(player);}
        else {s.operations[index]["after"]=motion(player);if(!result.isNull())s.operations[index]["result"]=std::move(result);}
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
    bool captured=false,handled=false;
    Root(PlayerObject* p,char const* method,float dt){
        auto& s=state();if(s.frame&&s.playback&&s.layer==PlayLayer::get()&&!s.fault.empty()){handled=true;return;}
        if(!active()||s.root||!p||(p!=s.layer->m_player1&&p!=s.layer->m_player2))return;
        captured=true;s.root=true;s.depth=0;s.updates={};s.simulated={};s.rootData=Json::object();
        s.rootData["method"]=method;s.rootData["player"]=playerIndex(p);s.rootData["dt"]=static_cast<double>(dt);s.rootData["before"]=motion(p);s.rootData["lastPositionBefore"]=point(p->m_lastPosition);
        if(std::string_view(method)=="UPDATE"&&playerIndex(p)==0){
            s.previousPositions={playerAt(0)->getPosition(),playerAt(1)->getPosition()};s.startingGround={playerAt(0)->m_isOnGround,playerAt(1)->m_isOnGround};
        }
        if(!s.playback){s.operations.clear();return;}
        handled=true;auto roots=s.data["roots"].asArray().unwrap();
        if(s.rootIndex>=roots.size()){fail("CBF_EXTRA_ROOT",nullptr,s.rootData);return;}
        auto expected=roots[s.rootIndex++];
        for(auto key:{"method","player","dt","before","lastPositionBefore"})if(expected[key]!=s.rootData[key]){fail("CBF_ROOT:"+std::string(key),expected,s.rootData);return;}
        if(std::string_view(method)=="UPDATE"&&playerIndex(p)==0){s.split={expected["split"][0].asBool().unwrap(),expected["split"][1].asBool().unwrap()};}
        s.cursor.reset(expected);
        while(s.fault.empty()){
            auto const* next=s.cursor.next();if(!next)break;
            if(!(*next)["external"].asBool().unwrap()){fail("CBF_MISSING_PARENT_NATIVE_CALL",*next);break;}
            auto index=s.cursor.index();executeExternal(*next);
            if(index==s.cursor.index()&&s.fault.empty()){fail("CBF_NATIVE_OBSERVER_NOT_CALLED");break;}
        }
        s.cursor.finish();checkCursor();
        if(std::string_view(method)=="ROTATE"&&s.split[playerIndex(p)])p->m_lastPosition=s.previousPositions[playerIndex(p)];
        if(expected["after"]!=motion(p))fail("CBF_ROOT_STATE:"+cbfFirstDifference(expected["after"],motion(p)),expected["after"],motion(p));
        if(expected["lastPositionAfter"]!=point(p->m_lastPosition))fail("CBF_LAST_POSITION",expected["lastPositionAfter"],point(p->m_lastPosition));
        s.rootData=std::move(expected);
    }
    ~Root(){
        if(!captured)return;
        auto& s=state();
        auto p=playerAt(static_cast<int>(integer(s.rootData,"player")));
        if(!s.playback){s.rootData["after"]=motion(p);s.rootData["lastPositionAfter"]=point(p->m_lastPosition);s.rootData["ops"]=Json::array();for(auto& op:s.operations)s.rootData["ops"].push(std::move(op));}
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
    s.leased=std::move(failed);s.frame=s.root=false;s.layer=nullptr;s.playback=nullptr;s.nativePlayer=s.shipPlayer=nullptr;s.depth=0;
}
bool cbfReplayHooksHeld(){return !state().leased.empty();}
void beginCbfNativeFrame(PlayLayer* layer,Playback* replay){
    auto& s=state();s.layer=layer;s.playback=replay;s.frame=true;s.root=false;s.depth=0;s.nativePlayer=s.shipPlayer=nullptr;s.rootIndex=s.clockIndex=0;s.totalUpdates={};s.fault.clear();s.operations.clear();
    if(replay){s.data=replay->current()->cbf;if(!cbfFrameValid(s.data))fail("CBF_FRAME_TAPE_INVALID");}
    else{s.data=Json::object();s.data["kind"]="cbf";s.data["schema"]=CbfBoundarySchema;s.data["roots"]=Json::array();s.data["clocks"]=Json::array();}
}
Json finishCbfNativeFrame(){auto& s=state();if(s.playback){if(s.rootIndex!=s.data["roots"].size()||s.clockIndex!=s.data["clocks"].size())fail("CBF_UNCONSUMED_FRAME_BOUNDARIES");}s.frame=s.root=false;return s.playback?s.data:cbfCompactFrame(s.data);}
std::string cbfNativeFrameError(){return state().fault;}
Json cbfNativeUpdateCounts(){auto v=Json::array();for(auto n:state().totalUpdates)v.push(static_cast<std::int64_t>(n));return v;}
Json captureCbfInitial(PlayLayer* layer){
    Json value;value["schema"]=CbfBoundarySchema;value["players"]=Json::array();
    if(!layer)return value;
    for(auto* p:{layer->m_player1,layer->m_player2}){if(!p){value["players"].push(nullptr);continue;}Json item;item["lastPosition"]=point(p->m_lastPosition);auto logs=Json::array();for(auto* log:{p->m_collisionLogTop,p->m_collisionLogBottom,p->m_collisionLogLeft,p->m_collisionLogRight})logs.push(log?static_cast<int>(log->count()):-1);item["collisionLogs"]=logs;value["players"].push(item);}return value;
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
    int checkCollisions(PlayerObject* p,float dt,bool ignoreDamage){corum::ranked::replay::Json fields;fields["ignoreDamage"]=ignoreDamage;corum::ranked::replay::Operation operation("COLLISION",p,dt,std::move(fields));return GJBaseGameLayer::checkCollisions(p,dt,ignoreDamage);}
    void handleButton(bool down,int button,bool player1){corum::ranked::replay::Json fields;fields["down"]=down;fields["button"]=button;fields["timestamp"]=m_timestamp;corum::ranked::replay::Operation operation("INPUT",player1?m_player1:m_player2,0,std::move(fields));GJBaseGameLayer::handleButton(down,button,player1);}
#if !defined(GEODE_IS_MACOS)
    double getModifiedDelta(float dt){using namespace corum::ranked::replay;auto result=GJBaseGameLayer::getModifiedDelta(dt);auto& s=state();if(!active())return result;
        Json clock;clock["in"]=static_cast<double>(dt);clock["out"]=result;clock["queuedAfter"]=encodeReplayButtons(m_queuedButtons);
        if(s.playback){result=static_cast<float>(result);clock["out"]=result;auto clocks=s.data["clocks"].asArray().unwrap();if(s.clockIndex>=clocks.size()){fail("CBF_EXTRA_CLOCK");return result;}auto expected=clocks[s.clockIndex++];if(expected["in"]!=clock["in"]||expected["out"]!=clock["out"]){fail("CBF_NATIVE_CLOCK",expected,clock);return result;}
            // Explicit source CBF queue handoff, not inferred consumption.
            auto queued=expected["queuedAfter"].asArray().unwrap();m_queuedButtons.clear();for(auto const& input:queued)m_queuedButtons.push_back(decodeReplayButton<PlayerButtonCommand>(input));
        }else s.data["clocks"].push(clock);return result;
    }
#endif
};
