#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
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
    void showRows(matjson::Value const& data);
    void onRefresh(cocos2d::CCObject*);
    geode::ScrollLayer* m_scroll=nullptr;
    cocos2d::CCLabelBMFont* m_caption=nullptr;
    CCMenuItemSpriteExtra* m_refresh=nullptr;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation=0;
    bool m_closed=false,m_busy=false;
};
} // namespace corum::ranked::ui
