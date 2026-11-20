#pragma once
#include "../domain/PatchNotes.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/web.hpp>
#include <array>

namespace corum::ranked::ui {
// Patch notes from the server (newest first) in Korean or English; the
// release named by `highlight` (the update being installed) is marked.
class RankedPatchNotesPopup final : public geode::Popup {
public:
    static RankedPatchNotesPopup* create(std::string highlight = {});
    void onClose(cocos2d::CCObject*) override;
    void onExit() override;
private:
    bool init() override;
    void fetch();
    void render();
    void onLanguage(cocos2d::CCObject*);
    std::string m_highlight;
    cocos2d::CCNode* m_panel=nullptr;
    cocos2d::CCNode* m_contents=nullptr;
    std::array<CCMenuItemSpriteExtra*,2> m_languages{};
    std::vector<PatchNoteView> m_notes;
    bool m_korean=false,m_loaded=false,m_failed=false,m_closed=false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::uint64_t m_generation=0;
};
} // namespace corum::ranked::ui
