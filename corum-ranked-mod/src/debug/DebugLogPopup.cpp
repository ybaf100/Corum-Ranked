#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "DebugLogUploader.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedTranslations.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <algorithm>
#include <chrono>
#include <set>

using namespace geode::prelude;
namespace corum::ranked {
namespace {

// "Send logs" from the Debug main menu: game logs on the left, crash logs on
// the right, styled like the Solo Ranked panels. Up to six files per upload.
class DebugLogPopup final : public Popup {
    static constexpr float W = 420.0f, H = 280.0f, ColumnW = 196.0f, RowW = 182.0f, RowH = 21.0f;
    std::vector<DebugLogFile> m_files;
    std::set<std::size_t> m_selected;
    std::vector<CCMenuItemSpriteExtra*> m_rows;
    CCMenu* m_menu = nullptr;
    CCMenuItemSpriteExtra* m_send = nullptr;
    CCLabelBMFont *m_status = nullptr, *m_count = nullptr;
    bool m_exited = false;

    static std::string displayName(DebugLogFile const& file) {
        auto name = file.path.stem().string();
        if (name.rfind("Geode_", 0) == 0) name.erase(0, 6);
        if (name.size() > 5 && name[4] == '-') name.erase(0, 5); // drop the year
        std::replace(name.begin(), name.end(), '_', ' ');
        return name;
    }
    static std::string sizeText(std::uintmax_t bytes) {
        return bytes >= 1'000'000 ? fmt::format("{:.1f}MB", static_cast<double>(bytes) / 1'000'000.0)
            : fmt::format("{}KB", std::max<std::uintmax_t>(1, (bytes + 999) / 1000));
    }

    CCNode* rowFace(std::size_t index) {
        auto const& file = m_files[index];
        auto const selected = m_selected.contains(index);
        auto const full = !selected && m_selected.size() >= DebugLogMaxFiles;
        auto const accent = file.crash ? ui::Red : ui::Cyan;
        auto* face = ui::surface({RowW, RowH}, {}, selected ? accent : ui::Blue, selected ? ui::SurfaceSoft : ui::SurfaceRaised,
            full ? 150 : 250, selected);
        // Check box.
        auto* box = ui::surface({11, 11}, {10, RowH / 2.0f}, selected ? accent : ui::TextMuted, ui::Ink, 255, selected);
        face->addChild(box, 4);
        if (selected) {
            auto* tick = CCLayerColor::create({accent.r, accent.g, accent.b, 255}, 5, 5);
            tick->setPosition({7.5f, RowH / 2.0f - 2.5f});face->addChild(tick, 6);
        }
        auto* name = ui::menuLabel(displayName(file), .24f, {21, RowH / 2.0f}, full ? ui::TextMuted : ui::Text, ui::FontWeight::ExtraBold);
        name->setAnchorPoint({0.0f, 0.5f});ui::fitMenuLabelWidth(name, file.current ? 92.0f : 118.0f, .24f, .15f);face->addChild(name, 5);
        if (file.current) {
            auto* now = ui::menuLabel(i18n::text("debug-logs-current", "NOW"), .18f, {124, RowH / 2.0f}, ui::Gold, ui::FontWeight::Black);
            ui::fitMenuLabelWidth(now, 26.0f, .18f, .12f);face->addChild(now, 5);
        }
        auto* size = ui::menuLabel(sizeText(file.bytes), .18f, {RowW - 6.0f, RowH / 2.0f}, ui::TextMuted, ui::FontWeight::SemiBold);
        size->setAnchorPoint({1.0f, 0.5f});face->addChild(size, 5);
        return face;
    }

    void refreshRows() {
        auto const busy = DebugLogUploader::get().busy();
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (!m_rows[i]) continue;
            m_rows[i]->setNormalImage(rowFace(i));m_rows[i]->updateSprite();
            m_rows[i]->setEnabled(!busy);
        }
        m_count->setString(fmt::format(fmt::runtime(i18n::text("debug-logs-selected", "{} / {} selected")), m_selected.size(), DebugLogMaxFiles).c_str());
        ui::fitMenuLabelWidth(m_count, 120.0f, .24f, .16f);
        sendFace();
    }

    void sendFace() {
        auto const busy = DebugLogUploader::get().busy();
        auto const ready = !busy && !m_selected.empty();
        auto* face = ui::surface({132, 30}, {}, ready ? ui::Gold : ui::TextMuted, ui::SurfaceRaised, ready ? 255 : 150, ready);
        auto const text = busy ? i18n::text("debug-logs-busy", "SENDING...")
            : fmt::format(fmt::runtime(i18n::text("debug-logs-send", "SEND {}")), m_selected.size());
        auto* caption = ui::menuLabel(text, .30f, {66, 15}, ready ? ui::Gold : ui::TextMuted, ui::FontWeight::Black);
        ui::fitMenuLabelWidth(caption, 120.0f, .30f, .2f);face->addChild(caption, 5);
        m_send->setNormalImage(face);m_send->updateSprite();m_send->setEnabled(ready);
    }

    CCMenuItemSpriteExtra* button(CCNode* face, SEL_MenuHandler selector, CCPoint at, int tag = 0) {
        auto* item = CCMenuItemSpriteExtra::create(face, this, selector);
        item->setTag(tag);item->setPosition(at);m_menu->addChild(item);return item;
    }

    void column(CCNode* panel, bool crash, float x) {
        auto const accent = crash ? ui::Red : ui::Cyan;
        auto* box = ui::surface({ColumnW, 168}, {x, 134}, accent, ui::Surface, 245, false);
        panel->addChild(box, 2);
        auto* heading = ui::menuLabel(crash ? i18n::text("debug-logs-crash", "CRASH LOGS") : i18n::text("debug-logs-game", "GAME LOGS"),
            .30f, {x, 207}, accent, ui::FontWeight::Black);
        ui::fitMenuLabelWidth(heading, ColumnW - 16.0f, .30f, .2f);panel->addChild(heading, 6);
        float y = 186.0f;
        bool any = false;
        for (std::size_t i = 0; i < m_files.size(); ++i) {
            if (m_files[i].crash != crash || y < 60.0f) continue;
            m_rows.resize(std::max(m_rows.size(), i + 1));
            m_rows[i] = button(rowFace(i), menu_selector(DebugLogPopup::onRow), {x, y}, static_cast<int>(i));
            y -= RowH + 3.0f;any = true;
        }
        if (!any) {
            auto* none = ui::menuLabel(crash ? i18n::text("debug-logs-no-crash", "No crash logs") : i18n::text("debug-logs-none", "No logs"),
                .24f, {x, 140}, ui::TextMuted, ui::FontWeight::SemiBold);
            ui::fitMenuLabelWidth(none, ColumnW - 20.0f, .24f, .16f);panel->addChild(none, 6);
        }
    }

    bool init() {
        if (!Popup::init(W, H)) return false;
        m_bgSprite->setVisible(false);setID("ranked-debug-logs"_spr);
        auto& uploader = DebugLogUploader::get();
        uploader.clearStatus();
        m_files = DebugLogUploader::listFiles();
        // Default: this session's log, the one before it and the newest crash log.
        std::size_t logs = 0;bool crash = false;
        for (std::size_t i = 0; i < m_files.size(); ++i) {
            if (!m_files[i].crash && logs < 2) { m_selected.insert(i);++logs; }
            else if (m_files[i].crash && !crash) { m_selected.insert(i);crash = true; }
        }
        auto* panel = ui::surface({W, H}, {W / 2.0f, H / 2.0f}, ui::Gold, ui::Surface, 255, true);
        m_mainLayer->addChild(panel);
        auto* title = ui::menuLabel(i18n::text("debug-logs-title", "SEND LOGS"), .46f, {W / 2.0f, 258}, ui::Gold, ui::FontWeight::Black);
        ui::fitMenuLabelWidth(title, W - 120.0f, .46f, .3f);panel->addChild(title, 6);
        auto* hint = ui::menuLabel(fmt::format(fmt::runtime(i18n::text("debug-logs-hint", "Pick up to {} files to send to the developer")), DebugLogMaxFiles),
            .22f, {W / 2.0f, 238}, ui::TextMuted, ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(hint, W - 40.0f, .22f, .15f);panel->addChild(hint, 6);
        m_menu = CCMenu::create();m_menu->setPosition(CCPointZero);panel->addChild(m_menu, 20);
        column(panel, false, W / 2.0f - ColumnW / 2.0f - 4.0f);
        column(panel, true, W / 2.0f + ColumnW / 2.0f + 4.0f);
        m_count = ui::menuLabel("", .24f, {78, 24}, ui::TextMuted, ui::FontWeight::ExtraBold);panel->addChild(m_count, 6);
        m_status = ui::menuLabel("", .22f, {W / 2.0f, 42}, ui::TextMuted, ui::FontWeight::SemiBold);panel->addChild(m_status, 6);
        auto* closeFace = ui::surface({84, 30}, {}, ui::Blue, ui::SurfaceRaised, 255, false);
        auto* closeText = ui::menuLabel(i18n::text("close", "CLOSE"), .30f, {42, 15}, ui::Text, ui::FontWeight::Black);
        ui::fitMenuLabelWidth(closeText, 72.0f, .30f, .2f);closeFace->addChild(closeText, 5);
        button(closeFace, menu_selector(DebugLogPopup::onClose), {W / 2.0f + 34.0f, 22});
        m_send = button(ui::surface({132, 30}, {}, ui::Gold, ui::SurfaceRaised, 255, true), menu_selector(DebugLogPopup::onSend), {W - 76.0f, 22});
        refreshRows();
        schedule(schedule_selector(DebugLogPopup::refresh), .1f);
        return true;
    }

    void onRow(CCObject* sender) {
        if (DebugLogUploader::get().busy()) return;
        auto const index = static_cast<std::size_t>(static_cast<CCNode*>(sender)->getTag());
        if (index >= m_files.size()) return;
        if (m_selected.contains(index)) m_selected.erase(index);
        else if (m_selected.size() < DebugLogMaxFiles) m_selected.insert(index);
        DebugLogUploader::get().clearStatus();
        refreshRows();
    }
    void onSend(CCObject*) {
        auto& uploader = DebugLogUploader::get();
        if (uploader.busy() || m_selected.empty()) return;
        std::vector<DebugLogFile> files;
        for (auto index : m_selected) files.push_back(m_files[index]);
        uploader.start(files);
        refreshRows();
    }
    void refresh(float) {
        auto const& status = DebugLogUploader::get().status();
        if (status != m_status->getString()) {
            m_status->setString(status.c_str());
            m_status->setColor(status == i18n::text("debug-logs-done", "LOGS SENT") ? ui::Green
                : status == i18n::text("debug-logs-failed", "LOG UPLOAD FAILED") ? ui::Red : ui::Gold);
            ui::fitMenuLabelWidth(m_status, W - 40.0f, .22f, .15f);
            refreshRows();
        }
    }

public:
    static DebugLogPopup* create() {
        auto* popup = new DebugLogPopup;
        if (popup->init()) { popup->autorelease();return popup; }
        delete popup;return nullptr;
    }
    void onExit() override {
        unschedule(schedule_selector(DebugLogPopup::refresh));
        if (!m_exited) { m_exited = true;CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};

} // namespace

void showDebugLogPopup() {
    if (auto* popup = DebugLogPopup::create()) popup->show();
}

} // namespace corum::ranked
#endif
