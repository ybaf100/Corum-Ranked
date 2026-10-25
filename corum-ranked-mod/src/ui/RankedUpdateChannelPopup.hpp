#pragma once
#include "RankedPopupLayout.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <functional>
namespace corum::ranked::ui {
using namespace geode::prelude;
class RankedUpdateChannelPopup final:public Popup {
    std::function<void(std::string)> m_choose;
    TextInput* m_password=nullptr;CCLabelBMFont* m_status=nullptr;
    CCMenuItemSpriteExtra *m_release=nullptr,*m_debug=nullptr,*m_confirm=nullptr;
    bool m_closed=false,m_exited=false,m_busy=false;
    geode::async::TaskHolder<web::WebResponse> m_request;
    void choose(std::string channel){
        if(m_closed||m_busy)return;
        [[maybe_unused]] Ref<RankedUpdateChannelPopup> keepAlive(this);
        auto callback=std::move(m_choose);dismiss();if(callback)callback(std::move(channel));
    }
    void onRelease(CCObject*){choose("release");}
    void onDebug(CCObject*){if(m_closed||m_busy)return;m_password->setVisible(true);m_confirm->setVisible(true);}
    void setBusy(bool busy){m_busy=busy;m_release->setEnabled(!busy);m_debug->setEnabled(!busy);m_confirm->setEnabled(!busy);m_password->setEnabled(!busy);}
    void onConfirm(CCObject*){
        if(m_closed||m_busy||m_password->getString().empty())return;
        matjson::Value body=matjson::Value::object();body["password"]=std::string(m_password->getString());m_password->setString("");setBusy(true);
        m_status->setString(i18n::text("loading","Loading...").c_str());
        auto request=web::WebRequest().timeout(std::chrono::seconds(8)).followRedirects(false);request.bodyJSON(body);
        m_request.spawn(request.post("https://api.52-74-37-130.sslip.io/api/ranked/updates/channel/debug"),[weak=WeakRef(this)](web::WebResponse response){
            auto owner=weak.lock();if(!owner||owner->m_closed||!owner->getParent())return;
            owner->setBusy(false);auto const value=response.json().unwrapOr(matjson::Value());
            if(response.code()>=200&&response.code()<300&&value["authorized"].asBool().unwrapOr(false)&&value["channel"].asString().unwrapOr("")=="debug")owner->choose("debug");
            else owner->m_status->setString(i18n::text("incorrect-password","Password verification failed.").c_str());
        });
    }
    bool init(std::function<void(std::string)> choose){
        auto const layout=rankedPopupLayout({300,240},CCDirector::sharedDirector()->getWinSize());
        if(!Popup::init(layout.size.width,layout.size.height))return false;
        m_choose=std::move(choose);m_noElasticity=true;m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-update-channel-popup"_spr);
        auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);content->addChild(surface({300,240},{150,120},Cyan,Surface,255,true));
        auto* title=menuLabel(i18n::text("update-channel","UPDATE CHANNEL"),.42f,{150,215},Text,FontWeight::Black);fitSingleLineMenuLabel(title,250,24,.42f);content->addChild(title,5);
        auto add=[&](std::string caption,CCPoint at,SEL_MenuHandler handler){
            auto* face=surface({124,30},{},Blue,SurfaceRaised,255,true);auto* text=menuLabel(caption,.29f,{62,15},Text,FontWeight::ExtraBold);fitSingleLineMenuLabel(text,110,22,.29f);face->addChild(text,5);
            auto* button=CCMenuItemSpriteExtra::create(face,this,handler);button->setPosition(at);m_buttonMenu->addChild(button);return button;
        };
        m_release=add("RELEASE",{80,173},menu_selector(RankedUpdateChannelPopup::onRelease));m_debug=add("DEBUG",{220,173},menu_selector(RankedUpdateChannelPopup::onDebug));
        m_password=TextInput::create(244,i18n::text("debug-password","Debug password"));m_password->setPasswordMode(true);m_password->setMaxCharCount(128);m_password->setPosition({150,121});m_password->setVisible(false);content->addChild(m_password,10);
        m_status=menuLabel("",.24f,{150,84},TextMuted,FontWeight::SemiBold);fitSingleLineMenuLabel(m_status,264,22,.24f);content->addChild(m_status,5);
        m_confirm=add(i18n::text("confirm","CONFIRM"),{80,37},menu_selector(RankedUpdateChannelPopup::onConfirm));m_confirm->setVisible(false);
        add(i18n::text("close","CLOSE"),{220,37},menu_selector(RankedUpdateChannelPopup::onClose));return true;
    }
public:
    static RankedUpdateChannelPopup* create(std::function<void(std::string)> choose){auto* p=new RankedUpdateChannelPopup;if(p->init(std::move(choose))){p->autorelease();return p;}delete p;return nullptr;}
    void dismiss(){if(m_closed)return;[[maybe_unused]]Ref<RankedUpdateChannelPopup> keepAlive(this);m_closed=true;m_request.cancel();m_password->setString("");m_choose={};Popup::onClose(nullptr);}
    void onClose(CCObject*)override{dismiss();}
    void onExit()override{m_closed=true;m_request.cancel();if(m_password)m_password->setString("");m_choose={};if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}Popup::onExit();}
};
}
