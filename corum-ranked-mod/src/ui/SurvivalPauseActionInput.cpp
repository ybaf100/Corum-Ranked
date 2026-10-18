#include "SurvivalPauseActionInput.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/UILayer.hpp>
#include <algorithm>
#include <limits>
using namespace geode::prelude;
namespace corum::ranked::ui {
SurvivalPauseActionInput* SurvivalPauseActionInput::create(CCNode* pause,PlayLayer* play,std::function<bool()> allowed){
    if(!pause || !play || !play->getParent())return nullptr;
    auto* input=new SurvivalPauseActionInput;
    if(!input->init()){delete input;return nullptr;}
    input->m_pause=WeakRef(pause);input->m_play=WeakRef(play);input->m_scene=WeakRef(play->getParent());input->m_allowed=std::move(allowed);
    // Another mod may retain a visible FLAlertLayer from an earlier scene.
    // The former global modal veto made every Pause action permanently invalid.
    std::vector<CCNode*> nodes{play->getParent()};
    while(!nodes.empty()){
        auto* node=nodes.back();nodes.pop_back();
        if(node!=pause && typeinfo_cast<FLAlertLayer*>(node) && node->isVisible())
            input->m_preexistingAlerts.emplace_back(node);
        if(node==play){if(play->m_uiLayer)nodes.push_back(play->m_uiLayer);continue;}
        for(auto* child:node->getChildrenExt())nodes.push_back(child);
    }
    input->setTouchEnabled(true);input->setID("survival-pause-action-input"_spr);input->scheduleUpdate();input->autorelease();return input;
}
void SurvivalPauseActionInput::bind(CCMenuItemSpriteExtra* item,std::function<void()> invoke){if(item)m_actions.push_back({WeakRef(item),std::move(invoke)});}
bool SurvivalPauseActionInput::valid()const{
    auto pause=m_pause.lock();auto play=m_play.lock();auto scene=m_scene.lock();auto* director=CCDirector::sharedDirector();
    // Pausing native PlayLayer can temporarily stop its scheduled/running state
    // on iPadOS even though its Pause window and scene remain authoritative.
    // Bind input to scene ownership, not the paused node's update lifecycle.
    if(!pause || !play || !scene || !pause->isVisible() || !scene->isRunning() || play.data()!=PlayLayer::get() ||
        play->getParent()!=scene.data() || director->getRunningScene()!=scene.data() || director->getNextScene() || !m_allowed || !m_allowed())return false;
    bool attached=false;for(auto* p=pause.data();p;p=p->getParent())if(p==scene.data()){attached=true;break;}
    if(!attached)return false;
    // Respect both Geode and vanilla confirmations, including dialogs opened
    // by other native Pause buttons. Never race their force-priority owner.
    std::vector<CCNode*> pending{scene.data()};
    while(!pending.empty()){
        auto* node=pending.back();pending.pop_back();if(!node->isVisible())continue;
        if(node!=pause.data() && typeinfo_cast<FLAlertLayer*>(node)){
            bool const preexisting=std::any_of(m_preexistingAlerts.begin(),m_preexistingAlerts.end(),[node](auto const& weak){
                auto alert=weak.lock();return alert && alert.data()==node;
            });
            if(!preexisting)return false;
        }
        // Do not walk the level's thousands of gameplay objects every paused
        // frame. Native/Geode dialogs live in the scene, Pause or UI layer.
        if(node==play.data()){if(play->m_uiLayer)pending.push_back(play->m_uiLayer);continue;}
        for(auto* child:node->getChildrenExt())pending.push_back(child);
    }
    return true;
}
bool SurvivalPauseActionInput::hit(std::size_t index,CCTouch* touch)const{
    if(index>=m_actions.size())return false;
    auto item=m_actions[index].item.lock();auto pause=m_pause.lock();if(!item || !pause || !item->isEnabled())return false;
    for(auto* node=static_cast<CCNode*>(item.data());node;node=node->getParent()){
        if(!node->isVisible())return false;
        if(node==pause.data()){
            auto const size=item->getContentSize();
            return size.width>0 && size.height>0 && CCRect(0,0,size.width,size.height).containsPoint(item->convertToNodeSpace(touch->getLocation()));
        }
    }
    return false;
}
void SurvivalPauseActionInput::cancelTouch(){
    if(m_pressed>=0 && static_cast<std::size_t>(m_pressed)<m_actions.size())if(auto item=m_actions[m_pressed].item.lock())item->unselected();
    m_touch=m_pressed=-1;
}
void SurvivalPauseActionInput::suspend(){++m_epoch;cancelTouch();m_pending=false;m_allowed={};}
void SurvivalPauseActionInput::registerWithTouchDispatcher(){
    log::info("[SurvivalPauseInput] registering targeted touch delegate");
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this,-500,true);
}
void SurvivalPauseActionInput::synchronize(){
    if(!isRunning())return;
    auto* dispatcher=CCDirector::sharedDirector()->getTouchDispatcher();
    auto* own=dispatcher->findHandler(this);
    if(!own){
        // Cocos may defer its onEnter registration while Pause was created
        // inside another touch dispatcher callback. Repair only after that
        // callback has unwound, and never register the same delegate twice.
        if(!m_registrationQueued && !m_repairedRegistration){
            m_registrationQueued=true;
            queueInMainThread([weak=WeakRef(this)]{
                auto input=weak.lock();
                if(!input || !input->isRunning())return;
                input->m_registrationQueued=false;
                auto* dispatcher=CCDirector::sharedDirector()->getTouchDispatcher();
                if(!dispatcher->findHandler(input.data())){
                    input->m_repairedRegistration=true;
                    input->registerWithTouchDispatcher();
                }
                input->synchronize();
            });
        }
        return;
    }
    if(!valid()){
        cancelTouch();
        if(m_syncWarnings++<3){
            auto pause=m_pause.lock();auto play=m_play.lock();auto scene=m_scene.lock();
            log::info("[SurvivalPauseInput] inactive pause={} play={} scene={} pauseRunning={} playRunning={} samePlay={} sceneRunning={} sceneMatches={} playParent={} nextScene={} allowed={} actionAllowed={}",
                static_cast<bool>(pause),static_cast<bool>(play),static_cast<bool>(scene),
                pause && pause->isRunning(),play && play->isRunning(),play && play.data()==PlayLayer::get(),
                scene && scene->isRunning(),scene && CCDirector::sharedDirector()->getRunningScene()==scene.data(),
                play && scene && play->getParent()==scene.data(),CCDirector::sharedDirector()->getNextScene()!=nullptr,
                static_cast<bool>(m_allowed),m_allowed && m_allowed());
        }
        return;
    }
    auto priority=own->m_nPriority;auto pause=m_pause.lock();auto play=m_play.lock();
    std::vector<CCNode*> pending{pause.data(),play.data(),play->m_uiLayer};
    // Only the exact native owners and the Pause subtree participate. Other
    // mods' global handlers and modal registrations are never rewritten.
    while(!pending.empty()){
        auto* node=pending.back();pending.pop_back();if(!node || node==this)continue;
        if(auto* layer=typeinfo_cast<CCLayer*>(node))if(auto* handler=dispatcher->findHandler(layer);handler && handler->m_nPriority>std::numeric_limits<int>::min())priority=std::min(priority,handler->m_nPriority-1);
        if(node!=play.data() && node!=play->m_uiLayer)for(auto* child:node->getChildrenExt())pending.push_back(child);
    }
    if(priority<own->m_nPriority)dispatcher->setPriority(priority,this);
    if(!m_logged){m_logged=true;log::info("[SurvivalPauseInput] ready priority={} actions={}",priority,m_actions.size());}
}
void SurvivalPauseActionInput::onEnter(){
    CCLayer::onEnter();
    log::info("[SurvivalPauseInput] onEnter registered={} actions={}",
        CCDirector::sharedDirector()->getTouchDispatcher()->findHandler(this)!=nullptr,m_actions.size());
    synchronize();
    // Pause can be constructed by a touch callback while registrations are
    // queued. Align after that dispatch has drained, before the next tap.
    queueInMainThread([weak=WeakRef(this)]{if(auto input=weak.lock();input && input->isRunning())input->synchronize();});
}
void SurvivalPauseActionInput::onExit(){++m_epoch;cancelTouch();m_pending=false;m_registrationQueued=false;CCLayer::onExit();}
void SurvivalPauseActionInput::update(float){synchronize();}
bool SurvivalPauseActionInput::ccTouchBegan(CCTouch* touch,CCEvent*){
    if(m_pending || m_touch!=-1 || !valid())return false;
    for(std::size_t i=0;i<m_actions.size();++i)if(hit(i,touch)){
        m_touch=touch->getID();m_pressed=static_cast<int>(i);if(auto item=m_actions[i].item.lock())item->selected();
        if(m_diagnostics++<10)log::info("[SurvivalPauseInput] began action={} touch={}",i,m_touch);
        return true;
    }
    return false;
}
void SurvivalPauseActionInput::ccTouchMoved(CCTouch* touch,CCEvent*){
    if(touch->getID()!=m_touch || m_pressed<0)return;
    if(!valid() || !hit(static_cast<std::size_t>(m_pressed),touch))cancelTouch();
}
void SurvivalPauseActionInput::ccTouchEnded(CCTouch* touch,CCEvent*){
    if(touch->getID()!=m_touch || m_pressed<0)return;
    auto const index=static_cast<std::size_t>(m_pressed);auto const activate=valid() && hit(index,touch);cancelTouch();if(!activate)return;
    m_pending=true;auto const epoch=m_epoch;
    queueInMainThread([weak=WeakRef(this),index,epoch]{
        auto input=weak.lock();if(!input || input->m_epoch!=epoch)return;input->m_pending=false;
        if(!input->valid() || index>=input->m_actions.size())return;
        auto item=input->m_actions[index].item.lock();if(item && item->isEnabled())input->m_actions[index].invoke();
    });
}
void SurvivalPauseActionInput::ccTouchCancelled(CCTouch* touch,CCEvent*){if(touch->getID()==m_touch)cancelTouch();}
}
