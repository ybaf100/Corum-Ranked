#pragma once
#include "SurvivalReplayRuntime.hpp"
#include "../ui/RankedTranslations.hpp"
#include "../ui/RankedVisuals.hpp"
namespace corum::ranked::replay {
class HistoryReplayButton final:public cocos2d::CCNode {
    Json m_identity;std::future<std::optional<Json>> m_check;
    CCMenuItemSpriteExtra* m_button=nullptr;CCLabelBMFont* m_caption=nullptr;
    void play(cocos2d::CCObject*){showReplay(m_identity);}
    bool init(Json identity,float width){
        if(!CCNode::init())return false;m_identity=std::move(identity);
        auto* menu=cocos2d::CCMenu::create();menu->setPosition({0,0});addChild(menu);
        auto* face=ui::surface({width,25},{},ui::Cyan,ui::Surface,240,true);
        m_caption=ui::menuLabel("REPLAY...",.22f,{width/2,12.5f},ui::Text);face->addChild(m_caption,5);
        m_button=CCMenuItemSpriteExtra::create(face,this,menu_selector(HistoryReplayButton::play));menu->addChild(m_button);m_button->setEnabled(false);
        try{m_check=Recorder::get().storage().inspect(m_identity);scheduleUpdate();}catch(...){m_caption->setString(i18n::text("replay-damaged","Replay file damaged").c_str());ui::fitFullMenuLabel(m_caption,width-10,20,.22f);}return true;
    }
    void update(float)override{
        if(!m_check.valid()||m_check.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        std::string text=i18n::text("replay-device-missing","No replay on this device");
        try{auto found=m_check.get();bool valid=found.has_value();
          if(found)for(auto key:{"attemptId","mapGeometrySha256","mapVersion","levelId"})if((*found)[key]!=m_identity[key])valid=false;
          if(valid){text=i18n::text("replay-view","VIEW REPLAY");m_button->setEnabled(true);}
        }catch(...){text=i18n::text("replay-damaged","Replay file damaged");}
        m_caption->setString(text.c_str());ui::fitFullMenuLabel(m_caption,m_button->getContentSize().width-10,20,.22f);unscheduleUpdate();
    }
public:static HistoryReplayButton* create(Json identity,float width){auto*p=new HistoryReplayButton;if(p->init(std::move(identity),width)){p->autorelease();return p;}delete p;return nullptr;}
};
}
