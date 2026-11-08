#pragma once

#include "../domain/SurvivalEliminationEffects.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>
#include <array>

namespace corum::ranked::ui {

// Survival elimination effect picker (Survival start card). Lists the five
// effects with their unlock requirement; unlocked ones can be equipped.
class RankedEliminationFxPopup final : public geode::Popup {
public:
    static RankedEliminationFxPopup* create();
    void dismiss();
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;

private:
    bool init() override;
    void fetch();
    void equip(EliminationEffect effect);
    void apply(matjson::Value const& data);
    void render();
    void showMessage(std::string const& text, cocos2d::ccColor3B color);
    void onEquip(cocos2d::CCObject* sender);
    void onRetry(cocos2d::CCObject*);

    cocos2d::CCNode* m_panel = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCMenu* m_rows = nullptr;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::array<bool, 5> m_unlocked{true, false, false, false, false};
    EliminationEffect m_equipped = EliminationEffect::Default;
    std::uint64_t m_generation = 0;
    bool m_loaded = false, m_busy = false, m_closed = false, m_exited = false;
};

} // namespace corum::ranked::ui
