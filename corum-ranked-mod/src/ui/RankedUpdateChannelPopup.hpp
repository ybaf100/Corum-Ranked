#pragma once
#include "RankedPopupLayout.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "../domain/ModUpdatePolicy.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <functional>
namespace corum::ranked::ui {
using namespace geode::prelude;
// Q10 board: title and X, the installed build, RELEASE / DEBUG cards, the
// Debug password and a CLOSE / CONFIRM row.
class RankedUpdateChannelPopup final:public Popup {
    std::function<void(std::string)> m_choose;
    TextInput* m_password=nullptr;CCLabelBMFont *m_status=nullptr,*m_passwordCaption=nullptr;
    CCMenuItemSpriteExtra *m_release=nullptr,*m_debug=nullptr,*m_confirm=nullptr;
    bool m_closed=false,m_exited=false,m_busy=false;
    geode::async::TaskHolder<web::WebResponse> m_request;
    void choose(std::string channel){
        if(m_closed||m_busy)return;
        [[maybe_unused]] Ref<RankedUpdateChannelPopup> keepAlive(this);
        auto callback=std::move(m_choose);dismiss();if(callback)callback(std::move(channel));
    }
    void onRelease(CCObject*){choose("release");}
    void onDebug(CCObject*){
        if(m_closed||m_busy)return;m_password->setVisible(true);m_passwordCaption->setVisible(true);m_confirm->setVisible(true);
        m_debug->setNormalImage(cardFace("DEBUG",i18n::text("update-channel-debug-note","Development builds · password required"),true));
    }
    // .panel card: name over a caption; the chosen card carries the lime edge.
    static CCNode* cardFace(std::string const& name,std::string const& note,bool chosen){
        auto* face=surface({119,44},{},chosen?Accent:Steel,SurfaceRaised,255,true);
        auto* title=menuLabel(name,.32f,{9,29},chosen?Accent:Text,FontWeight::Black);fitSingleLineMenuLabel(title,100,16,.32f);title->setAnchorPoint({0,.5f});face->addChild(title,5);
        auto* caption=menuLabel(note,.18f,{9,13},TextMuted,FontWeight::SemiBold);fitSingleLineMenuLabel(caption,102,11,.18f);caption->setAnchorPoint({0,.5f});face->addChild(caption,5);
        return face;
    }
    void setBusy(bool busy){m_busy=busy;m_release->setEnabled(!busy);m_debug->setEnabled(!busy);m_confirm->setEnabled(!busy);m_password->setEnabled(!busy);}
    void onConfirm(CCObject*){
        if(m_closed||m_busy||m_password->getString().empty())return;
        matjson::Value body=matjson::Value::object();body["password"]=std::string(m_password->getString());m_password->setString("");setBusy(true);
        m_status->setColor(TextMuted);m_status->setString(i18n::text("loading","Loading...").c_str());
        auto request=web::WebRequest().timeout(std::chrono::seconds(8)).followRedirects(false);request.bodyJSON(body);
        m_request.spawn(request.post("https://api.hwanhee1.com/api/ranked/updates/channel/debug"),[weak=WeakRef(this)](web::WebResponse response){
            auto owner=weak.lock();if(!owner||owner->m_closed||!owner->getParent())return;
            owner->setBusy(false);auto const value=response.json().unwrapOr(matjson::Value());
            if(response.code()>=200&&response.code()<300&&value["authorized"].asBool().unwrapOr(false)&&value["channel"].asString().unwrapOr("")=="debug")owner->choose("debug");
            else{owner->m_status->setColor(Red);owner->m_status->setString(i18n::text("incorrect-password","Password verification failed.").c_str());}
        });
    }
    bool init(std::function<void(std::string)> choose){
        auto const layout=rankedPopupLayout({270,230},CCDirector::sharedDirector()->getWinSize());
        if(!Popup::init(layout.size.width,layout.size.height))return false;
        m_choose=std::move(choose);m_noElasticity=true;m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-update-channel-popup"_spr);
        auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);content->addChild(surface({270,230},{135,115},Accent,Surface,255,true));
        auto text=[&](std::string const& value,CCPoint at,float width,float height,float scale,ccColor3B color,FontWeight weight){
            auto* label=menuLabel(value,scale,at,color,weight);fitSingleLineMenuLabel(label,width,height,scale);label->setAnchorPoint({0,.5f});content->addChild(label,5);return label;
        };
        auto* title=text(i18n::text("update-channel","UPDATE CHANNEL"),{13,212},210,24,28.f/64.f,Text,FontWeight::Black);leanLabel(title);
        auto button=[&](CCNode* face,CCPoint at,SEL_MenuHandler handler){
            auto* item=CCMenuItemSpriteExtra::create(face,this,handler);item->setPosition(at);m_buttonMenu->addChild(item);return item;
        };
        // .xbtn
        auto* cross=surface({20,20},{},Steel,SurfaceRaised,250,true);
        auto* glyph=menuLabel("X",.26f,{10,10},Text,FontWeight::Black);fitSingleLineMenuLabel(glyph,14,14,.26f);cross->addChild(glyph,5);
        button(cross,{247,212},menu_selector(RankedUpdateChannelPopup::onClose));
        auto const channel=rankedBuildChannel();
        text(i18n::text("update-channel-installed","INSTALLED")+" "+std::string(Mod::get()->getVersion().toVString())+" · "+(channel=="debug"?"DEBUG":"RELEASE"),{13,191},244,12,.20f,TextMuted,FontWeight::SemiBold);
        m_release=button(cardFace("RELEASE",i18n::text("update-channel-release-note","Stable builds for everyone"),false),{72.5f,155},menu_selector(RankedUpdateChannelPopup::onRelease));
        m_debug=button(cardFace("DEBUG",i18n::text("update-channel-debug-note","Development builds · password required"),false),{197.5f,155},menu_selector(RankedUpdateChannelPopup::onDebug));
        m_passwordCaption=text(i18n::text("debug-password","Debug password"),{13,120},244,12,.20f,TextMuted,FontWeight::SemiBold);m_passwordCaption->setVisible(false);
        m_password=TextInput::create(244,i18n::text("debug-password","Debug password"));m_password->setPasswordMode(true);m_password->setMaxCharCount(128);m_password->setPosition({135,100});m_password->setVisible(false);content->addChild(m_password,10);
        m_status=text("",{13,72},244,14,.22f,Red,FontWeight::SemiBold);
        // .btn.ghost CLOSE and the filled CONFIRM.
        auto* ghost=surface({119,24},{},Steel,SurfaceRaised,255,true);
        auto* closeText=menuLabel(i18n::text("close","CLOSE"),.26f,{59.5f,12},Text,FontWeight::ExtraBold);fitSingleLineMenuLabel(closeText,105,16,.26f);ghost->addChild(closeText,5);
        button(ghost,{72.5f,26},menu_selector(RankedUpdateChannelPopup::onClose));
        auto* filled=surface({119,24},{},AccentFill,AccentFill,255,true);
        auto* confirmText=menuLabel(i18n::text("update-channel-confirm","CONFIRM"),.26f,{59.5f,12},AccentInk,FontWeight::ExtraBold);fitSingleLineMenuLabel(confirmText,105,16,.26f);filled->addChild(confirmText,5);
        m_confirm=button(filled,{197.5f,26},menu_selector(RankedUpdateChannelPopup::onConfirm));m_confirm->setVisible(false);
        return true;
    }
public:
    static RankedUpdateChannelPopup* create(std::function<void(std::string)> choose){auto* p=new RankedUpdateChannelPopup;if(p->init(std::move(choose))){p->autorelease();return p;}delete p;return nullptr;}
    void dismiss(){if(m_closed)return;[[maybe_unused]]Ref<RankedUpdateChannelPopup> keepAlive(this);m_closed=true;m_request.cancel();m_password->setString("");m_choose={};Popup::onClose(nullptr);}
    void onClose(CCObject*)override{dismiss();}
    void onExit()override{m_closed=true;m_request.cancel();if(m_password)m_password->setString("");m_choose={};if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}Popup::onExit();}
};
}
