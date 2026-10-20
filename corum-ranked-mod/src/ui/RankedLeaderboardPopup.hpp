#pragma once

#include "../domain/LeaderboardPresentation.hpp"
#include "../domain/RankedMode.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

namespace corum::ranked::ui {
class PlayerIcon;

class RankedLeaderboardPopup final : public geode::Popup {
public:
    static RankedLeaderboardPopup* create(RankedMode mode = RankedMode::SoloRanked);
    void dismiss();
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;
    void update(float dt) override;

private:
    struct Row {
        cocos2d::CCNode* node = nullptr;
        PlayerIcon* icon = nullptr;
        cocos2d::CCDrawNode* shine = nullptr;
        LeaderboardEntry entry;
        CCMenuItemSpriteExtra* action=nullptr;
    };
    bool init() override;
    void fetch(int offset);
    void showRows(matjson::Value const& data);
    void showError();
    void onPrevious(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void updateButtons();
    void onMode(cocos2d::CCObject*);
    void onSearch(cocos2d::CCObject*);
    void onPlayerInfo(cocos2d::CCObject*);
    RankedMode m_mode = RankedMode::SoloRanked;
    CCMenuItemSpriteExtra* m_survivalTab = nullptr;
    CCMenuItemSpriteExtra* m_soloTab = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_previous = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    CCMenuItemSpriteExtra* m_refresh = nullptr;
    CCMenuItemSpriteExtra* m_searchButton = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    std::string m_searchQuery;
    int m_searchAccount = 0;
    std::vector<Row> m_rows;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation = 0;
    int m_offset = 0;
    bool m_hasMore = false, m_busy = false, m_closed = false, m_exited = false;
    double m_elapsed = 0;
};
} // namespace corum::ranked::ui
