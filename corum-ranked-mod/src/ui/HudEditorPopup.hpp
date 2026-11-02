#pragma once

#include "../domain/HudLayout.hpp"
#include <Geode/ui/Popup.hpp>
#include <map>
#include <string>
#include <vector>

namespace corum::ranked::ui {

class HudEditorInput;

// Full-window in-game HUD editor opened from the Solo/Survival lobby. It
// previews the real HUD labels (same fonts, scales and authored coordinates)
// over a static background, and edits placements of the saved layout.
class HudEditorPopup final : public geode::Popup {
public:
    static HudEditorPopup* create(HudMode mode);

    // Touch routing from HudEditorInput (window coordinates).
    bool beginTouch(cocos2d::CCPoint location);
    void moveTouch(cocos2d::CCPoint location);
    void endTouch();

private:
    struct ToolbarItem {
        CCMenuItemSpriteExtra* item = nullptr;
        cocos2d::CCPoint offset;
    };
    // One row of the element list: select button and visibility toggle.
    struct ListRow {
        std::string id;
        CCMenuItemSpriteExtra* select = nullptr;
        CCMenuItemSpriteExtra* toggle = nullptr;
        cocos2d::CCLabelBMFont* name = nullptr;
        cocos2d::CCLabelBMFont* state = nullptr;
        cocos2d::CCLabelBMFont* toggleCaption = nullptr;
    };
    enum class Drag { None, Element, Toolbar };

    bool init(HudMode mode);
    void onExit() override;
    void keyBackClicked() override;

    cocos2d::CCNodeRGBA* group(std::string_view id);
    void buildBackground();
    void buildSoloPreview();
    void buildSurvivalPreview();
    void buildToolbar();
    CCMenuItemSpriteExtra* toolbarButton(std::string const& text, cocos2d::CCSize size, cocos2d::CCPoint offset,
        cocos2d::SEL_MenuHandler selector, cocos2d::ccColor3B accent, cocos2d::CCLabelBMFont** caption = nullptr, int tag = 0);
    CCMenuItemSpriteExtra* arrowButton(float rotation, cocos2d::CCPoint offset, int tag);
    cocos2d::CCLabelBMFont* toolbarCaption(std::string const& text, cocos2d::CCPoint offset, float width,
        cocos2d::ccColor3B color, float scale = 0.2f);
    void placeToolbar(cocos2d::CCPoint center);
    void buildList();
    void setListOpen(bool open);
    void refreshList();

    HudPlacement placement(std::string const& id) const;
    void storePlacement(std::string const& id, HudPlacement const& value);
    HudTransform transform(std::string const& id) const;
    cocos2d::CCRect elementBox(std::string const& id) const;
    void moveSelected(float dx, float dy);
    void keepInside(std::string const& id);
    void refresh();

    void onPrevious(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onToggleVisible(cocos2d::CCObject*);
    void onSmaller(cocos2d::CCObject*);
    void onLarger(cocos2d::CCObject*);
    void onMoreTransparent(cocos2d::CCObject*);
    void onLessTransparent(cocos2d::CCObject*);
    void onNudge(cocos2d::CCObject*);
    void onGrid(cocos2d::CCObject*);
    void onList(cocos2d::CCObject*);
    void onListSelect(cocos2d::CCObject*);
    void onListToggle(cocos2d::CCObject*);
    void onPreset(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onCollapse(cocos2d::CCObject*);
    void onExpand(cocos2d::CCObject*);
    void setToolsCollapsed(bool collapsed);
    // Tools fade while an element is dragged; the grid shows only then.
    void setDragging(bool dragging);

    HudMode m_mode = HudMode::Solo;
    HudLayoutConfig m_saved;
    HudPlacements m_working;
    cocos2d::CCSize m_size;
    cocos2d::CCNode* m_canvas = nullptr;
    cocos2d::CCDrawNode* m_outlines = nullptr;
    cocos2d::CCDrawNode* m_gridLines = nullptr;
    CCMenuItemSpriteExtra* m_expandButton = nullptr;
    bool m_toolsCollapsed = false;
    cocos2d::CCLabelBMFont* m_tag = nullptr;
    std::map<std::string, cocos2d::CCNodeRGBA*, std::less<>> m_groups;
    std::string m_selected;
    float m_grid = HudSnapGrid;

    cocos2d::CCNode* m_toolbar = nullptr;
    cocos2d::CCSize m_toolbarSize;
    std::vector<ToolbarItem> m_toolbarItems;
    CCMenuItemSpriteExtra* m_visibleButton = nullptr;
    CCMenuItemSpriteExtra* m_smallerButton = nullptr;
    CCMenuItemSpriteExtra* m_largerButton = nullptr;
    CCMenuItemSpriteExtra* m_transparentButton = nullptr;
    CCMenuItemSpriteExtra* m_opaqueButton = nullptr;
    CCMenuItemSpriteExtra* m_gridButton = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_nudgeButtons;
    cocos2d::CCLabelBMFont* m_elementName = nullptr;
    cocos2d::CCLabelBMFont* m_visibleCaption = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_transparencyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_gridCaption = nullptr;
    cocos2d::CCLabelBMFont* m_presetLabel = nullptr;
    cocos2d::CCLabelBMFont* m_indexLabel = nullptr;

    cocos2d::CCNode* m_list = nullptr;
    std::vector<ListRow> m_listRows;
    std::vector<CCMenuItemSpriteExtra*> m_listItems;
    bool m_listOpen = false;

    Drag m_drag = Drag::None;
    cocos2d::CCPoint m_dragStart;
    cocos2d::CCPoint m_dragOrigin;
    cocos2d::CCRect m_dragBox;
    float m_dragScale = 1.0f;
    bool m_closed = false;
};

} // namespace corum::ranked::ui
