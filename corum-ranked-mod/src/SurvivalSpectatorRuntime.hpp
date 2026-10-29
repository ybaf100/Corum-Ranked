#pragma once
#include "replay/SurvivalReplayPlayback.hpp"
#include <future>
#include <memory>
#include "SpectatorLiveRuntime.hpp"
#include "domain/LevelPreparationPolicy.hpp"
namespace corum::ranked {
struct SurvivalSpectatorTarget {std::string id,name,status;int account=0,rank=0;double best=0,now=0;bool selectable=false,bot=false;};
// Owns Survival selection/authority/lifecycle. The visual HTTP transport has no scoring port.
class SurvivalSpectatorRuntime final {
public:
    static SurvivalSpectatorRuntime& get();
    void tick();
    void openTargets();
    void selectionVisible(bool visible);
    void refreshTargets();
    void select(std::string id);
    void leave(bool chooseAgain=false);
    void freePlay();
    bool active()const{return m_authorized;}
    bool watchingThroughEnd()const{return m_authorized&&m_inScene&&!m_exitRequested;}
    bool sceneLevel(int level)const;
    bool canLaunch()const;
    bool arm(GJGameLevel* level);
    void sceneExited();
    bool exitRequested()const{return m_exitRequested;}
    replay::Playback* world(){return m_world.get();}
    bool publishing()const;
    bool capture(SpectatorGhostPose const& pose);
    void terminal(SpectatorGhostPose const& pose);
    std::vector<SurvivalSpectatorTarget> const& targets()const{return m_targets;}
    std::string const& error()const{return m_error;}
    std::string const& targetName()const{return m_targetName;}
    std::string hudText()const;
private:
    bool publisherAllowed()const;
    bool currentScope()const;
    std::string path()const;
    SurvivalSpectatorTransportContext transport(bool viewer)const;
    std::vector<SurvivalSpectatorTarget> m_targets;
    std::string m_error,m_target,m_targetName,m_session,m_match,m_pendingTarget,m_pendingTerminalAttempt,m_targetsContext,m_lifecycleContext;
    LevelPreparationContext m_context;
    std::future<std::unique_ptr<replay::Playback>> m_worldLoading;
    std::unique_ptr<replay::Playback> m_world;
    std::optional<SpectatorGhostPose> m_terminal;
    std::uint64_t m_generation=0;
    bool m_authorized=false,m_inScene=false,m_exitRequested=false,m_openAfterExit=false,m_selectionVisible=false,m_busy=false,m_ticking=false,m_startFreePlay=false;
    std::chrono::steady_clock::time_point m_nextTargets{},m_terminalUntil{};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request,m_leave;
};
void showSurvivalSpectatorTargets();
}
