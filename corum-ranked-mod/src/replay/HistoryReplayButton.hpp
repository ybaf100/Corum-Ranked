#pragma once
#include "SurvivalReplayRuntime.hpp"
#include "SurvivalReplayQuality.hpp"
#include "SurvivalReplayEnvironment.hpp"
#include "../ui/RankedTranslations.hpp"
#include "../ui/RankedVisuals.hpp"
namespace corum::ranked::replay {
class HistoryReplayButton final:public cocos2d::CCNode {
    Json m_identity;std::future<Json> m_check;
    CCMenuItemSpriteExtra* m_button=nullptr;cocos2d::CCLabelBMFont* m_caption=nullptr;
    void play(cocos2d::CCObject*){showReplay(m_identity);}
    bool init(Json identity,float width){
        if(!CCNode::init())return false;m_identity=std::move(identity);Recorder::get().retryPending();
        auto* menu=cocos2d::CCMenu::create();menu->setPosition({0,0});addChild(menu);
        auto* face=ui::surface({width,25},{},ui::Cyan,ui::Surface,240,true);
        m_caption=ui::menuLabel("REPLAY...",.22f,{width/2,12.5f},ui::Text);face->addChild(m_caption,5);
        m_button=CCMenuItemSpriteExtra::create(face,this,menu_selector(HistoryReplayButton::play));menu->addChild(m_button);m_button->setEnabled(false);
        try{m_check=Recorder::get().storage().inspectState(m_identity);scheduleUpdate();}catch(...){m_caption->setString(i18n::text("replay-damaged","Replay file damaged").c_str());ui::fitFullMenuLabel(m_caption,width-10,20,.22f);}return true;
    }
    static std::string label(ReplayQualityView const& v){
        switch(v.quality){
          case ReplayQuality::CompleteVerified:return i18n::text("replay-view","VIEW REPLAY");
          case ReplayQuality::PartialRecovered:return i18n::text("replay-view-partial","VIEW REPLAY · PARTIAL RECOVERED");
          case ReplayQuality::OfficialPrefix:return i18n::text("replay-view-prefix","VIEW REPLAY · OFFICIAL PREFIX");
          case ReplayQuality::WaitingReceipt:return i18n::text("replay-waiting-receipt","Replay waiting for official receipt");
          case ReplayQuality::Unavailable:return i18n::text("replay-unavailable","Replay unavailable (missing evidence)");
          case ReplayQuality::Damaged:return i18n::text("replay-damaged","Replay file damaged");
          case ReplayQuality::Incompatible:
            switch(v.compatibility){
              case ReplayCompatibility::UnsupportedSetting:return i18n::text("replay-incompatible-setting","Replay incompatible: unsupported CBF setting");
              case ReplayCompatibility::VersionMismatch:return i18n::text("replay-incompatible-version","Replay incompatible: version mismatch");
              case ReplayCompatibility::HookUnavailable:return i18n::text("replay-incompatible-hook","Replay incompatible: hook unavailable");
              default:return i18n::text("replay-incompatible-evidence","Replay incompatible: missing native evidence");
            }
          case ReplayQuality::NotOnDevice:break;
        }
        return i18n::text("replay-device-missing","No replay on this device");
    }
    void update(float)override{
        if(!m_check.valid()||m_check.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        ReplayQualityView view;
        try{auto state=m_check.get();std::string environment;
          if(state["best"].isObject())environment=replayPhysicsEnvironmentMismatch(state["best"]["physicsEnvironment"],captureReplayPhysicsEnvironment());
          view=classifyReplayQuality(state,m_identity,environment);
        }catch(...){view.quality=ReplayQuality::Damaged;}
        // Only a stored, identity-matching, environment-compatible replay is enterable;
        // partial/prefix labels never claim a full 100% replay.
        m_button->setEnabled(view.playable);
        if(!view.reason.empty())geode::log::info("[SurvivalReplay] stage=HISTORY_QUALITY quality={} reason={} compatibility={} attemptId={}",replayQualityName(view.quality),view.reason,replayCompatibilityName(view.compatibility),str(m_identity,"attemptId"));
        auto text=label(view);
        m_caption->setString(text.c_str());ui::fitFullMenuLabel(m_caption,m_button->getContentSize().width-10,20,.22f);unscheduleUpdate();
    }
public:static HistoryReplayButton* create(Json identity,float width){auto*p=new HistoryReplayButton;if(p->init(std::move(identity),width)){p->autorelease();return p;}delete p;return nullptr;}
};
}
