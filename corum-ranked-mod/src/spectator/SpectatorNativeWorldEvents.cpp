#include "SpectatorNativeWorldEvents.hpp"
#include "../SurvivalRuntime.hpp"
#include "../SurvivalSpectatorRuntime.hpp"
#include "../debug/DebugSpectateRuntime.hpp"
#include "../replay/SurvivalReplayRuntime.hpp"
#include "SpectatorWorldEventsCodec.hpp"
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/RandTriggerGameObject.hpp>
#include <deque>
#include <set>
#include <unordered_map>
using namespace geode::prelude;
using namespace corum::ranked;
namespace {
using namespace corum::ranked;
struct ObjectIdentity {
    int index, id;
    float x, y;
};
struct RandomCall {
    SpectatorRandomEvent event;
    GJBaseGameLayer *layer = nullptr;
    bool replay = false;
    std::size_t applied = 0;
    RandomCall *previous = nullptr;
};
thread_local RandomCall *currentRandom = nullptr;
struct RandomScope {
    RandomCall &call;
    RandomScope(RandomCall &c) : call(c) {
        call.previous = currentRandom;
        currentRandom = &call;
    }
    ~RandomScope() { currentRandom = call.previous; }
};
replay::Playback *replayOwner(PlayLayer *layer) {
    if (!layer || layer != PlayLayer::get() || !layer->m_level)
        return nullptr;
    auto id = static_cast<int>(layer->m_level->m_levelID);
    if (replay::Runtime::get().owns(id))
        return replay::Runtime::get().playback();
    if (SurvivalSpectatorRuntime::get().sceneLevel(id))
        return SurvivalSpectatorRuntime::get().world();
    return nullptr;
}
} // namespace
class $modify(CorumNativeWorldEventLayer, PlayLayer) {
  public:
    struct Fields {
        std::unordered_map<GameObject *, ObjectIdentity> objects;
        std::vector<GameObject *> byIndex;
        std::vector<SpectatorRandomEvent> captured;
        std::deque<SpectatorRandomEvent> expected;
        SpectatorRandomEvents snapshot;
        std::string scope;
        std::uint64_t ordinal = 0, seen = 0;
        std::optional<SpectatorRemoteContext> context;
        bool remote = false, applyingSource = false;
        std::set<int> missingActivations;
        unsigned misses = 0;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        bool trace = false;
        unsigned traceSamples = 0, triggerActivations = 0;
        double traceAt = -1;
        std::vector<int> traceGroups;
#endif
    };
    bool init(GJGameLevel *level, bool replay, bool noObjects) {
        if (!PlayLayer::init(level, replay, noObjects))
            return false;
        bool needed = replayOwner(this) != nullptr;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        m_fields->trace = Mod::get()->getSavedValue<bool>("debug-spectator-world-trace", false);
        needed = needed ||
                 debug::DebugSpectateRuntime::get().preparedLevel(static_cast<int>(level->m_levelID)) ||
                 debug::DebugSpectateRuntime::get().recordLevel(static_cast<int>(level->m_levelID));
#endif
        if (needed)
            buildObjects();
        return true;
    }
    void buildObjects() {
        m_fields->objects.clear();
        m_fields->byIndex.clear();
        if (!m_objects)
            return;
        for (unsigned i = 0; i < m_objects->count(); ++i) {
            auto *object = typeinfo_cast<GameObject *>(m_objects->objectAtIndex(i));
            m_fields->byIndex.push_back(object);
            if (object)
                m_fields->objects.emplace(object,
                                          ObjectIdentity{static_cast<int>(i), object->m_objectID,
                                                         object->getPositionX(), object->getPositionY()});
        }
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->scope.clear();
        m_fields->captured.clear();
        m_fields->snapshot.reset();
        m_fields->expected.clear();
        m_fields->context.reset();
        m_fields->ordinal = m_fields->seen = 0;
        m_fields->remote = false;m_fields->applyingSource=false;m_fields->missingActivations.clear();
    }
};
namespace {
CorumNativeWorldEventLayer *owner(GJBaseGameLayer *layer) {
    auto *play = PlayLayer::get();
    return play && static_cast<GJBaseGameLayer *>(play) == layer
               ? static_cast<CorumNativeWorldEventLayer *>(play)
               : nullptr;
}
void emitSpawn(GJBaseGameLayer *layer, SpectatorRandomSpawn const &s, int trigger, int control) {
    gd::vector<int> remap;
    for (auto n : s.remap)
        remap.push_back(n);
    layer->spawnGroup(s.group, s.ordered, s.delay, remap, s.triggerId.value_or(trigger), s.controlId.value_or(control));
}
} // namespace
class $modify(CorumNativeRandomBranch, RandTriggerGameObject) {
  public:
    void triggerObject(GJBaseGameLayer *layer, int uniqueId, gd::vector<int> const *remapKeys) override {
        auto *play = owner(layer);
        if (!play) {
            RandTriggerGameObject::triggerObject(layer, uniqueId, remapKeys);
            return;
        }
        auto *playback = replayOwner(play);
        auto &survival = SurvivalRuntime::get();
        bool official = play->m_level && survival.gameplayLevel(static_cast<int>(play->m_level->m_levelID)) &&
                        !survival.freePlaying() && !play->m_isTestMode && !play->m_isPracticeMode;
        bool recording =
            official && replay::Recorder::get().active() && replay::Recorder::get().inNativeFrame();
        std::string scope = recording ? survival.localAttemptId() : "";
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        bool debugRecord =
            debug::DebugSpectateRuntime::get().recordLevel(static_cast<int>(play->m_level->m_levelID));
        if (debugRecord)
            scope = debug::DebugSpectateRuntime::get().contextId();
#else
        bool debugRecord = false;
#endif
        bool remote = play->m_fields->remote;
        if (!recording && !debugRecord && !playback && !remote) {
            RandTriggerGameObject::triggerObject(layer, uniqueId, remapKeys);
            return;
        }
        // The prepared-level predicate may be false during nested init/reset.
        // Build identities at the first real capture activation as well.
        if((recording||debugRecord) && scope!=play->m_fields->scope){
            play->buildObjects();play->m_fields->scope=scope;play->m_fields->ordinal=0;
            play->m_fields->captured.clear();play->m_fields->snapshot.reset();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            log::info("[SpectatorRandom] phase=CAPTURE_READY attemptId={} objects={}",scope,play->m_fields->objects.size());
#endif
        }
        auto found = play->m_fields->objects.find(this);
        if (found == play->m_fields->objects.end()) {
            if (playback || remote) {
                SpectatorLiveRuntime::get().noteWorldPartial("RANDOM_OBJECT_IDENTITY_MISSING");
                return;
            }
            log::warn("[SpectatorRandom] phase=REJECTED reason=SOURCE_OBJECT_IDENTITY_MISSING scope={} objectId={}",scope,m_objectID);
            RandTriggerGameObject::triggerObject(layer, uniqueId, remapKeys);
            return;
        }
        auto identity = found->second;
        RandomCall call;
        call.layer = layer;
        if (playback) {
            auto event = playback->randomEvent(identity.index, identity.id, identity.x, identity.y);
            if (!event)
                return;
            call.event = *event;
            call.replay = true;
        } else if (remote) {
            // Native collision/spawn may run ahead of the presented source
            // clock. Only the due-event drain may dispatch authoritative outputs.
            if(!play->m_fields->applyingSource){
                play->m_fields->missingActivations.insert(identity.index);
                SpectatorLiveRuntime::get().noteWorldPartial("RANDOM_SOURCE_ACTIVATION_MISSING");return;
            }
            if(play->m_fields->expected.empty())return;
            auto const& expected=play->m_fields->expected.front();
            if(expected.objectIndex!=identity.index || expected.objectId!=identity.id ||
                std::hypot(expected.originX-identity.x,expected.originY-identity.y)>.1){
                SpectatorLiveRuntime::get().noteWorldPartial("RANDOM_MAP_IDENTITY_MISMATCH");return;
            }
            call.event=expected;play->m_fields->expected.pop_front();
            call.replay=true;
        } else {
            if (scope != play->m_fields->scope) {
                play->m_fields->scope = scope;
                play->m_fields->ordinal = 0;
                play->m_fields->captured.clear();
                play->m_fields->snapshot.reset();
            }
            call.event.ordinal = ++play->m_fields->ordinal;
            call.event.objectIndex = identity.index;
            call.event.objectId = identity.id;
            call.event.originX = identity.x;
            call.event.originY = identity.y;
            call.event.tick = std::max(0, play->m_currentStep);
            call.event.elapsedMs = std::max(0., play->m_gameState.m_levelTime * 1000.);
        }
        if(call.replay){
            // Reapply actual recorded outputs once. Calling the Random trigger
            // here would draw a fresh branch and mutate unrelated native RNG.
            for(auto const& output:call.event.outputs)emitSpawn(layer,output,uniqueId,m_controlID);
            if(remote){
                play->m_fields->missingActivations.erase(identity.index);
                if(play->m_fields->missingActivations.empty())SpectatorLiveRuntime::get().worldCauseRecovered("RANDOM_SOURCE_ACTIVATION_MISSING");
            }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            log::info("[SpectatorRandom] phase=APPLIED triggerId={} activation={} nativeTick={} outputs={}",identity.index,call.event.ordinal,call.event.tick,call.event.outputs.size());
#endif
            return;
        }
        {RandomScope active(call);RandTriggerGameObject::triggerObject(layer,uniqueId,remapKeys);}
        if (!validSpectatorRandomEvent(call.event)) {
            if (recording) {
                replay::Json bad;
                bad["kind"] = "UNSUPPORTED_RANDOM_OUTPUT";
                replay::Recorder::get().add(bad);
            }
            return;
        }
        if (recording) {
            auto record = replay::Json();
            record["kind"] = "random";
            record["event"] = encodeSpectatorRandomEvent(call.event);
            replay::Recorder::get().add(record);
        }
        if (debugRecord && play->m_fields->captured.size() >= 256)
            SpectatorLiveRuntime::get().noteWorldPartial("RANDOM_CAPTURE_CAPACITY");
        if (debugRecord && play->m_fields->captured.size() < 256) {
            play->m_fields->captured.push_back(call.event);
            play->m_fields->snapshot.reset();
        }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (call.event.ordinal <= 32)
            log::info("[SpectatorRandom] phase=RECORD attemptId={} triggerId={} objectId={} activation={} "
                      "nativeTick={} elapsedMs={} outputs={}",
                      scope, identity.index, identity.id, call.event.ordinal, call.event.tick,
                      call.event.elapsedMs,
                      encodeSpectatorRandomEvent(call.event)["outputs"].dump(matjson::NO_INDENTATION));
#endif
    }
};
class $modify(CorumNativeRandomOutput, GJBaseGameLayer) {
  public:
    void spawnGroup(int group, bool ordered, double delay, gd::vector<int> const &remap, int triggerId,
                    int controlId) override {
        if (!currentRandom || currentRandom->layer != this) {
            GJBaseGameLayer::spawnGroup(group, ordered, delay, remap, triggerId, controlId);
            return;
        }
        auto &call = *currentRandom;
        if (call.replay) {
            if (call.applied >= call.event.outputs.size())
                return;
            auto const &source = call.event.outputs[call.applied++];
            gd::vector<int> keys;
            for (auto n : source.remap)
                keys.push_back(n);
            GJBaseGameLayer::spawnGroup(source.group, source.ordered, source.delay, keys, triggerId,
                                        controlId);
            return;
        }
        SpectatorRandomSpawn out;
        out.group = group;out.triggerId=triggerId;out.controlId=controlId;
        out.ordered = ordered;
        out.delay = delay;
        for (auto n : remap)
            out.remap.push_back(n);
        call.event.outputs.push_back(std::move(out));
        GJBaseGameLayer::spawnGroup(group, ordered, delay, remap, triggerId, controlId);
    }
};
namespace corum::ranked {
void beginNativeWorldEventCapture(PlayLayer *layer) {
    auto *play = owner(layer);
    if (play && play->m_fields->objects.empty())
        play->buildObjects();
}
SpectatorRandomEvents captureNativeRandomEvents(PlayLayer *layer) {
    auto *play = owner(layer);
    if (!play)
        return {};
    if (!play->m_fields->snapshot) {
        auto events = play->m_fields->captured;
        if(events.size()>16)events.resize(16); // packet limit, not capture limit
        std::sort(events.begin(), events.end(),
                  [](auto const &a, auto const &b) { return a.ordinal < b.ordinal; });
        play->m_fields->snapshot =
            std::make_shared<std::vector<SpectatorRandomEvent> const>(std::move(events));
    }
    return play->m_fields->snapshot;
}
void commitNativeRandomEvents(PlayLayer *layer, SpectatorRandomEvents const &events) {
    auto *play = owner(layer);
    if (!play || !events || events->empty())
        return;
    auto last = events->back().ordinal;
    std::erase_if(play->m_fields->captured, [&](auto const &e) { return e.ordinal <= last; });
    play->m_fields->snapshot.reset();
}
void prepareNativeWorldEvents(PlayLayer *layer, SpectatorRemoteFrame const &frame) {
    auto *play = owner(layer);
    if (!play || !frame.pose)
        return;
    if (play->m_fields->objects.empty())
        play->buildObjects();
    if (play->m_fields->context != frame.context) {
        play->m_fields->context = frame.context;
        play->m_fields->expected.clear();
        play->m_fields->seen = 0;
        play->m_fields->misses = 0;play->m_fields->missingActivations.clear();
    }
    play->m_fields->remote = true;
    if (frame.pose->randomEvents)
        for (auto const &e : *frame.pose->randomEvents)
            if (e.ordinal > play->m_fields->seen) {
                if (play->m_fields->expected.size() >= 256) {
                    SpectatorLiveRuntime::get().noteWorldPartial("RANDOM_EVENT_CAPACITY");
                    break;
                }
                play->m_fields->expected.push_back(e);
                play->m_fields->seen = e.ordinal;
            }
}
void applyDueNativeRandomEvents(PlayLayer *layer) {
    auto *play = owner(layer);
    if (!play || !play->m_fields->remote)
        return;
    for (unsigned i = 0; i < 16 && !play->m_fields->expected.empty(); ++i) {
        auto index = play->m_fields->expected.front().objectIndex;
        if (index < 0 || static_cast<std::size_t>(index) >= play->m_fields->byIndex.size())
            break;
        auto *object = typeinfo_cast<RandTriggerGameObject *>(play->m_fields->byIndex[index]);
        if (!object)
            break;
        auto count = play->m_fields->expected.size();
        play->m_fields->applyingSource=true;
        object->triggerObject(play, object->m_uniqueID, nullptr);
        play->m_fields->applyingSource=false;
        if (play->m_fields->expected.size() >= count)
            break;
    }
}
void noteNativeFollowTrigger(GJBaseGameLayer *layer, EffectGameObject *trigger) {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto *play = owner(layer);
    if (!play || !play->m_fields->trace || !trigger || trigger->m_targetGroupID <= 0 ||
        play->m_fields->triggerActivations >= 64)
        return;
    auto &debugRuntime = debug::DebugSpectateRuntime::get();
    auto id = static_cast<int>(play->m_level->m_levelID);
    if (!debugRuntime.recordLevel(id) && !debugRuntime.playbackLevel(id) && !replayOwner(play))
        return;
    auto group = trigger->m_targetGroupID;
    auto &groups = play->m_fields->traceGroups;
    if (groups.size() < 8 && std::find(groups.begin(), groups.end(), group) == groups.end())
        groups.push_back(group);
    ++play->m_fields->triggerActivations;
    log::info(
        "[SpectatorFollow] phase=ACTIVATE role={} attemptId={} nativeTick={} elapsedMs={} triggerId={} "
        "objectId={} targetGroupId={} lockX={} lockY={} followX={} followY={} triggerActivationCount={}",
        debugRuntime.recordLevel(id) ? "RECORD" : "TEST", debugRuntime.contextId(), play->m_currentStep,
        play->m_gameState.m_levelTime * 1000., trigger->m_uniqueID, trigger->m_objectID, group,
        trigger->m_lockToPlayerX, trigger->m_lockToPlayerY, trigger->m_followXMod, trigger->m_followYMod,
        play->m_fields->triggerActivations);
#else
    (void)layer;
    (void)trigger;
#endif
}
void sampleNativeFollowWorld(GJBaseGameLayer *layer, float dt) {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto *play = owner(layer);
    if (!play || !play->m_fields->trace || !play->m_player1 || play->m_fields->traceSamples >= 120)
        return;
    auto time = play->m_gameState.m_levelTime * 1000.;
    if (time < play->m_fields->traceAt)
        play->m_fields->traceAt = -1;
    if (time < play->m_fields->traceAt + 250)
        return;
    play->m_fields->traceAt = time;
    ++play->m_fields->traceSamples;
    auto *player = play->m_player1;
    auto index = (player->m_followRelated % 200 + 200) % 200;
    auto sample = player->m_playerFollowFloats.size() == 200 ? player->m_playerFollowFloats[index] : 0.f;
    auto &debugRuntime = debug::DebugSpectateRuntime::get();
    auto role = debugRuntime.recordLevel(static_cast<int>(play->m_level->m_levelID)) ? "RECORD" : "TEST";
    for (auto group : play->m_fields->traceGroups)
        if (auto *objects = layer->getGroup(group))
            for (unsigned i = 0; i < std::min(3u, objects->count()); ++i)
                if (auto *object = typeinfo_cast<GameObject *>(objects->objectAtIndex(i)))
                    log::info(
                        "[SpectatorFollow] phase=OBJECT role={} attemptId={} nativeTick={} elapsedMs={} "
                        "playerX={} playerY={} dummyX={} dummyY={} targetGroupId={} objectId={} "
                        "nativeObjectX={} nativeObjectY={} followHistoryIndex={} followSample={} "
                        "triggerActivationCount={} worldDeltaMs={} nativeStarted={} nativeResetDelay={}",
                        role, debugRuntime.contextId(), play->m_currentStep, time, player->getPositionX(),
                        player->getPositionY(), player->m_positionX, player->m_positionY, group,
                        object->m_objectID, object->getPositionX(), object->getPositionY(),
                        player->m_followRelated, sample, play->m_fields->triggerActivations, dt * 1000,
                        play->m_started, play->m_inResetDelay);
#else
    (void)layer;
    (void)dt;
#endif
}
} // namespace corum::ranked
