#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <array>
#include <Geode/utils/web.hpp>

namespace corum::ranked::ui {
class RankedTierGuidePopup final : public geode::Popup {
public:
    static RankedTierGuidePopup* create();
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;
private:
    bool init();
    void fetch();
    void render();
    void onRefresh(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject*);
    cocos2d::CCNode* m_panel=nullptr;
    cocos2d::CCNode* m_contents=nullptr;
    cocos2d::CCLabelBMFont* m_caption=nullptr;
    CCMenuItemSpriteExtra* m_refresh=nullptr;
    std::array<CCMenuItemSpriteExtra*,2> m_modes{};
    matjson::Value m_solo;
    bool m_survival=false,m_failed=false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation=0;
    bool m_closed=false,m_busy=false;
};
} // namespace corum::ranked::ui
