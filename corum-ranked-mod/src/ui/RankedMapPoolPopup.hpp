#pragma once

#include "../domain/MapPoolPresentation.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>

namespace corum::ranked::ui {

// Solo map pool: every active map with pool-wide statistics (list view) and
// one map's statistics (info view). "View level" opens the native GD level
// page exactly like the history VIEW LEVEL button.
class RankedMapPoolPopup final : public geode::Popup {
public:
    static RankedMapPoolPopup* create();
    void dismiss();
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;
    void update(float dt) override;

private:
    struct RowHit { cocos2d::CCNode* row = nullptr; cocos2d::CCNode* info = nullptr; cocos2d::CCNode* view = nullptr; std::size_t index = 0; };
    bool init() override;
    void buildListChrome();
    void fetchList();
    void showList();
    void showListError();
    void rebuildRows();
    void updateControls();
    void updateSummary();
    void openInfo(std::size_t index);
    void fetchDetail();
    void showDetail(matjson::Value const& data);
    void showDetailMessage(std::string const& text, cocos2d::ccColor3B color);
    void backToList();
    void viewLevel(std::size_t index);
    void onPool(cocos2d::CCObject*);
    void onSort(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);
    void onViewLevel(cocos2d::CCObject*);

    cocos2d::CCNode* m_panel = nullptr;
    cocos2d::CCNode* m_listView = nullptr;
    cocos2d::CCNode* m_infoView = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCLabelBMFont* m_updated = nullptr;
    CCMenuItemSpriteExtra* m_refresh = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_poolTabs, m_sortTabs;
    cocos2d::CCNode* m_summary = nullptr;
    std::vector<MapPoolRow> m_rows;
    std::vector<RowHit> m_hits;
    int m_totalRounds = 0;
    std::optional<double> m_totalDrawRate;
    std::string m_mostPlayed, m_mostBanned;
    int m_pool = 0;
    MapPoolSort m_sort = MapPoolSort::Difficulty;
    std::optional<std::size_t> m_detailIndex;
    std::string m_detailId;
    std::chrono::steady_clock::time_point m_receivedAt{};
    bool m_hasList = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation = 0;
    bool m_busy = false, m_closed = false, m_exited = false;
    int m_shownMinutes = -1;
};

} // namespace corum::ranked::ui
