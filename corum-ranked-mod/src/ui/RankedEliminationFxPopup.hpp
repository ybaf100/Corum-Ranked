#pragma once

#include "../domain/SurvivalEliminationEffects.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>
#include <array>
#include <utility>
#include <vector>

namespace corum::ranked::ui {

// Survival elimination effect picker (Survival start card). The list shows
// the five effects with their unlock requirement; the selected one (locked
// ones too) loops on a preview stage, and unlocked ones can be equipped.
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
    void buildStage();
    void playPreview();
    void showMessage(std::string const& text, cocos2d::ccColor3B color);
    void onSelect(cocos2d::CCObject* sender);
    void onEquip(cocos2d::CCObject*);
    void onReplay(cocos2d::CCObject*);
    void onRetry(cocos2d::CCObject*);
    void previewTick(float);
    void executeVictim(std::size_t index);

    cocos2d::CCNode* m_panel = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_stage = nullptr;
    cocos2d::CCNode* m_world = nullptr;  // clipped preview contents (rebuilt per replay)
    cocos2d::CCPoint m_executorAt{};     // executor icon centre in m_world space
    cocos2d::CCNode* m_executorIcon = nullptr; // the gold card's icon (hidden while it flies)
    std::vector<std::pair<cocos2d::CCNode*, cocos2d::CCNode*>> m_victims; // card, icon slot
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::array<bool, 5> m_unlocked{true, false, false, false, false};
    EliminationEffect m_equipped = EliminationEffect::Default;
    EliminationEffect m_selected = EliminationEffect::Default;
    std::uint64_t m_generation = 0;
    bool m_loaded = false, m_busy = false, m_closed = false, m_exited = false;
};

} // namespace corum::ranked::ui
