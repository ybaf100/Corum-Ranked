#include "SurvivalBadgeCollection.hpp"
#include "../domain/SurvivalBadgeCollectionData.hpp"
#include "RankedTranslations.hpp"
#include "SurvivalCollectionBadge.hpp"
#include "../domain/SurvivalCollectionPresentation.hpp"
#include "RankedPopupLayout.hpp"
#include "../RankedRuntime.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/utils/web.hpp>
#include <cctype>
using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
std::string difficultyName(matjson::Value const& d){
    auto name=d["name"].asString().unwrapOr("");if(!name.empty())return name;
    auto id=d["id"].asString().unwrapOr("");
    for(std::size_t i=0;i<id.size();++i)id[i]=id[i]=='_'?' ':static_cast<char>(i==0||id[i-1]==' '?std::toupper(static_cast<unsigned char>(id[i])):std::tolower(static_cast<unsigned char>(id[i])));
    return id;
}
class BadgeDetail final:public Popup {
    std::string m_session;std::function<void()> m_closed;
    bool init(matjson::Value const& data,matjson::Value const& selected,bool eliminated,std::function<void()> closed){
        auto layout=rankedPopupLayout({410,292},CCDirector::sharedDirector()->getWinSize());
        if(!Popup::init(layout.size.width,layout.size.height))return false;
        m_session=RankedRuntime::get().spectatorLiveSessionToken();m_closed=std::move(closed);m_bgSprite->setVisible(false);
        auto* root=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);root->addChild(surface({410,292},{205,146},eliminated?Red:Accent,Surface,242,true));
        auto* title=menuLabel(eliminated?"ELIMINATED":difficultyName(selected),.36f,{205,266},eliminated?Red:Accent,FontWeight::Black);fitFullMenuLabel(title,350,24,.36f);root->addChild(title,5);
        auto const counts=eliminated?data["totals"]:selected["counts"];std::int64_t total=0;
        if(eliminated)total=counts["ELIMINATED"].asInt().unwrapOr(0);else total=survivalBadgeCollectionTotal(counts);
        root->addChild(menuLabel(fmt::format("{}: {}",i18n::text("badge-total","TOTAL"),total),.27f,{240,229},Text,FontWeight::ExtraBold));
        auto* preview=survivalCollectionBadge(static_cast<int>(selected["nativeCode"].asInt().unwrapOr(0)),eliminated?"ELIMINATED":"",44);
        preview->setPosition({62,229});root->addChild(preview,5);
        if(eliminated){
            auto rows=survivalBadgeCollectionRows(data);auto actual=data["eliminatedByDifficulty"].asArray().unwrapOr(std::vector<matjson::Value>{});
            for(auto const& r:actual)if(std::none_of(rows.begin(),rows.end(),[&](auto const& d){return d["id"]==r["difficulty"];}))rows.push_back(matjson::makeObject({{"id",r["difficulty"]},{"name",r["difficulty"]}}));
            auto const columns=rows.size()>7?2:1;auto const per=(rows.size()+columns-1)/columns;
            for(std::size_t i=0;i<rows.size();++i){auto const& d=rows[i];std::int64_t n=0;for(auto const& r:actual)if(r["difficulty"]==d["id"])n=r["count"].asInt().unwrapOr(0);
                float x=columns==1?205.f:108.f+194.f*(i/per),y=195.f-23.f*(i%per);
                auto* label=menuLabel(fmt::format("{}  {}",difficultyName(d),n),.24f,{x,y},Text);fitFullMenuLabel(label,columns==1?350:180,20,.24f);root->addChild(label,5);
            }
        }else for(std::size_t i=0;i<SurvivalCollectionGrades.size();++i){
            auto const grade=SurvivalCollectionGrades[i];auto const cell=survivalCollectionGradeCell(i);
            auto* badge=survivalCollectionBadge(static_cast<int>(selected["nativeCode"].asInt().unwrapOr(0)),grade,cell.side);
            badge->setPosition({cell.x,cell.y});root->addChild(badge,5);
            auto* count=menuLabel(fmt::format("{}",counts[std::string(grade)].asInt().unwrapOr(0)),.30f,{cell.x,cell.countY},survivalBadgeColor(grade),FontWeight::Black);
            fitFullMenuLabel(count,cell.side,18,.30f);root->addChild(count,5);
        }
        scheduleUpdate();return true;
    }
    void update(float)override{if(m_session!=RankedRuntime::get().spectatorLiveSessionToken())onClose(nullptr);}
    void onExit()override{auto closed=std::exchange(m_closed,{});Popup::onExit();if(closed)closed();}
public:
    static BadgeDetail* create(matjson::Value data,matjson::Value selected,bool eliminated,std::function<void()> closed){auto* p=new BadgeDetail;if(p->init(std::move(data),std::move(selected),eliminated,std::move(closed))){p->autorelease();return p;}delete p;return nullptr;}
};
class BadgeCollection final:public Popup {
    std::string m_session;std::uint64_t m_generation=0;bool m_closed=false,m_busy=false;
    matjson::Value m_data;CCNode* m_root=nullptr;CCLabelBMFont* m_status=nullptr;CCLabelBMFont* m_total=nullptr;std::vector<CCMenuItemSpriteExtra*> m_cards;
    geode::async::TaskHolder<web::WebResponse> m_request;
    bool init()override{
        // H4 board page (568 x 320 canvas): ink ground, the leaning brand with
        // its caption, TOTAL EARNED on the right, the badge strip and the hint.
        auto layout=rankedPopupLayout({568,320},CCDirector::sharedDirector()->getWinSize());if(!Popup::init(layout.size.width,layout.size.height))return false;
        setID("survival-badge-collection"_spr);m_bgSprite->setVisible(false);m_root=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        auto* ground=CCLayerColor::create({Ink.r,Ink.g,Ink.b,255},568,320);m_root->addChild(ground,0);
        auto* brand=menuLabel(i18n::text("badge-showcase","ROUND BADGE SHOWCASE"),30.f/64.f,{46,302},Text,FontWeight::Black);brand->setAnchorPoint({0,.5f});leanLabel(brand);
        fitMenuLabelWidth(brand,250,30.f/64.f,.2f);m_root->addChild(brand,5);
        m_status=menuLabel(i18n::text("loading","Loading..."),13.f/64.f,{46+brand->getScaledContentSize().width+10,302},TextMuted,FontWeight::SemiBold);m_status->setAnchorPoint({0,.5f});m_root->addChild(m_status,5);
        auto* earnedCaption=menuLabel(i18n::text("badge-total-earned","TOTAL EARNED"),13.f/64.f,{554,309},TextMuted,FontWeight::SemiBold);earnedCaption->setAnchorPoint({1,.5f});m_root->addChild(earnedCaption,5);
        m_total=menuLabel("-",24.f/64.f,{554,296},Accent,FontWeight::Black);m_total->setAnchorPoint({1,.5f});m_root->addChild(m_total,5);
        auto* hint=menuLabel(i18n::text("badge-tap","TAP A BADGE FOR DETAILS"),13.f/64.f,{284,62},TextMuted,FontWeight::SemiBold);fitFullMenuLabel(hint,520,14,13.f/64.f);m_root->addChild(hint,5);
        auto* face=slab({90,17},{},ccc3(58,63,78),true,Ink);auto* refreshLabel=menuLabel(i18n::text("refresh","REFRESH"),14.f/64.f,{45,8.5f},Text,FontWeight::ExtraBold);face->addChild(refreshLabel,5);
        auto* retry=CCMenuItemSpriteExtra::create(face,this,menu_selector(BadgeCollection::refresh));retry->setPosition({284,30});m_buttonMenu->addChild(retry);
        m_session=RankedRuntime::get().spectatorLiveSessionToken();scheduleUpdate();refresh(nullptr);return true;
    }
    void refresh(CCObject*){
        if(m_closed||m_busy)return;m_busy=true;auto const generation=++m_generation;auto const session=m_session;
        m_status->setString(i18n::text("loading","Loading...").c_str());
        clearCards();
        web::WebRequest request;request.header("Authorization","Bearer "+session);request.followRedirects(false);request.timeout(std::chrono::seconds(15));
        m_request.spawn(request.get("https://api.52-74-37-130.sslip.io/api/ranked/survival/badges"),[weak=WeakRef(this),generation,session](web::WebResponse response){
            auto p=weak.lock();if(!p||p->m_closed||p->m_generation!=generation||session!=RankedRuntime::get().spectatorLiveSessionToken())return;p->m_busy=false;
            auto json=response.json();bool const valid=response.code()==200&&json.isOk()&&survivalBadgeCollectionValid(json.unwrap());
            if(!valid){
                p->m_status->setString(i18n::text("badge-load-failed","COULD NOT LOAD BADGES · RETRY").c_str());p->m_status->setColor(Red);return;
            }
            p->m_data=json.unwrap();auto rows=survivalBadgeCollectionRows(p->m_data);
            p->m_status->setString(i18n::text("badge-official-rounds","OFFICIAL SURVIVAL ROUND RESULTS").c_str());p->m_status->setColor(TextMuted);
            std::int64_t earned=0;for(auto const& row:rows)earned+=survivalBadgeCollectionTotal(row["counts"]);
            p->m_total->setString(fmt::format("{}",earned).c_str());
            p->layoutCards(rows,p->m_data["totals"]["ELIMINATED"].asInt().unwrapOr(0));
        });
    }
    void clearCards(){for(auto* card:m_cards)card->removeFromParentAndCleanup(true);m_cards.clear();}
    // Every badge on one board: up to six cards a row, rows shrink to fit and
    // the last row is centred. Nothing is clipped or dragged, so the board
    // reads the same on 16:9 phones and 4:3 iPads.
    void layoutCards(std::vector<matjson::Value> const& rows,std::int64_t eliminated){
        clearCards();
        auto const count=rows.size()+1;
        auto const columns=std::min<std::size_t>(count,6),lines=(count+columns-1)/columns;
        constexpr float left=14,right=554,bottom=78,top=274,gap=8;
        auto const cellW=std::min(96.f,(right-left-gap*static_cast<float>(columns-1))/static_cast<float>(columns));
        auto const cellH=std::min(132.f,(top-bottom-gap*static_cast<float>(lines-1))/static_cast<float>(lines));
        auto const gridH=cellH*static_cast<float>(lines)+gap*static_cast<float>(lines-1);
        auto const gridTop=(top+bottom+gridH)/2;
        for(std::size_t i=0;i<count;++i){
            auto const last=i==rows.size();auto const line=i/columns,column=i%columns;
            auto const inLine=std::min(columns,count-line*columns);
            auto const lineW=cellW*static_cast<float>(inLine)+gap*static_cast<float>(inLine-1);
            auto const x=(left+right-lineW)/2+static_cast<float>(column)*(cellW+gap)+cellW/2;
            auto const y=gridTop-static_cast<float>(line)*(cellH+gap)-cellH/2;
            // H4 card (.panel): a flat surface behind each badge; the badge
            // keeps its face, grade frame and the X for eliminations.
            auto* face=CCNode::create();face->setContentSize({cellW,cellH});
            auto* card=CCLayerColor::create({Surface.r,Surface.g,Surface.b,255},cellW,cellH);face->addChild(card,1);
            auto const edge=last?Red:SurfaceSoft;
            auto* rule=CCLayerColor::create({edge.r,edge.g,edge.b,255},cellW,1.5f);rule->setPosition({0,cellH-1.5f});face->addChild(rule,2);
            auto const side=std::min(cellW*.86f,cellH*.6f);
            auto* cell=survivalCollectionBadge(last?0:static_cast<int>(rows[i]["nativeCode"].asInt().unwrapOr(0)),last?"ELIMINATED":"",side);
            cell->setPosition({cellW/2,cellH*.58f});face->addChild(cell,5);
            auto const total=last?eliminated:survivalBadgeCollectionTotal(rows[i]["counts"]);
            auto* label=menuLabel(fmt::format("{}",total),.34f,{cellW/2,std::max(12.f,cellH*.13f)},last?Red:Text,FontWeight::ExtraBold);
            fitFullMenuLabel(label,cellW-6,22,.34f);face->addChild(label,5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(BadgeCollection::onCard));
            item->setTag(static_cast<int>(i));item->setPosition({x,y});m_buttonMenu->addChild(item);m_cards.push_back(item);
        }
    }
    void onCard(CCObject* sender){if(sender)detail(static_cast<std::size_t>(sender->getTag()));}
    void detail(std::size_t index){
        if(m_closed||m_cards.empty())return;auto rows=survivalBadgeCollectionRows(m_data);if(index>rows.size())return;
        if(auto* popup=BadgeDetail::create(m_data,index<rows.size()?rows[index]:matjson::Value(),index==rows.size(),[]{}))popup->show();
    }
    void update(float)override{if(m_session!=RankedRuntime::get().spectatorLiveSessionToken())onClose(nullptr);}
    void onExit()override{m_closed=true;++m_generation;m_request.cancel();unscheduleUpdate();Popup::onExit();}
public:
    static BadgeCollection* create(){auto* p=new BadgeCollection;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
};
}
void showSurvivalBadgeCollection(){auto* scene=CCDirector::sharedDirector()->getRunningScene();if(!scene||scene->getChildByIDRecursive("survival-badge-collection"_spr))return;if(auto* p=BadgeCollection::create())p->show();}
}
