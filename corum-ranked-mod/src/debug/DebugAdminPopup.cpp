#include "DebugMenuPopup.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../RankedRuntime.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedTranslations.hpp"
#include "../ui/RankedConfirmPopup.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/FLAlertLayerProtocol.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <charconv>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::debug {
namespace {
class DebugAudioVolumePopup final : public Popup {
    struct Song { int id=0, percent=100; std::vector<std::string> keys; };
    std::vector<Song> m_songs;
    std::size_t m_index=0;
    Slider* m_slider=nullptr;
    CCLabelBMFont *m_song=nullptr,*m_id=nullptr,*m_percent=nullptr,*m_status=nullptr;
    CCMenuItemSpriteExtra *m_previous=nullptr,*m_next=nullptr,*m_save=nullptr,*m_reload=nullptr;
    async::TaskHolder<web::WebResponse> m_request;
    std::string m_password,m_session;
    bool m_closed=false,m_exited=false,m_busy=false;

    static bool allowed() {
        auto* account=GJAccountManager::get();
        return account && account->m_accountID>0 && std::string(account->m_username)=="hwanhee1";
    }
    CCMenuItemSpriteExtra* button(CCMenu* menu, std::string text, CCPoint point, float width, int tag) {
        auto* face=ui::surface({width,28},{},ui::Blue,ui::SurfaceRaised,255,true);
        auto* caption=ui::menuLabel(text,.36f,{width/2,14},ui::Text,ui::FontWeight::ExtraBold);
        ui::fitMenuLabelWidth(caption,width-12,.36f,.26f); face->addChild(caption,5);
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(DebugAudioVolumePopup::onSelect));
        item->setTag(tag); item->setPosition(point); menu->addChild(item); return item;
    }
    bool init(std::string password) {
        auto& runtime=RankedRuntime::get();
        if (!allowed() || password.empty() || runtime.view().stage!=RuntimeStage::Ready || runtime.spectatorLiveSessionToken().empty()) return false;
        if (!Popup::init(384,280)) return false;
        m_password=std::move(password); m_session=runtime.spectatorLiveSessionToken();
        m_bgSprite->setVisible(false); setID("ranked-admin-audio-volume"_spr);
        auto* panel=ui::surface({384,280},{192,140},ui::Cyan,ui::Surface,255,true); m_mainLayer->addChild(panel);
        auto* title=ui::menuLabel(i18n::text("admin-audio-title","Song volume (all players)"),.46f,{192,257},ui::Cyan,ui::FontWeight::Black);
        ui::fitMenuLabelWidth(title,334,.46f,.34f); panel->addChild(title,10);
        auto* scope=ui::menuLabel(i18n::text("admin-audio-scope","Ranked menu BGM - saved on server"),.26f,{192,233},ui::TextMuted);
        ui::fitMenuLabelWidth(scope,350,.26f,.22f); panel->addChild(scope,10);
        m_song=ui::menuLabel("",.34f,{192,195},ui::Text,ui::FontWeight::ExtraBold); panel->addChild(m_song,10);
        m_id=ui::menuLabel("",.27f,{192,171},ui::TextMuted); panel->addChild(m_id,10);
        m_slider=Slider::create(this,menu_selector(DebugAudioVolumePopup::onVolume),.95f);
        if (!m_slider) return false;
        m_slider->setPosition({192,136}); m_slider->setValue(1); m_slider->setID("ranked-admin-song-volume"_spr); panel->addChild(m_slider,20);
        m_percent=ui::menuLabel("",.45f,{192,106},ui::Gold,ui::FontWeight::ExtraBold); panel->addChild(m_percent,10);
        auto* menu=CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu,30);
        m_previous=button(menu,"<",{44,193},44,-1); m_next=button(menu,">",{340,193},44,1);
        m_reload=button(menu,i18n::text("admin-audio-reload","Reload"),{90,65},94,2);
        m_save=button(menu,i18n::text("admin-audio-save","Save for all players"),{249,65},200,0);
        m_status=ui::menuLabel("",.26f,{192,29},ui::TextMuted); panel->addChild(m_status,10);
        schedule(schedule_selector(DebugAudioVolumePopup::refresh),.1f);
        request(false); return true;
    }
    void status(std::string const& value) { m_status->setString(value.c_str()); ui::fitFullMenuLabel(m_status,352,32,.26f); }
    int selectedPercent() const {
        auto const value=m_slider->getValue();
        return std::isfinite(value) ? static_cast<int>(std::lround(std::clamp(value,0.0f,1.0f)*100)) : 100;
    }
    void updatePercent() { m_percent->setString(fmt::format("{}%",selectedPercent()).c_str()); }
    void controls() {
        auto const enabled=!m_busy && !m_songs.empty();
        m_reload->setEnabled(!m_busy);
        m_previous->setEnabled(enabled && m_songs.size()>1); m_next->setEnabled(enabled && m_songs.size()>1);
        m_save->setEnabled(enabled); m_slider->setVisible(enabled);
    }
    void showSong() {
        controls();
        if (m_songs.empty()) { m_song->setString(i18n::text("admin-audio-empty","No Ranked songs configured.").c_str()); m_id->setString(""); m_percent->setString(""); return; }
        auto const& song=m_songs[m_index]; std::string name;
        for (auto const& key:song.keys) {
            if (!name.empty()) name+=" / ";
            if (key=="menu") name+=i18n::text("admin-audio-menu","Lobby");
            else if (key=="menu_promotion") name+=i18n::text("admin-audio-promotion","Promotion lobby");
            else if (key=="menu_demotion_warning") name+=i18n::text("admin-audio-warning","Demotion warning lobby");
            else if (key=="match") name+=i18n::text("admin-audio-match","Round 1/2");
            else if (key=="round3_deathmatch_wait") name+=i18n::text("admin-audio-deathmatch","Round 3 / Death Match wait");
            else if (key=="result_win") name+=i18n::text("admin-audio-win","Win result");
            else if (key=="result_lose") name+=i18n::text("admin-audio-lose","Lose result");
            else name+=key;
        }
        m_song->setString(name.c_str()); ui::fitFullMenuLabel(m_song,262,36,.34f);
        m_id->setString(fmt::format("GD #{}  ({}/{})",song.id,m_index+1,m_songs.size()).c_str());
        m_slider->setValue(song.percent/100.f); updatePercent();
    }
    void onVolume(CCObject*) {
        if (m_closed || m_busy || m_songs.empty()) return;
        updatePercent(); status(i18n::text("admin-audio-unsaved","Press Save to apply to all players."));
    }
    void onSelect(CCObject* sender) {
        if (m_closed || m_busy) return;
        auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender); if (!item) return;
        if (item->getTag()==2) { request(false); return; }
        if (m_songs.empty()) return;
        if (item->getTag()==0) { request(true); return; }
        if (item->getTag()<0) m_index=(m_index+m_songs.size()-1)%m_songs.size();
        else m_index=(m_index+1)%m_songs.size();
        showSong(); status("");
    }
    void request(bool save) {
        if (m_closed || m_busy || !allowed() || m_session!=RankedRuntime::get().spectatorLiveSessionToken() || (save && m_songs.empty())) return;
        matjson::Value body; body["scope"]="COMMON";body["password"]=m_password; body["action"]=save ? "SET_AUDIO_VOLUME" : "GET_AUDIO_VOLUMES";
        auto const selected=m_songs.empty() ? 0 : m_songs[m_index].id;
        if (save) { body["songId"]=selected; body["volumePercent"]=selectedPercent(); }
        web::WebRequest http; http.header("Authorization","Bearer "+m_session); http.bodyJSON(body);
        http.followRedirects(false); http.timeout(std::chrono::seconds(20));
        m_busy=true; controls(); status(i18n::text("admin-working","Applying changes..."));
        auto const weak=WeakRef<DebugAudioVolumePopup>(this);
        m_request.spawn(http.post("https://corum-ranked.onrender.com/api/ranked/debug/admin"),[weak,selected,save](web::WebResponse response) {
            auto keep=weak.lock(); if (!keep || keep->m_closed || !keep->isRunning() || keep->m_session!=RankedRuntime::get().spectatorLiveSessionToken()) return;
            keep->m_busy=false; keep->controls();
            auto const root=response.json().unwrapOr(matjson::Value());
            if (response.code()<200 || response.code()>=300 || !root["ok"].asBool().unwrapOr(false)) {
                keep->status(root["message"].asString().unwrapOr(i18n::text("admin-audio-failed","Could not load/save song volumes."))); return;
            }
            auto const array=root["songs"].asArray();
            if (array.isErr()) { keep->status(i18n::text("admin-audio-failed","Could not load/save song volumes.")); return; }
            std::vector<Song> songs;
            for (auto const& item:array.unwrap()) {
                auto const id=item["songId"].asInt().unwrapOr(0),percent=item["volumePercent"].asInt().unwrapOr(-1);
                if (id<=0 || id>2147483647 || percent<0 || percent>100) continue;
                Song song; song.id=static_cast<int>(id); song.percent=static_cast<int>(percent);
                auto const keys=item["keys"].asArray(); if (keys.isOk()) for (auto const& key:keys.unwrap()) {
                    auto const text=key.asString().unwrapOr(""); if (!text.empty()) song.keys.push_back(text);
                }
                songs.push_back(std::move(song));
            }
            keep->m_songs=std::move(songs); keep->m_index=0;
            for (std::size_t i=0;i<keep->m_songs.size();++i) if (keep->m_songs[i].id==selected) keep->m_index=i;
            RankedRuntime::get().applyAudioVolumes(root["audioVolumes"]);
            keep->showSong(); keep->status(save ? i18n::text("admin-audio-saved","Saved. All players receive this within 5 seconds.") : "");
        });
    }
    void refresh(float) {
        if (!m_closed && (!allowed() || m_session!=RankedRuntime::get().spectatorLiveSessionToken() || RankedRuntime::get().view().stage!=RuntimeStage::Ready)) dismiss();
    }
public:
    static DebugAudioVolumePopup* create(std::string password) {
        auto* popup=new DebugAudioVolumePopup;
        if (popup->init(std::move(password))) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onClose(CCObject*) override { dismiss(); }
    void dismiss() {
        if (m_closed) return;
        m_closed=true;
        auto const keep=Ref<DebugAudioVolumePopup>(this);
        queueInMainThread([keep] { if (keep->isRunning()) keep->Popup::onClose(nullptr); });
    }
    void onExit() override {
        m_closed=true; m_request.cancel(); m_password.clear();
        unschedule(schedule_selector(DebugAudioVolumePopup::refresh));
        if (!m_exited) { m_exited=true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};

class DebugAdminPopup final : public Popup {
    TextInput *m_username=nullptr,*m_password=nullptr,*m_lp=nullptr,*m_mmr=nullptr;
    CCLabelBMFont* m_status=nullptr;
    CCMenu* m_actions=nullptr;
    std::optional<WeakRef<ui::RankedConfirmPopup>> m_confirm;
    int m_scope=0;
    CCMenu* m_scopes=nullptr;
    async::TaskHolder<web::WebResponse> m_request;
    std::string m_session;
    bool m_closed=false,m_exited=false,m_busy=false;
    static bool allowed() {
        auto* account=GJAccountManager::get();
        return account && account->m_accountID>0 && std::string(account->m_username)=="hwanhee1";
    }
    TextInput* field(CCNode* panel, char const* placeholder, CCPoint point, bool password=false) {
        auto* input=TextInput::create(162,placeholder); input->setPosition(point);
        input->setPasswordMode(password); input->setMaxCharCount(password?128:32);
        panel->addChild(input,20); return input;
    }
    bool init(RankedMode mode) {
        auto& runtime=RankedRuntime::get();
        if (!allowed() || runtime.view().stage!=RuntimeStage::Ready || runtime.spectatorLiveSessionToken().empty()) return false;
        m_scope=mode==RankedMode::Survival?1:0;
        if (!Popup::init(384,340)) return false;
        m_session=runtime.spectatorLiveSessionToken();
        m_bgSprite->setVisible(false); setID("ranked-debug-admin"_spr);
        auto* panel=ui::surface({384,340},{192,170},ui::Red,ui::Surface,255,true); m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel("Admin / hwanhee1",.48f,{192,318},ui::Red,ui::FontWeight::Black));
        m_scopes=CCMenu::create();m_scopes->setPosition(CCPointZero);panel->addChild(m_scopes,30);
        for(int i=0;i<3;i++) {
            auto* item=CCMenuItemSpriteExtra::create(scopeFace(i),this,menu_selector(DebugAdminPopup::onAction));
            item->setTag(20+i);item->setPosition({64+i*128.0f,280});m_scopes->addChild(item);
        }
        m_username=field(panel,"Player nickname",{99,239}); m_username->setString("hwanhee1");
        m_password=field(panel,"Debug password",{285,239},true);
        m_lp=field(panel,"LP",{99,197}); m_lp->setFilter("0123456789");
        m_mmr=field(panel,"MMR",{285,197}); m_mmr->setFilter("0123456789-");
        m_actions=CCMenu::create(); m_actions->setPosition(CCPointZero); panel->addChild(m_actions,30);
        constexpr char const* keys[]={"admin-unlock","admin-lp","admin-mmr","admin-both","admin-delete-bots","admin-history","admin-reset","admin-audio"};
        constexpr char const* labels[]={"Unlock all achievements","Set LP + tier","Set MMR","Set LP + MMR + tier","Delete all bots","Reset player data","Reset ranked database","Song volumes (all players)"};
        for (int i=0;i<8;++i) {
            auto const danger=i==4 || i==5 || i==6;
            auto* face=ui::surface({170,30},{},danger?ui::Red:ui::Blue,ui::SurfaceRaised,255,true);
            auto* caption=ui::menuLabel(i18n::text(keys[i],labels[i]),.32f,{85,15},danger?ui::Red:ui::Text,ui::FontWeight::ExtraBold);
            ui::fitMenuLabelWidth(caption,154,.32f,.24f); face->addChild(caption,5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(DebugAdminPopup::onAction));
            item->setTag(i); item->setPosition(CCPoint{99+(i%2)*186.0f,155-(i/2)*36.0f}); m_actions->addChild(item);
        }
        m_status=ui::menuLabel("",.24f,{192,20},ui::TextMuted); panel->addChild(m_status,10);
        schedule(schedule_selector(DebugAdminPopup::refresh),.1f); refreshScope();return true;
    }
    std::string scope() const {return m_scope==0?"SOLO_RANKED":m_scope==1?"SURVIVAL":"COMMON";}
    bool scopeAction(int action) const {
        if(m_scope==2)return action==0||action==5||action==6||action==7;
        if(m_scope==1)return action==1||action==5||action==6;
        return action>=1&&action<=6;
    }
    CCNode* scopeFace(int index) {
        auto const selected=m_scope==index;
        auto* face=ui::surface({120,28},{},selected?ui::Gold:ui::Blue,ui::SurfaceRaised,255,true);
        auto text=index==0?i18n::text("solo-ranked","SOLO RANKED"):index==1?i18n::text("survival","SURVIVAL"):i18n::text("admin-scope-common","COMMON");
        auto* caption=ui::menuLabel(text,.26f,{60,14},selected?ui::Gold:ui::Text,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(caption,108,.26f,.20f);face->addChild(caption,5);return face;
    }
    void refreshScope() {
        for(auto* object:CCArrayExt<CCObject*>(m_scopes->getChildren()))if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object)) {
            item->setNormalImage(scopeFace(item->getTag()-20));item->updateSprite();item->setEnabled(!m_busy);
        }
        m_lp->setVisible(m_scope!=2);m_mmr->setVisible(m_scope==0);
        enabled(!m_busy);
    }
    void status(std::string const& value) { m_status->setString(value.c_str()); ui::fitFullMenuLabel(m_status,352,28,.24f); }
    void enabled(bool value) {
        for (auto* object : CCArrayExt<CCObject*>(m_actions->getChildren()))
            if (auto* button=typeinfo_cast<CCMenuItem*>(object)) button->setEnabled(value && scopeAction(button->getTag()));
    }
    static std::optional<int> number(TextInput* input, bool allowNegative=false) {
        auto const value=input->getString(); int result=0;
        auto const parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        if (value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || (!allowNegative && result<0)) return std::nullopt;
        return result;
    }
    void onAction(CCObject* sender) {
        if (m_closed || m_busy || m_confirm) return;
        auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender); if (!item) return;
        auto const choice=item->getTag();
        if(choice>=20&&choice<=22){m_scope=choice-20;refreshScope();status("");return;}
        if(!scopeAction(choice))return;
        if (choice==7) {
            auto const password=std::string(m_password->getString());
            if (password.empty()) { status(i18n::text("admin-password-needed","Enter the debug password.")); return; }
            m_closed=true; auto const keep=Ref<DebugAdminPopup>(this);
            queueInMainThread([keep,password] {
                if (!keep->isRunning()) return;
                keep->Popup::onClose(nullptr);
                if (auto* popup=DebugAudioVolumePopup::create(password)) popup->show();
            });
            return;
        }
        if (choice==6) {
            auto const weak=WeakRef<DebugAdminPopup>(this);
            auto* alert=ui::RankedConfirmPopup::create(scope()+" · "+i18n::text("admin-reset","RESET DATABASE"),
                i18n::text("admin-reset-scope","Reset the selected scope? Other mode data and accounts are retained."),[weak](bool confirmed) {
                    if(auto owner=weak.lock()){owner->m_confirm.reset();if(confirmed&&!owner->m_closed)owner->execute(6,true);}
                });
            if(alert){m_confirm.emplace(alert);alert->show();}return;
        }
        execute(choice,false);
    }
    void execute(int choice, bool confirmed) {
        if (m_closed || m_busy || !allowed() || m_session!=RankedRuntime::get().spectatorLiveSessionToken()) return;
        constexpr char const* actions[]={"UNLOCK_ALL","SET_LP","SET_MMR","SET_BOTH","DELETE_BOTS","RESET_HISTORY","RESET_DB"};
        if (choice<0 || choice>6 || !scopeAction(choice) || (choice==6 && !confirmed)) return;
        // Android gd::string is a distinct ABI type. Normalize before JSON;
        // assigning it directly would require two implicit conversions.
        matjson::Value body;body["scope"]=scope();body["action"]=actions[choice]; body["password"]=std::string(m_password->getString());
        if (m_password->getString().empty()) { status(i18n::text("admin-password-needed","Enter the debug password.")); return; }
        if (choice!=4 && choice!=6) {
            if (m_username->getString().empty()) { status(i18n::text("admin-player-needed","Enter a player nickname.")); return; }
            body["username"]=std::string(m_username->getString());
        }
        if (choice==1 || choice==3) { auto value=number(m_lp); if (!value) { status("Enter valid LP (0 - 2147483647)."); return; } body["lp"]=*value; }
        if (choice==2 || choice==3) { auto value=number(m_mmr,true); if (!value) { status("Enter valid MMR (-2147483648 - 2147483647)."); return; } body["mmr"]=*value; }
        if (confirmed) body["confirmation"]="RESET";
        web::WebRequest http; http.header("Authorization","Bearer "+m_session); http.bodyJSON(body);
        http.followRedirects(false); http.timeout(std::chrono::seconds(20));
        m_busy=true; enabled(false); status(i18n::text("admin-working","Applying changes..."));
        auto const weak=WeakRef<DebugAdminPopup>(this);
        m_request.spawn(http.post("https://corum-ranked.onrender.com/api/ranked/debug/admin"),[weak](web::WebResponse response) {
            auto keep=weak.lock(); if (!keep || keep->m_closed || !keep->isRunning()) return;
            keep->m_busy=false; keep->enabled(true);
            if (keep->m_session!=RankedRuntime::get().spectatorLiveSessionToken()) return;
            auto const root=response.json().unwrapOr(matjson::Value());
            if (response.code()<200 || response.code()>=300) { keep->status(root["message"].asString().unwrapOr("Admin request failed.")); return; }
            auto const message=root["message"].asString().unwrapOr("Changes saved.");
            // Retire outside this TaskHolder's callback and native input dispatch.
            keep->m_closed=true;
            queueInMainThread([keep,message,root] {
                if (!keep->isRunning()) return;
                keep->Popup::onClose(nullptr);
                FLAlertLayer::create("Admin",message,"OK")->show();
                // One summary for a batch unlock; ordinary match notices remain
                // untouched. Reload projections/ranks/history from the server.
                RankedRuntime::get().applyAdminProjection(root,keep->scope());
            });
        });
    }
    void refresh(float) {
        if (!m_closed && (!allowed() || m_session!=RankedRuntime::get().spectatorLiveSessionToken() || RankedRuntime::get().view().stage!=RuntimeStage::Ready)) dismiss();
    }
    void closeConfirm() {
        if(m_confirm) { if(auto alert=m_confirm->lock()) { alert->cancel(); } }
        m_confirm.reset();
    }

public:
    static DebugAdminPopup* create(RankedMode mode=RankedMode::SoloRanked) {
        auto* popup=new DebugAdminPopup;
        if (popup->init(mode)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onClose(CCObject*) override { dismiss(); }
    void dismiss() {
        if (m_closed) return;
        m_closed=true; closeConfirm();
        auto const keep=Ref<DebugAdminPopup>(this);
        queueInMainThread([keep] { if (keep->isRunning()) keep->Popup::onClose(nullptr); });
    }
    void onExit() override {
        m_closed=true; m_request.cancel(); closeConfirm(); m_password->setString("");
        unschedule(schedule_selector(DebugAdminPopup::refresh));
        if (!m_exited) { m_exited=true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};
}
void showDebugAdminPopup(RankedMode mode) { if (auto* popup=DebugAdminPopup::create(mode)) popup->show(); }
}
#endif
