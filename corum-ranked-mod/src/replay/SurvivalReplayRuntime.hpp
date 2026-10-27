#pragma once
#include "SurvivalReplayPlayback.hpp"
#include "SurvivalReplayRecorder.hpp"
#include "../domain/LevelPreparationPolicy.hpp"
namespace corum::ranked::replay {
class Runtime final {
public:
    static Runtime& get();
    void request(Json identity);
    void pump();
    void cancel();
    bool preparing()const{return m_requested&&!m_scene&&m_error.empty();}
    bool scene()const{return m_scene;}
    bool owns(int level)const{return m_scene&&integer(m_identity,"levelId")==level;}
    bool canLaunch()const{return preparing()&&m_playback!=nullptr;}
    bool arm(GJGameLevel*);
    LevelPreparationContext context()const;
    Playback* playback(){return m_playback.get();}
    std::string const& error()const{return m_error;}
    bool validate(GJGameLevel*);
    void exited();
private:
    Json m_identity;
    std::optional<Json> m_pendingIdentity;
    bool m_requested=false,m_scene=false;
    std::string m_error;
    std::future<std::unique_ptr<Playback>> m_loading;
    std::unique_ptr<Playback> m_playback;
};
void showReplay(Json identity);
}
