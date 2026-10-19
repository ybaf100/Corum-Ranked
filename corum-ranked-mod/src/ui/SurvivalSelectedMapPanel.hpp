#pragma once
#include "../SurvivalRuntime.hpp"
#include "../RankedRoundPreparation.hpp"
#include "../domain/SurvivalSelectionLayout.hpp"
#include "../domain/SurvivalOptionsPolicy.hpp"
#include "../domain/MapMetadata.hpp"
#include <functional>
namespace corum::ranked::ui {
// Both reveals feed exactly this panel. Image placement belongs to the reveal
// layer so the landing card never disappears during the information fade.
class SurvivalSelectedMapPanel final : public cocos2d::CCLayerColor {
    SurvivalSelectionLayout m_layout;
    bool m_built=false,m_thumbnail=false,m_ldmSupported=false,m_readyEnabled=false,m_retry=false,m_start=false;
    SurvivalNativeOptions m_options;
    cocos2d::CCLabelBMFont *m_title=nullptr,*m_creator=nullptr,*m_metadata=nullptr,*m_readiness=nullptr,*m_countdown=nullptr,*m_stars=nullptr,*m_rating=nullptr;
    cocos2d::CCSprite* m_star=nullptr;
    cocos2d::CCNode* m_difficulty=nullptr;
    CCMenuItemSpriteExtra *m_ldm=nullptr,*m_shake=nullptr,*m_jukebox=nullptr,*m_ready=nullptr;
    cocos2d::CCMenu* m_menu=nullptr;
    std::uint64_t m_metadataRevision=0;
    int m_levelId=0;
    MapMetadata m_mapMetadata;
    std::string m_creatorText,m_statusText;
    std::function<void(int)> m_action;
    void build(cocos2d::CCSize,bool,bool);
    void onAction(cocos2d::CCObject*);
    cocos2d::CCNode* readyFace(bool,bool retry=false,bool start=false);
public:
    static SurvivalSelectedMapPanel* create(std::function<void(int)> action);
    void present(cocos2d::CCSize,bool thumbnail,SurvivalMatchView const&,RoundPreparationView const&,
        SurvivalNativeOptions,bool supportsLdm,bool actionsEnabled,bool canEnter,double appearance);
    SurvivalSelectionLayout const& layout()const{return m_layout;}
    void suspend();
};
}
