#include "DebugMenuPopup.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../RankedRuntime.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedTranslations.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/FLAlertLayerProtocol.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <charconv>

using namespace geode::prelude;
namespace corum::ranked::debug {
namespace {
class DebugAdminPopup final : public Popup, public FLAlertLayerProtocol {
    TextInput *m_username=nullptr,*m_password=nullptr,*m_lp=nullptr,*m_mmr=nullptr;
    CCLabelBMFont* m_status=nullptr;
    CCMenu* m_actions=nullptr;
    std::optional<WeakRef<FLAlertLayer>> m_confirm;
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
    bool init() override {
        auto& runtime=RankedRuntime::get();
        if (!allowed() || runtime.view().stage!=RuntimeStage::Ready || runtime.spectatorLiveSessionToken().empty()) return false;
        if (!Popup::init(384,296)) return false;
        m_session=runtime.spectatorLiveSessionToken();
        m_bgSprite->setVisible(false); setID("ranked-debug-admin"_spr);
        auto* panel=ui::surface({384,296},{192,148},ui::Red,ui::Surface,255,true); m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel("Admin / hwanhee1",.48f,{192,275},ui::Red,ui::FontWeight::Black));
        m_username=field(panel,"Player nickname",{99,238}); m_username->setString("hwanhee1");
        m_password=field(panel,"Debug password",{285,238},true);
        m_lp=field(panel,"LP",{99,196}); m_lp->setFilter("0123456789");
        m_mmr=field(panel,"MMR",{285,196}); m_mmr->setFilter("0123456789-");
        m_actions=CCMenu::create(); m_actions->setPosition(CCPointZero); panel->addChild(m_actions,30);
        constexpr char const* keys[]={"admin-unlock","admin-lp","admin-mmr","admin-both","admin-delete-bots","admin-history","admin-reset"};
        constexpr char const* labels[]={"Unlock all achievements","Set LP + tier","Set MMR","Set LP + MMR + tier","Delete all bots","Reset player history","Reset ranked database"};
        for (int i=0;i<7;++i) {
            auto const danger=i==4 || i==5 || i==6;
            auto* face=ui::surface({170,30},{},danger?ui::Red:ui::Blue,ui::SurfaceRaised,255,true);
            auto* caption=ui::menuLabel(i18n::text(keys[i],labels[i]),.32f,{85,15},danger?ui::Red:ui::Text,ui::FontWeight::ExtraBold);
            ui::fitMenuLabelWidth(caption,154,.32f,.24f); face->addChild(caption,5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(DebugAdminPopup::onAction));
            item->setTag(i); item->setPosition(i==6?CCPoint{192,48}:CCPoint{99+(i%2)*186.0f,155-(i/2)*36.0f}); m_actions->addChild(item);
        }
        m_status=ui::menuLabel("",.24f,{192,20},ui::TextMuted); panel->addChild(m_status,10);
        schedule(schedule_selector(DebugAdminPopup::refresh),.1f); return true;
    }
    void status(std::string const& value) { m_status->setString(value.c_str()); ui::fitFullMenuLabel(m_status,352,28,.24f); }
    void enabled(bool value) {
        for (auto* object : CCArrayExt<CCObject*>(m_actions->getChildren()))
            if (auto* button=typeinfo_cast<CCMenuItem*>(object)) button->setEnabled(value);
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
        if (choice==6) {
            auto* alert=FLAlertLayer::create(this,"RESET RANKED DATABASE",
                "Delete all Ranked players, sessions, achievements and matches? Operational configuration is retained.","Cancel","RESET",390);
            m_confirm.emplace(alert); alert->show(); return;
        }
        execute(choice,false);
    }
    void execute(int choice, bool confirmed) {
        if (m_closed || m_busy || !allowed() || m_session!=RankedRuntime::get().spectatorLiveSessionToken()) return;
        constexpr char const* actions[]={"UNLOCK_ALL","SET_LP","SET_MMR","SET_BOTH","DELETE_BOTS","RESET_HISTORY","RESET_DB"};
        if (choice<0 || choice>6 || (choice==6 && !confirmed)) return;
        matjson::Value body; body["action"]=actions[choice]; body["password"]=m_password->getString();
        if (m_password->getString().empty()) { status(i18n::text("admin-password-needed","Enter the debug password.")); return; }
        if (choice!=4 && choice!=6) {
            if (m_username->getString().empty()) { status(i18n::text("admin-player-needed","Enter a player nickname.")); return; }
            body["username"]=m_username->getString();
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
            queueInMainThread([keep,message] {
                if (!keep->isRunning()) return;
                keep->Popup::onClose(nullptr);
                FLAlertLayer::create("Admin",message,"OK")->show();
                // One summary for a batch unlock; ordinary match notices remain
                // untouched. Reload projections/ranks/history from the server.
                RankedRuntime::get().begin();
            });
        });
    }
    void refresh(float) {
        if (!m_closed && (!allowed() || m_session!=RankedRuntime::get().spectatorLiveSessionToken() || RankedRuntime::get().view().stage!=RuntimeStage::Ready)) dismiss();
    }
    void closeConfirm() {
        if (m_confirm) if (auto alert=m_confirm->lock(); alert && alert->getParent()) {
            alert->m_alertProtocol=nullptr; alert->onBtn1(nullptr); CCTouchDispatcher::get()->unregisterForcePrio(alert.data());
        }
        m_confirm.reset();
    }
public:
    static DebugAdminPopup* create() {
        auto* popup=new DebugAdminPopup;
        if (popup->init()) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void FLAlert_Clicked(FLAlertLayer*,bool confirmed) override { m_confirm.reset(); if (confirmed) execute(6,true); }
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
void showDebugAdminPopup() { if (auto* popup=DebugAdminPopup::create()) popup->show(); }
}
#endif
