#pragma once
#include "SurvivalSelectedMapPanel.hpp"
#include "../domain/SurvivalMapSelection.hpp"
#include <array>
#include <deque>
#include <memory>
namespace corum::ranked::ui {
class SurvivalMapSelectionLayer final : public cocos2d::CCLayerColor {
    struct Visuals;
    std::unique_ptr<Visuals> m_visuals;
    SurvivalMapSelectionTimeline m_timeline;
    SurvivalSelectedMapPanel* m_panel=nullptr;
    cocos2d::CCLabelBMFont *m_header=nullptr,*m_subtitle=nullptr,*m_clock=nullptr,*m_hint=nullptr,*m_finalId=nullptr,*m_pendingLabel=nullptr;
    std::function<void(int)> m_action;
    std::uint64_t m_sceneGeneration=0;
    std::uint64_t m_appEpoch=0;
    std::optional<SurvivalSelectionContext> m_providerContext;
    bool m_thumbnail=false,m_fallback=false,m_active=false,m_needsResume=true;
    std::deque<std::size_t> m_sounds;
    std::int64_t m_lastSound=0;
    std::int64_t m_lastFrameMillis=0;
    void resetVisuals();
    void numberFrame(SurvivalRevealFrame const&,SurvivalSelectionLayout const&);
    void thumbnailFrame(SurvivalRevealFrame const&,SurvivalSelectionLayout const&);
    void playLocks(SurvivalRevealFrame const&);
    bool init(std::function<void(int)>);
public:
    SurvivalMapSelectionLayer();
    ~SurvivalMapSelectionLayer() override;
    static SurvivalMapSelectionLayer* create(std::function<void(int)> action);
    void prepareServerMap(SurvivalMatchView const&);
    void present(SurvivalMatchView const&,RoundPreparationView const&,SurvivalNativeOptions,bool supportsLdm,bool actionsEnabled,bool canEnter,
        std::optional<std::int64_t> countdown,std::optional<std::int64_t> remaining);
    void hideSelection();
    bool settled()const;
    void onEnter() override;
    void onExit() override;
};
}
