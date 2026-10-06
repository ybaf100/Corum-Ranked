#if defined(CORUM_RANKED_DEBUG_SPECTATE) && !defined(CORUM_CRASH_DIAG_NO_PROBES_HOOKS)
#include "../domain/SpectatorNativeProbe.hpp"
#include "../diagnostic/CrashHookDiagnostics.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>

using namespace geode::prelude;
using corum::ranked::SpectatorNativePath;
using corum::ranked::noteSpectatorNativePath;

// Pass-through probes: deliberately never add a second collision pass. Native
// collision is broader than trigger observation (portals/rings/pickups/damage).
class $modify(CorumSpectatorNativeInteractionProbe, GJBaseGameLayer) {
    static void onModify(auto& self) {
#if defined(CORUM_CRASH_DIAG)
        CORUM_CRASH_MODIFY(self, "CorumSpectatorNativeInteractionProbe");
#endif
    }
    int checkCollisions(PlayerObject* player, float dt, bool ignoreDamage) {
        noteSpectatorNativePath(this, player, SpectatorNativePath::CheckCollisions);
        return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
    }
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int count, float dt) {
        noteSpectatorNativePath(this, player, SpectatorNativePath::CollisionObjects);
        GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
    }
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_IOS)
    void playerTouchedObject(PlayerObject* player, GameObject* object) {
        noteSpectatorNativePath(this, player, SpectatorNativePath::TouchedObject);
        GJBaseGameLayer::playerTouchedObject(player, object);
    }
#endif
    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        noteSpectatorNativePath(this, player, SpectatorNativePath::TouchedTrigger);
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }
    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        noteSpectatorNativePath(this, player, SpectatorNativePath::ActivationQuery);
        auto const allowed = GJBaseGameLayer::canBeActivatedByPlayer(player, object);
        if (allowed) noteSpectatorNativePath(this, player, SpectatorNativePath::ActivationAllowed);
        return allowed;
    }
    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        noteSpectatorNativePath(this, player, SpectatorNativePath::Teleport);
        GJBaseGameLayer::teleportPlayer(object, player);
    }
    void checkSpawnObjects() {
        noteSpectatorNativePath(this, nullptr, SpectatorNativePath::Spawn);
        GJBaseGameLayer::checkSpawnObjects();
    }
    void processCommands(float dt, bool halfTick, bool lastTick) {
        noteSpectatorNativePath(this, nullptr, SpectatorNativePath::Commands);
        GJBaseGameLayer::processCommands(dt, halfTick, lastTick);
    }
    void processPlayerFollowActions(float dt) {
        noteSpectatorNativePath(this, nullptr, SpectatorNativePath::Follow);
        GJBaseGameLayer::processPlayerFollowActions(dt);
    }
    void processDynamicObjectActions(int type, float dt) {
        noteSpectatorNativePath(this, nullptr, SpectatorNativePath::DynamicMove);
        GJBaseGameLayer::processDynamicObjectActions(type, dt);
    }
};

class $modify(CorumSpectatorNativeTriggerProbe, EffectGameObject) {
    static void onModify(auto& self) {
#if defined(CORUM_CRASH_DIAG)
        CORUM_CRASH_MODIFY(self, "CorumSpectatorNativeTriggerProbe");
#endif
    }
    void triggerObject(GJBaseGameLayer* layer, int uniqueId, gd::vector<int> const* remapKeys) override {
        corum::ranked::noteSpectatorNativeTrigger(layer, m_uniqueID);
        EffectGameObject::triggerObject(layer, uniqueId, remapKeys);
    }
};
#endif
