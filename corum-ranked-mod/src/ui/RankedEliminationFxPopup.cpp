#include "RankedEliminationFxPopup.hpp"

#include "../RankedRuntime.hpp"
#include "MapPoolVisuals.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
using namespace mappool;
namespace {

constexpr float kWidth = 340.0f;
constexpr float kRow = 42.0f;

std::size_t effectIndex(EliminationEffect effect) {
    for (std::size_t i = 0; i < kEliminationEffects.size(); ++i) if (kEliminationEffects[i] == effect) return i;
    return 0;
}

std::string effectName(EliminationEffect effect) {
    auto const code = std::string(eliminationEffectCode(effect));
    return i18n::text("elim-fx-" + code, code);
}

std::string requirementText(EliminationEffect effect) {
    auto const requirement = eliminationRequirement(effect);
    auto const key = std::string(requirement.key);
    auto const fallback = key == "elim-req-survival" ? std::string("Reach Survival {}") : key == "elim-req-both" ?
        std::string("Reach {} in Survival and Solo Ranked") : key == "elim-req-grade" ? std::string("Reach achievement grade {}") :
        std::string("Always available");
    auto const format = i18n::text(key, fallback);
    if (requirement.tier.empty()) return format;
    return fmt::format(fmt::runtime(format), requirement.tier);
}

// Small polygon glyph per effect (the popup is not part of the lobby fade).
CCDrawNode* glyph(EliminationEffect effect, ccColor3B tone) {
    auto* node = CCDrawNode::create();
    node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    auto const c = ccc4f(tone.r / 255.0f, tone.g / 255.0f, tone.b / 255.0f, 1.0f);
    auto poly = [&](std::vector<CCPoint> points) { node->drawPolygon(points.data(), static_cast<unsigned int>(points.size()), c, 0, c); };
    switch (effect) {
    case EliminationEffect::Bolt: poly({{2, 9}, {-5, -1}, {0, -1}, {-2, -9}, {5, 1}, {0, 1}}); break;
    case EliminationEffect::Slice:
        poly({{-7, -7}, {-1, -7}, {-1, -1}, {-7, -1}});poly({{1, 1}, {7, 1}, {7, 7}, {1, 7}});
        poly({{-9, 8}, {-7, 9}, {9, -8}, {7, -9}}); break;
    case EliminationEffect::Shatter:
        poly({{-8, 8}, {-1, 6}, {-5, 1}});poly({{2, 8}, {8, 4}, {3, 2}});poly({{-7, -2}, {-1, -3}, {-3, -9}});poly({{2, -2}, {8, -4}, {4, -9}}); break;
    case EliminationEffect::Vortex:
        for (int i = 0; i < 7; ++i) {
            auto const a = static_cast<float>(i) * 0.95f, r = 2.0f + static_cast<float>(i) * 1.1f, s = 1.0f + static_cast<float>(i) * 0.18f;
            auto const x = std::cos(a) * r, y = std::sin(a) * r;
            poly({{x - s, y}, {x, y + s}, {x + s, y}, {x, y - s}});
        }
        break;
    default: poly({{-2, 8}, {2, 8}, {2, -1}, {6, -1}, {0, -8}, {-6, -1}, {-2, -1}}); break;
    }
    return node;
}

} // namespace

RankedEliminationFxPopup* RankedEliminationFxPopup::create() {
    auto* popup = new RankedEliminationFxPopup;
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool RankedEliminationFxPopup::init() {
    auto const window = CCDirector::sharedDirector()->getWinSize();
    auto const height = std::min(96.0f + kRow * static_cast<float>(kEliminationEffects.size()), window.height - 16);
    auto const size = CCSize{std::min(kWidth, window.width - 24), height};
    if (!Popup::init(size.width, size.height)) return false;
    setID("ranked-elimination-fx-popup"_spr);
    m_noElasticity = true;
    m_bgSprite->setVisible(false);
    m_closeBtn->setVisible(false);
    m_panel = surface(size, {size.width / 2, size.height / 2}, Gold, Surface, 255, true);
    m_mainLayer->addChild(m_panel, 10);
    showMessage(i18n::text("elim-fx-loading", "Loading..."), TextMuted);
    fetch();
    return true;
}

void RankedEliminationFxPopup::showMessage(std::string const& message, ccColor3B color) {
    if (m_content) m_content->removeFromParentAndCleanup(true);
    m_rows = nullptr;
    m_content = CCNode::create();
    m_content->setContentSize(m_panel->getContentSize());
    m_panel->addChild(m_content, 5);
    auto const size = m_content->getContentSize();
    caption(m_content, i18n::text("elim-fx-title", "ELIMINATION EFFECT"), {size.width / 2, size.height - 18}, 0.42f, size.width - 40, Gold, FontWeight::Black);
    caption(m_content, message, {size.width / 2, size.height / 2}, 0.27f, size.width - 40, color);
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    m_content->addChild(menu, 20);
    auto* close = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("close", "CLOSE"), {96, 24}, Blue, false),
        this, menu_selector(RankedEliminationFxPopup::onClose));
    close->setPosition({size.width / 2 - (m_loaded ? 0 : 54), 20});
    menu->addChild(close);
    if (!m_loaded) {
        auto* retry = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("retry", "RETRY"), {96, 24}, Cyan, false),
            this, menu_selector(RankedEliminationFxPopup::onRetry));
        retry->setPosition({size.width / 2 + 54, 20});
        menu->addChild(retry);
    }
}

void RankedEliminationFxPopup::fetch() {
    if (m_closed || m_busy) return;
    m_busy = true;
    auto const generation = ++m_generation;
    auto pending = request();
    m_request.spawn(pending.get(RankedRuntime::get().eliminationEffectsUrl()), [weak = WeakRef(this), generation](web::WebResponse response) {
        queueInMainThread([weak, generation, response = std::move(response)] {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            if (!response.ok() || !data["effects"].isArray()) {
                self->showMessage(i18n::text("elim-fx-error", "Could not load the effects. Try again."), Red);
                return;
            }
            self->apply(data);
        });
    });
}

void RankedEliminationFxPopup::equip(EliminationEffect effect) {
    if (m_closed || m_busy) return;
    m_busy = true;
    auto const generation = ++m_generation;
    auto pending = request();
    matjson::Value body;
    body["effect"] = std::string(eliminationEffectCode(effect));
    pending.bodyJSON(body);
    m_request.spawn(pending.post(RankedRuntime::get().eliminationEffectsUrl(true)), [weak = WeakRef(this), generation](web::WebResponse response) {
        queueInMainThread([weak, generation, response = std::move(response)] {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            if (response.ok() && data["effects"].isArray()) self->apply(data);
            else self->render();
        });
    });
}

void RankedEliminationFxPopup::apply(matjson::Value const& data) {
    m_loaded = true;
    m_unlocked.fill(false);
    m_unlocked[0] = true;
    for (auto const& entry : data["effects"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
        auto const effect = eliminationEffectFromCode(entry["id"].asString().unwrapOr(""));
        if (entry["unlocked"].asBool().unwrapOr(false)) m_unlocked[effectIndex(effect)] = true;
    }
    m_equipped = eliminationEffectFromCode(data["equipped"].asString().unwrapOr("DEFAULT"));
    render();
}

void RankedEliminationFxPopup::render() {
    showMessage("", TextMuted);
    auto const size = m_content->getContentSize();
    caption(m_content, i18n::text("elim-fx-note", "Plays on every eliminated player when you finish 1st in a round"),
        {size.width / 2, size.height - 36}, 0.21f, size.width - 36, TextMuted);
    m_rows = CCMenu::create();
    m_rows->setPosition(CCPointZero);
    m_content->addChild(m_rows, 20);
    auto const rowWidth = size.width - 24;
    for (std::size_t i = 0; i < kEliminationEffects.size(); ++i) {
        auto const effect = kEliminationEffects[i];
        auto const unlocked = m_unlocked[i], equipped = effect == m_equipped;
        auto const y = size.height - 70 - kRow * static_cast<float>(i);
        auto* row = surface({rowWidth, kRow - 6}, {size.width / 2, y}, equipped ? Gold : unlocked ? Blue : SurfaceRaised,
            equipped ? SurfaceSoft : SurfaceRaised, 250, true);
        m_content->addChild(row, 6);
        auto* mark = glyph(effect, unlocked ? Gold : TextMuted);
        mark->setPosition({22, (kRow - 6) / 2});
        row->addChild(mark, 5);
        caption(row, effectName(effect), {42, (kRow - 6) / 2 + 7}, 0.29f, rowWidth - 140, equipped ? Gold : unlocked ? Text : TextMuted,
            FontWeight::Black, 0.0f);
        caption(row, requirementText(effect), {42, (kRow - 6) / 2 - 8}, 0.19f, rowWidth - 140, unlocked ? ccColor3B{125, 219, 140} : ccColor3B{255, 138, 151},
            FontWeight::SemiBold, 0.0f);
        auto const label = equipped ? i18n::text("elim-equipped", "EQUIPPED") : unlocked ? i18n::text("elim-equip", "EQUIP") : i18n::text("elim-locked", "LOCKED");
        auto* face = buttonFace(label, {78, 24}, unlocked ? Cyan : SurfaceRaised, equipped, 0.22f, unlocked ? Text : TextMuted);
        auto* button = CCMenuItemSpriteExtra::create(face, this, menu_selector(RankedEliminationFxPopup::onEquip));
        button->setTag(static_cast<int>(i));
        button->setPosition({size.width / 2 + rowWidth / 2 - 48, y});
        button->setEnabled(unlocked && !equipped);
        m_rows->addChild(button);
    }
}

void RankedEliminationFxPopup::onEquip(CCObject* sender) {
    if (!sender || m_busy) return;
    auto const index = sender->getTag();
    if (index < 0 || static_cast<std::size_t>(index) >= kEliminationEffects.size() || !m_unlocked[index]) return;
    equip(kEliminationEffects[index]);
}

void RankedEliminationFxPopup::onRetry(CCObject*) { fetch(); }

void RankedEliminationFxPopup::onClose(CCObject*) { dismiss(); }

void RankedEliminationFxPopup::dismiss() {
    if (m_closed) return;
    [[maybe_unused]] Ref<RankedEliminationFxPopup> keepAlive(this);
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    Popup::onClose(nullptr);
}

void RankedEliminationFxPopup::onExit() {
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
    Popup::onExit();
}

} // namespace corum::ranked::ui
