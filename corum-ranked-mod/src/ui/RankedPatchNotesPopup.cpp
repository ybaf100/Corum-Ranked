#include "RankedPatchNotesPopup.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankedPopupLayout.hpp"
#include "../RankedRuntime.hpp"
#include "RankedPatchNotesCache.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <chrono>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
constexpr float Width=460, Height=270, Pad=12;
CCNode* segmentFace(std::string const& value,float width,bool on) {
    auto* face=surface({width,18},{},on?Accent:Steel,on?Accent:SurfaceRaised,255,true);
    auto* label=menuLabel(value,.21f,{width/2,9},on?AccentInk:TextMuted,FontWeight::ExtraBold);
    fitSingleLineMenuLabel(label,width-6,13,.21f);face->addChild(label,5);return face;
}
} // namespace

RankedPatchNotesPopup* RankedPatchNotesPopup::create(std::string highlight) {
    auto* popup=new RankedPatchNotesPopup();
    popup->m_highlight=std::move(highlight);
    if(popup->init()){popup->autorelease();return popup;}
    delete popup;return nullptr;
}

bool RankedPatchNotesPopup::init() {
    auto const layout=rankedPopupLayout({Width,Height},CCDirector::sharedDirector()->getWinSize());
    if(!Popup::init(layout.size.width,layout.size.height))return false;
    setID("ranked-patch-notes-popup"_spr);m_noElasticity=true;
    m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);m_closeBtn->setEnabled(false);
    m_korean=corum::ranked::i18n::isKorean();
    auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
    m_panel=surface({Width,Height},{Width/2,Height/2},Accent,Surface,255,true);content->addChild(m_panel,1);
    auto* title=menuLabel(i18n::text("patch-notes","PATCH NOTES"),28.f/64.f,{Pad,Height-18},Text,FontWeight::Black);
    title->setAnchorPoint({0,.5f});fitSingleLineMenuLabel(title,150,20,28.f/64.f);leanLabel(title);m_panel->addChild(title,8);
    auto* menu=m_buttonMenu;menu->setZOrder(20);
    auto button=[&](CCNode* face,CCPoint at,SEL_MenuHandler handler,int tag=0){
        auto* item=CCMenuItemSpriteExtra::create(face,this,handler);item->setPosition(at);item->setTag(tag);menu->addChild(item);return item;
    };
    // KO / EN: the language of the notes (starts with the UI language).
    m_languages[0]=button(segmentFace("KO",34,m_korean),{Width-Pad-20-6-74,Height-18},menu_selector(RankedPatchNotesPopup::onLanguage),1);
    m_languages[1]=button(segmentFace("EN",34,!m_korean),{Width-Pad-20-6-38,Height-18},menu_selector(RankedPatchNotesPopup::onLanguage),0);
    m_languages[0]->setID("ranked-patch-notes-ko"_spr);m_languages[1]->setID("ranked-patch-notes-en"_spr);
    auto* cross=surface({20,20},{},Steel,SurfaceRaised,250,true);
    auto* glyph=menuLabel("X",.26f,{10,10},Text,FontWeight::Black);fitSingleLineMenuLabel(glyph,14,14,.26f);cross->addChild(glyph,5);
    button(cross,{Width-Pad-10,Height-18},menu_selector(RankedPatchNotesPopup::onClose))->setID("ranked-patch-notes-close"_spr);
    render();fetch();return true;
}

void RankedPatchNotesPopup::fetch() {
    if(m_closed)return;
    auto const generation=++m_generation;
    web::WebRequest request;[](auto& q){if constexpr(requires{q.getHttpVersion();})q.version(decltype(q.getHttpVersion())::VERSION_2TLS);}(request); /* HTTP/2: see PREFER_HTTP2 note in RankedRuntime.cpp */request.timeout(std::chrono::seconds(20));
    m_request.spawn(request.get(RankedRuntime::get().patchNotesUrl()),[weak=WeakRef(this),generation](web::WebResponse response) {
        queueInMainThread([weak,generation,response=std::move(response)] {
            auto self=weak.lock();if(!self || self->m_closed || generation!=self->m_generation)return;
            self->m_loaded=true;
            self->m_notes=response.ok()?patchNotesFromJson(response.json().unwrapOr(matjson::Value())):std::vector<PatchNoteView>{};
            self->m_failed=self->m_notes.empty();
            self->render();
        });
    });
}

// Newest first: each release's version and date, then its lines. The
// release being installed is marked in the accent colour.
void RankedPatchNotesPopup::render() {
    if(m_closed)return;
    if(m_contents)m_contents->removeFromParentAndCleanup(true);
    m_contents=CCNode::create();m_panel->addChild(m_contents,6);
    for(std::size_t i=0;i<m_languages.size();++i)if(m_languages[i])
        m_languages[i]->setNormalImage(segmentFace(i?"EN":"KO",34,(i==0)==m_korean));
    auto const viewW=Width-Pad*2,viewH=Height-44;
    if(!m_loaded||m_failed){
        auto* status=menuLabel(!m_loaded?corum::ranked::i18n::literal("inline.loading"):corum::ranked::i18n::literal("inline.could-not-load-press-refresh"),
            .24f,{Width/2,Height/2-10},m_failed?Red:TextMuted,FontWeight::SemiBold);
        fitSingleLineMenuLabel(status,viewW,14,.24f);m_contents->addChild(status,8);
        return;
    }
    std::vector<CCNode*> items;std::vector<float> gaps;float total=4;
    for(auto const& note:m_notes){
        auto const mine=!m_highlight.empty()&&samePatchVersion(note.version,m_highlight);
        auto* head=menuLabel(note.date.empty()?"v"+note.version:fmt::format("v{}  ·  {}",note.version,note.date),.27f,{},mine?Accent:Text,FontWeight::Black);
        head->setAnchorPoint({0,1});fitSingleLineMenuLabel(head,viewW-8,16,.27f);
        items.push_back(head);gaps.push_back(items.size()==1?0:10);total+=gaps.back()+head->getScaledContentSize().height;
        for(auto const& line:note.lines(m_korean)){
            auto* text=CCLabelTTF::create(("- "+line).c_str(),patchNotesFont(),8.5f,{viewW-16,0},kCCTextAlignmentLeft);
            if(!text)continue;
            text->setAnchorPoint({0,1});text->setColor(mine?Text:ccColor3B{200,204,214});
            items.push_back(text);gaps.push_back(3);total+=3+text->getContentSize().height;
        }
    }
    auto* scroll=ScrollLayer::create({viewW,viewH});scroll->setPosition({Pad,Pad});scroll->setID("ranked-patch-notes-scroll"_spr);
    auto const contentH=std::max(viewH,total);scroll->m_contentLayer->setContentSize({viewW,contentH});
    float y=contentH-4;
    for(std::size_t i=0;i<items.size();++i){
        y-=gaps[i];
        auto const indent=dynamic_cast<CCLabelTTF*>(items[i])?10.0f:2.0f;
        items[i]->setPosition({indent,y});scroll->m_contentLayer->addChild(items[i]);
        y-=items[i]->getScaledContentSize().height;
    }
    m_contents->addChild(scroll,5);scroll->scrollToTop();
}

void RankedPatchNotesPopup::onLanguage(CCObject* sender) {
    if(m_closed||!sender)return;
    auto const korean=static_cast<CCNode*>(sender)->getTag()==1;
    if(korean==m_korean)return;
    m_korean=korean;render();
}
void RankedPatchNotesPopup::onClose(CCObject* sender) {
    if(m_closed)return;m_closed=true;++m_generation;m_request.cancel();Popup::onClose(sender);
}
void RankedPatchNotesPopup::onExit() { m_closed=true;++m_generation;m_request.cancel();Popup::onExit(); }
} // namespace corum::ranked::ui
