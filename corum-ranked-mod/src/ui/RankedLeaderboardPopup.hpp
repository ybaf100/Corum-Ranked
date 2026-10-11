#pragma once

#include "../domain/LeaderboardPresentation.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>

namespace corum::ranked::ui {
class PlayerIcon;

class RankedLeaderboardPopup final : public geode::Popup {
public:
    static RankedLeaderboardPopup* create();
    void dismiss();
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;
    void update(float dt) override;

private:
    struct Row {
        cocos2d::CCNode* node = nullptr;
        PlayerIcon* icon = nullptr;
        std::vector<cocos2d::CCSprite*> trail;
        cocos2d::CCNode* shine = nullptr;
        LeaderboardEntry entry;
    };
    bool init();
    void fetch(int offset);
    void showRows(matjson::Value const& data);
    void showError();
    void onPrevious(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void updateButtons();
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_previous = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    std::vector<Row> m_rows;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation = 0;
    int m_offset = 0;
    bool m_hasMore = false, m_busy = false, m_closed = false, m_exited = false;
    double m_elapsed = 0;
};
} // namespace corum::ranked::ui
