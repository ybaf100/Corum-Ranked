#pragma once

#include "../domain/MapPoolPresentation.hpp"
#include "RankedHistoryFilterPopup.hpp"
#include "RankedMapInfoPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>
#include <functional>

namespace corum::ranked::ui {
class MapPoolTabStrip;

// Body of the Solo map pool page (under the lobby's screen header): summary,
// scrolling pool tabs, an order picker, the map list and REFRESH. It loads
// and redraws itself, so the lobby renders the page once.
class RankedMapPoolView final : public cocos2d::CCNode {
public:
    static RankedMapPoolView* create(cocos2d::CCSize screen);
    // Info and order popups belong to the page: close them with it.
    void closePopups();
    void cleanup() override;
    void update(float dt) override;

private:
    struct RowHit { cocos2d::CCNode* row = nullptr; cocos2d::CCNode* info = nullptr; cocos2d::CCNode* view = nullptr; std::size_t index = 0; };
    bool init(cocos2d::CCSize screen);
    void fetch();
    void showError();
    void updateSummary();
    void updateControls();
    void rebuildRows();
    void selectPool(int pool);
    void openInfo(std::size_t index);
    void viewLevel(std::size_t index);
    void onSort(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);

    cocos2d::CCSize m_screen;
    float m_left = 0, m_width = 0;
    cocos2d::CCMenu* m_menu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    MapPoolTabStrip* m_tabs = nullptr;
    cocos2d::CCNode* m_summary = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCLabelBMFont* m_updated = nullptr;
    CCMenuItemSpriteExtra* m_sortButton = nullptr;
    CCMenuItemSpriteExtra* m_refresh = nullptr;
    std::vector<cocos2d::CCLabelBMFont*> m_headers;
    std::vector<MapPoolRow> m_rows;
    std::vector<RowHit> m_hits;
    int m_totalRounds = 0;
    std::optional<double> m_totalDrawRate;
    std::string m_mostPlayed, m_mostBanned;
    int m_pool = 0;
    MapPoolSort m_sort = MapPoolSort::Difficulty;
    std::chrono::steady_clock::time_point m_receivedAt{};
    bool m_hasList = false, m_busy = false, m_closed = false;
    int m_shownMinutes = -1;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation = 0;
    std::optional<geode::WeakRef<RankedMapInfoPopup>> m_info;
    std::optional<geode::WeakRef<RankedHistoryFilterPopup>> m_order;
};

} // namespace corum::ranked::ui
