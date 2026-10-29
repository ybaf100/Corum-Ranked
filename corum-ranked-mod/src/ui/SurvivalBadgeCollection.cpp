#include "SurvivalBadgeCollection.hpp"
#include "../domain/SurvivalBadgeCollectionData.hpp"
#include "RankedTranslations.hpp"
#include "SurvivalRoundBadge.hpp"
#include "RankedPopupLayout.hpp"
#include "../RankedRuntime.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/utils/web.hpp>
using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
std::string difficultyName(matjson::Value const& d){auto id=d["id"].asString().unwrapOr("");return i18n::text("badge-difficulty-"+id,d["name"].asString().unwrapOr(id));}
CCNode* difficultyFace(matjson::Value const& d,float extent){
    auto* face=GJDifficultySprite::create(static_cast<int>(d["nativeCode"].asInt().unwrapOr(0)),GJDifficultyName::Short);
    if(face){auto s=face->getContentSize();face->setScale(extent/std::max({1.f,s.width,s.height}));}return face;
}
class BadgeDetail final:public Popup {
    std::string m_session;std::function<void()> m_closed;
    bool init(matjson::Value const& data,matjson::Value const& selected,bool eliminated,std::function<void()> closed){
        auto layout=rankedPopupLayout({410,292},CCDirector::sharedDirector()->getWinSize());
        if(!Popup::init(layout.size.width,layout.size.height))return false;
        m_session=RankedRuntime::get().spectatorLiveSessionToken();m_closed=std::move(closed);m_bgSprite->setVisible(false);
        auto* root=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);root->addChild(surface({410,292},{205,146},eliminated?Red:Cyan,Surface,255,true));
        auto* title=menuLabel(eliminated?"ELIMINATED":difficultyName(selected),.36f,{205,266},eliminated?Red:Cyan,FontWeight::Black);fitFullMenuLabel(title,350,24,.36f);root->addChild(title,5);
        auto const counts=eliminated?data["totals"]:selected["counts"];std::int64_t total=0;
        if(eliminated)total=counts["ELIMINATED"].asInt().unwrapOr(0);else for(auto g:SurvivalBadgeGrades)total+=counts[std::string(g)].asInt().unwrapOr(0);
        root->addChild(menuLabel(fmt::format("{}: {}",i18n::text("badge-total","TOTAL"),total),.27f,{240,229},Text,FontWeight::ExtraBold));
        if(eliminated){matjson::Value b=matjson::makeObject({{"grade","ELIMINATED"}});if(auto* card=survivalRoundBadge(b,{54,50},false)){card->setPosition({58,226});root->addChild(card,5);}}
        else if(auto* icon=difficultyFace(selected,40)){icon->setPosition({58,229});root->addChild(icon,5);}
        if(eliminated){
            auto rows=survivalBadgeCollectionRows(data);auto actual=data["eliminatedByDifficulty"].asArray().unwrapOr(std::vector<matjson::Value>{});
            for(auto const& r:actual)if(std::none_of(rows.begin(),rows.end(),[&](auto const& d){return d["id"]==r["difficulty"];}))rows.push_back(matjson::makeObject({{"id",r["difficulty"]},{"name",r["difficulty"]}}));
            auto const columns=rows.size()>7?2:1;auto const per=(rows.size()+columns-1)/columns;
            for(std::size_t i=0;i<rows.size();++i){auto const& d=rows[i];std::int64_t n=0;for(auto const& r:actual)if(r["difficulty"]==d["id"])n=r["count"].asInt().unwrapOr(0);
                float x=columns==1?205.f:108.f+194.f*(i/per),y=195.f-23.f*(i%per);
                auto* label=menuLabel(fmt::format("{}  {}",difficultyName(d),n),.24f,{x,y},Text);fitFullMenuLabel(label,columns==1?350:180,20,.24f);root->addChild(label,5);
            }
        }else for(std::size_t i=0;i<5;++i){
            auto const grade=std::string(SurvivalBadgeGrades[i]);auto const y=190.f-36.f*i;
            auto b=matjson::makeObject({{"grade",grade}});if(auto* card=survivalRoundBadge(b,{105,30},false)){card->setPosition({100,y});root->addChild(card,5);}
            root->addChild(menuLabel(fmt::format("{}",counts[grade].asInt().unwrapOr(0)),.32f,{287,y},survivalBadgeColor(grade),FontWeight::Black));
        }
        scheduleUpdate();return true;
    }
    void update(float)override{if(m_session!=RankedRuntime::get().spectatorLiveSessionToken())onClose(nullptr);}
    void onExit()override{auto closed=std::exchange(m_closed,{});Popup::onExit();if(closed)closed();}
public:
    static BadgeDetail* create(matjson::Value data,matjson::Value selected,bool eliminated,std::function<void()> closed){auto* p=new BadgeDetail;if(p->init(std::move(data),std::move(selected),eliminated,std::move(closed))){p->autorelease();return p;}delete p;return nullptr;}
};
// Own the complete gesture: a drag can never activate a badge. All content is
// clipped, with a fixed five-slot viewport on every supported aspect ratio.
class BadgeStrip final:public CCLayer {
    CCNode* m_content=nullptr;std::function<void(std::size_t)> m_tap;std::size_t m_count=0;
    float m_offset=0,m_origin=0,m_distance=0;CCPoint m_start{};int m_touch=-1;
public:
    static BadgeStrip* create(std::vector<matjson::Value> const& rows,std::function<void(std::size_t)> tap){
        auto* p=new BadgeStrip;if(!p->init()){delete p;return nullptr;}p->autorelease();p->setContentSize({440,112});p->m_count=rows.size()+1;p->m_tap=std::move(tap);
        auto* stencil=CCDrawNode::create();CCPoint points[]={{0,0},{440,0},{440,112},{0,112}};stencil->drawPolygon(points,4,{1,1,1,1},0,{1,1,1,1});
        auto* clip=CCClippingNode::create(stencil);p->addChild(clip);p->m_content=CCNode::create();clip->addChild(p->m_content);
        for(std::size_t i=0;i<p->m_count;++i){auto const last=i==rows.size();auto* cell=surface({82,106},{44.f+88*i,56},last?Red:Blue,SurfaceRaised,255,true);p->m_content->addChild(cell);
            if(last)cell->addChild(menuLabel("X",.85f,{41,66},Red,FontWeight::Black));else if(auto* icon=difficultyFace(rows[i],42)){icon->setPosition({41,66});cell->addChild(icon,5);}
            auto* label=menuLabel(last?"ELIMINATED":difficultyName(rows[i]),.23f,{41,24},last?Red:Text,FontWeight::ExtraBold);fitFullMenuLabel(label,72,30,.23f);cell->addChild(label,5);
        }
        p->setTouchEnabled(true);return p;
    }
    void registerWithTouchDispatcher()override{CCDirector::sharedDirector()->getTouchDispatcher()->addPrioTargetedDelegate(this,-65000,true);}
    bool ccTouchBegan(CCTouch* touch,CCEvent*)override{if(m_touch!=-1||!isVisible())return false;auto at=convertToNodeSpace(touch->getLocation());if(!CCRect(0,0,440,112).containsPoint(at))return false;m_start=at;m_origin=m_offset;m_distance=0;m_touch=touch->getID();return true;}
    void ccTouchMoved(CCTouch* touch,CCEvent*)override{if(touch->getID()!=m_touch)return;auto at=convertToNodeSpace(touch->getLocation());m_distance=std::max(m_distance,std::hypot(at.x-m_start.x,at.y-m_start.y));m_offset=survivalBadgeGalleryOffset(m_origin+m_start.x-at.x,m_count,440);m_content->setPositionX(-m_offset);}
    void ccTouchEnded(CCTouch* touch,CCEvent* event)override{if(touch->getID()!=m_touch)return;ccTouchMoved(touch,event);m_touch=-1;auto at=convertToNodeSpace(touch->getLocation());if(m_distance<=6&&CCRect(0,0,440,112).containsPoint(at)&&m_tap)m_tap(static_cast<std::size_t>((at.x+m_offset)/88));}
    void ccTouchCancelled(CCTouch* touch,CCEvent*)override{if(touch->getID()==m_touch)m_touch=-1;}
    void onExit()override{m_touch=-1;m_tap={};CCLayer::onExit();}
};
class BadgeCollection final:public Popup {
    std::string m_session;std::uint64_t m_generation=0;bool m_closed=false,m_busy=false;
    matjson::Value m_data;CCNode* m_root=nullptr;CCLabelBMFont* m_status=nullptr;BadgeStrip* m_strip=nullptr;
    geode::async::TaskHolder<web::WebResponse> m_request;
    bool init()override{
        auto layout=rankedPopupLayout({480,260},CCDirector::sharedDirector()->getWinSize());if(!Popup::init(layout.size.width,layout.size.height))return false;
        setID("survival-badge-collection"_spr);m_bgSprite->setVisible(false);m_root=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        m_root->addChild(surface({480,260},{240,130},Gold,Surface,255,true));
        m_root->addChild(menuLabel(i18n::text("badge-collection","ROUND BADGES"),.39f,{240,231},Gold,FontWeight::Black));
        m_status=menuLabel(i18n::text("loading","Loading..."),.24f,{240,190},TextMuted);m_root->addChild(m_status,5);
        auto* hint=menuLabel(i18n::text("badge-drag","DRAG TO EXPLORE · TAP FOR DETAILS"),.22f,{240,60},TextMuted);fitFullMenuLabel(hint,440,20,.22f);m_root->addChild(hint,5);
        auto* face=surface({120,26},{},Blue,SurfaceRaised,255,true);face->addChild(menuLabel(i18n::text("refresh","REFRESH"),.25f,{60,13},Text));auto* retry=CCMenuItemSpriteExtra::create(face,this,menu_selector(BadgeCollection::refresh));retry->setPosition({240,28});m_buttonMenu->addChild(retry);
        m_session=RankedRuntime::get().spectatorLiveSessionToken();scheduleUpdate();refresh(nullptr);return true;
    }
    void refresh(CCObject*){
        if(m_closed||m_busy)return;m_busy=true;auto const generation=++m_generation;auto const session=m_session;
        m_status->setString(i18n::text("loading","Loading...").c_str());
        if(m_strip){m_strip->removeFromParentAndCleanup(true);m_strip=nullptr;}
        web::WebRequest request;request.header("Authorization","Bearer "+session);request.followRedirects(false);request.timeout(std::chrono::seconds(15));
        m_request.spawn(request.get("https://api.52-74-37-130.sslip.io/api/ranked/survival/badges"),[weak=WeakRef(this),generation,session](web::WebResponse response){
            auto p=weak.lock();if(!p||p->m_closed||p->m_generation!=generation||session!=RankedRuntime::get().spectatorLiveSessionToken())return;p->m_busy=false;
            auto json=response.json();bool const valid=response.code()==200&&json.isOk()&&survivalBadgeCollectionValid(json.unwrap());
            if(!valid){
                p->m_status->setString(i18n::text("badge-load-failed","COULD NOT LOAD BADGES · RETRY").c_str());p->m_status->setColor(Red);return;
            }
            p->m_data=json.unwrap();auto rows=survivalBadgeCollectionRows(p->m_data);
            p->m_status->setString(i18n::text("badge-official-rounds","OFFICIAL SURVIVAL ROUND RESULTS").c_str());p->m_status->setColor(TextMuted);
            p->m_strip=BadgeStrip::create(rows,[weak](std::size_t index){if(auto owner=weak.lock())owner->detail(index);});p->m_strip->setPosition({20,74});p->m_root->addChild(p->m_strip,5);
        });
    }
    void detail(std::size_t index){
        if(m_closed||!m_strip)return;auto rows=survivalBadgeCollectionRows(m_data);if(index>rows.size())return;
        auto* popup=BadgeDetail::create(m_data,index<rows.size()?rows[index]:matjson::Value(),index==rows.size(),[weak=WeakRef(this)]{if(auto owner=weak.lock();owner&&!owner->m_closed&&owner->m_strip)owner->m_strip->setTouchEnabled(true);});
        if(popup){m_strip->setTouchEnabled(false);popup->show();}
    }
    void update(float)override{if(m_session!=RankedRuntime::get().spectatorLiveSessionToken())onClose(nullptr);}
    void onExit()override{m_closed=true;++m_generation;m_request.cancel();unscheduleUpdate();Popup::onExit();}
public:
    static BadgeCollection* create(){auto* p=new BadgeCollection;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
};
}
void showSurvivalBadgeCollection(){auto* scene=CCDirector::sharedDirector()->getRunningScene();if(!scene||scene->getChildByIDRecursive("survival-badge-collection"_spr))return;if(auto* p=BadgeCollection::create())p->show();}
}
