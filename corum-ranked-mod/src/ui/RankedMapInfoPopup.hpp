#pragma once

#include "../domain/MapPoolPresentation.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>

namespace corum::ranked::ui {

// One Solo map's statistics over the map pool page. VIEW LEVEL opens the
// native GD level page exactly like the history VIEW LEVEL button.
class RankedMapInfoPopup final : public geode::Popup {
public:
    static RankedMapInfoPopup* create(MapPoolRow row);
    void dismiss();
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;

private:
    bool init() override;
    void fetch();
    void showMessage(std::string const& text, cocos2d::ccColor3B color);
    void showDetail(matjson::Value const& data);
    void addChrome();
    void onRefresh(cocos2d::CCObject*);
    void onViewLevel(cocos2d::CCObject*);

    MapPoolRow m_row;
    cocos2d::CCNode* m_panel = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation = 0;
    bool m_busy = false, m_closed = false, m_exited = false;
};

} // namespace corum::ranked::ui
