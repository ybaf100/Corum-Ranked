#include "RankedEliminationFxPopup.hpp"

#include "../RankedRuntime.hpp"
#include "../domain/CompetitiveHudLayout.hpp"
#include "MapPoolVisuals.hpp"
#include "PlayerIcon.hpp"
#include "SurvivalEliminationFx.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
using namespace mappool;
namespace {

constexpr float kListWidth = 300.0f;
constexpr float kRow = 42.0f;
constexpr float kIconScale = 0.5f;
// The preview replays the whole 1.55 s elimination window, then waits.
constexpr float kPreviewLead = 0.3f;
constexpr float kPreviewLoop = 2.6f;

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
    auto const size = CCSize{std::min(560.0f, window.width - 24), std::min(98.0f + kRow * static_cast<float>(kEliminationEffects.size()), window.height - 16)};
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
    m_stage = nullptr;
    m_world = nullptr;
    m_victims.clear();
    m_content = CCNode::create();
    m_content->setContentSize(m_panel->getContentSize());
    m_panel->addChild(m_content, 5);
    auto const size = m_content->getContentSize();
    caption(m_content, i18n::text("elim-fx-title", "ELIMINATION EFFECT"), {size.width / 2, size.height - 18}, 0.42f, size.width - 200, Gold, FontWeight::Black);
    if (!message.empty()) caption(m_content, message, {size.width / 2, size.height / 2}, 0.27f, size.width - 40, color);
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    m_content->addChild(menu, 20);
    auto* close = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("close", "CLOSE"), {64, 22}, Blue, false),
        this, menu_selector(RankedEliminationFxPopup::onClose));
    close->setPosition({44, size.height - 20});
    menu->addChild(close);
    if (!m_loaded) {
        auto* retry = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("retry", "RETRY"), {96, 24}, Cyan, false),
            this, menu_selector(RankedEliminationFxPopup::onRetry));
        retry->setPosition({size.width / 2, 24});
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
    auto const first = !m_loaded;
    m_loaded = true;
    m_unlocked.fill(false);
    m_unlocked[0] = true;
    for (auto const& entry : data["effects"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
        auto const effect = eliminationEffectFromCode(entry["id"].asString().unwrapOr(""));
        if (entry["unlocked"].asBool().unwrapOr(false)) m_unlocked[effectIndex(effect)] = true;
    }
    m_equipped = eliminationEffectFromCode(data["equipped"].asString().unwrapOr("DEFAULT"));
    if (first) m_selected = m_equipped;
    render();
}

void RankedEliminationFxPopup::render() {
    showMessage("", TextMuted);
    auto const size = m_content->getContentSize();
    caption(m_content, i18n::text("elim-fx-note", "Plays on every eliminated player when you finish 1st in a round"),
        {size.width / 2, size.height - 36}, 0.20f, size.width - 36, TextMuted);
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    m_content->addChild(menu, 20);
    auto const listWidth = std::min(kListWidth, size.width * 0.56f);
    for (std::size_t i = 0; i < kEliminationEffects.size(); ++i) {
        auto const effect = kEliminationEffects[i];
        auto const unlocked = m_unlocked[i], equipped = effect == m_equipped, selected = effect == m_selected;
        auto const rowSize = CCSize{listWidth, kRow - 6};
        auto* row = surface(rowSize, {}, selected ? Gold : unlocked ? Blue : SurfaceRaised, selected ? SurfaceSoft : SurfaceRaised, 250, true);
        auto* mark = glyph(effect, unlocked ? Gold : TextMuted);
        mark->setPosition({20, rowSize.height / 2});
        row->addChild(mark, 5);
        caption(row, effectName(effect), {38, rowSize.height / 2 + 7}, 0.27f, listWidth - 110, selected ? Gold : unlocked ? Text : TextMuted,
            FontWeight::Black, 0.0f);
        caption(row, requirementText(effect), {38, rowSize.height / 2 - 8}, 0.17f, listWidth - 48, unlocked ? ccColor3B{125, 219, 140} : ccColor3B{255, 138, 151},
            FontWeight::SemiBold, 0.0f);
        auto const tag = equipped ? i18n::text("elim-equipped", "EQUIPPED") : unlocked ? std::string() : i18n::text("elim-locked", "LOCKED");
        if (!tag.empty()) caption(row, tag, {listWidth - 8, rowSize.height / 2 + 7}, 0.17f, 64, equipped ? Gold : ccColor3B{255, 138, 151},
            FontWeight::Black, 1.0f);
        auto* item = CCMenuItemSpriteExtra::create(row, this, menu_selector(RankedEliminationFxPopup::onSelect));
        item->setTag(static_cast<int>(i));
        item->setPosition({12 + listWidth / 2, size.height - 70 - kRow * static_cast<float>(i)});
        menu->addChild(item);
    }

    // Preview stage on the right, the selected effect's buttons below it.
    auto const stageLeft = 24 + listWidth;
    auto const stageSize = CCSize{size.width - stageLeft - 12, size.height - 50 - 56};
    m_stage = surface(stageSize, {stageLeft + stageSize.width / 2, 56 + stageSize.height / 2}, Blue, {7, 13, 24}, 255, true);
    m_content->addChild(m_stage, 6);
    auto const index = effectIndex(m_selected);
    auto const buttonWidth = (stageSize.width - 6) / 2;
    auto* replay = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("elim-replay", "REPLAY"), {buttonWidth, 26}, Cyan, false, 0.22f),
        this, menu_selector(RankedEliminationFxPopup::onReplay));
    replay->setPosition({stageLeft + buttonWidth / 2, 28});
    menu->addChild(replay);
    auto const unlocked = m_unlocked[index], equipped = m_selected == m_equipped;
    auto const label = equipped ? i18n::text("elim-equipped", "EQUIPPED") : unlocked ? i18n::text("elim-equip", "EQUIP") : i18n::text("elim-locked", "LOCKED");
    auto* equipButton = CCMenuItemSpriteExtra::create(buttonFace(label, {buttonWidth, 26}, unlocked ? Gold : SurfaceRaised, equipped, 0.22f,
        unlocked ? Text : TextMuted), this, menu_selector(RankedEliminationFxPopup::onEquip));
    equipButton->setPosition({stageLeft + stageSize.width - buttonWidth / 2, 28});
    equipButton->setEnabled(unlocked && !equipped && !m_busy);
    menu->addChild(equipButton);
    playPreview();
    unschedule(schedule_selector(RankedEliminationFxPopup::previewTick));
    schedule(schedule_selector(RankedEliminationFxPopup::previewTick), kPreviewLoop);
}

// A clipped miniature of the result board: the local player as the gold 1st
// place card and three bot cards in a row to be eliminated.
void RankedEliminationFxPopup::buildStage() {
    if (!m_stage) return;
    if (m_world) m_world->getParent()->removeFromParentAndCleanup(true);
    m_world = nullptr;
    m_victims.clear();
    auto const size = m_stage->getContentSize();
    auto* mask = CCDrawNode::create();
    CCPoint rect[4] = {{1, 1}, {size.width - 1, 1}, {size.width - 1, size.height - 1}, {1, size.height - 1}};
    mask->drawPolygon(rect, 4, {1, 1, 1, 1}, 0, {1, 1, 1, 1});
    auto* clip = CCClippingNode::create(mask);
    m_stage->addChild(clip, 10);
    m_world = CCNode::create();
    m_world->setContentSize(size);
    clip->addChild(m_world);

    auto const executorSize = CCSize{std::min(96.0f, size.width * 0.5f), 34};
    auto* executor = surface(executorSize, {6 + executorSize.width / 2, size.height - 6 - executorSize.height / 2}, Gold, {48, 36, 6}, 250, true);
    m_world->addChild(executor, 2);
    if (auto* me = PlayerIcon::createLocal()) { me->setScale(kIconScale); me->setPosition({17, 17}); executor->addChild(me, 5); }
    auto const first = i18n::isKorean() ? fmt::format(fmt::runtime(i18n::text("player-rank", "{}")), 1) : std::string("1st");
    caption(executor, first, {executorSize.width - 8, 17}, 0.26f, executorSize.width - 40, Gold, FontWeight::Black, 1.0f);
    m_executorAt = m_world->convertToNodeSpace(executor->convertToWorldSpace({17, 17}));

    auto const cardWidth = (size.width - 16) / 3.0f - 4.0f;
    for (int i = 0; i < 3; ++i) {
        auto const x = 8 + static_cast<float>(i) * (cardWidth + 4) + cardWidth / 2 + 2;
        auto* card = surface({cardWidth, 40}, {x, 30}, Blue, SurfaceRaised, 250, true);
        m_world->addChild(card, 2);
        auto* slot = CCNode::create();
        slot->setPosition({cardWidth / 2, 21});
        card->addChild(slot, 6);
        if (auto* icon = PlayerIcon::createBot(i + 1)) { icon->setScale(kIconScale); slot->addChild(icon); }
        m_victims.push_back({card, slot});
    }
}

void RankedEliminationFxPopup::playPreview() {
    buildStage();
    if (!m_world) return;
    auto const starts = survivalEliminationStarts(m_victims.size());
    for (std::size_t i = 0; i < m_victims.size() && i < starts.size(); ++i) {
        m_world->runAction(CCSequence::create(CCDelayTime::create(kPreviewLead + starts[i] / 1000.0f),
            CallFuncExt::create([weak = WeakRef(this), i] { if (auto self = weak.lock()) self->executeVictim(i); }), nullptr));
    }
}

void RankedEliminationFxPopup::executeVictim(std::size_t index) {
    if (!m_world || index >= m_victims.size()) return;
    auto [card, slot] = m_victims[index];
    // The card turns red as its effect starts.
    auto* red = CCLayerColor::create({255, 82, 105, 0}, card->getContentSize().width, card->getContentSize().height);
    card->addChild(red, 4);
    red->runAction(CCFadeTo::create(0.18f, 120));
    auto const fall = m_world->getContentSize().height;
    auto const order = static_cast<int>(index);
    EliminationFxTarget target{slot, card, slot->getPosition(), kIconScale,
        [order]() -> CCNode* { return PlayerIcon::createBot(order + 1); }, order};
    switch (m_selected) {
    case EliminationEffect::Default: playDefaultFall(slot, order, fall); break;
    case EliminationEffect::Slice: {
        auto* ghost = PlayerIcon::createLocal();
        if (!ghost) { playSliceFx(target, fall, {kSliceLeave.x - kSliceEnter.x, kSliceLeave.y - kSliceEnter.y}); break; }
        ghost->setScale(kIconScale * 1.15f);
        ghost->setPosition(m_executorAt);
        m_world->addChild(ghost, 30);
        auto const victim = m_world->convertToNodeSpace(card->convertToWorldSpace(slot->getPosition()));
        runSliceGhost(ghost, victim, m_executorAt, [target, fall]() {
            playSliceFx(target, fall, {kSliceLeave.x - kSliceEnter.x, kSliceLeave.y - kSliceEnter.y});
        });
        break;
    }
    default:
        playEliminationFx(m_selected, target, fall, card->convertToNodeSpace(m_world->convertToWorldSpace(m_executorAt)));
        break;
    }
}

void RankedEliminationFxPopup::previewTick(float) {
    if (!m_closed) playPreview();
}

void RankedEliminationFxPopup::onSelect(CCObject* sender) {
    if (!sender || m_closed) return;
    auto const index = sender->getTag();
    if (index < 0 || static_cast<std::size_t>(index) >= kEliminationEffects.size()) return;
    m_selected = kEliminationEffects[static_cast<std::size_t>(index)];
    render();
}

void RankedEliminationFxPopup::onEquip(CCObject*) {
    if (m_busy || m_closed) return;
    if (!m_unlocked[effectIndex(m_selected)] || m_selected == m_equipped) return;
    equip(m_selected);
}

void RankedEliminationFxPopup::onReplay(CCObject*) {
    if (m_closed) return;
    playPreview();
    unschedule(schedule_selector(RankedEliminationFxPopup::previewTick));
    schedule(schedule_selector(RankedEliminationFxPopup::previewTick), kPreviewLoop);
}

void RankedEliminationFxPopup::onRetry(CCObject*) { fetch(); }

void RankedEliminationFxPopup::onClose(CCObject*) { dismiss(); }

void RankedEliminationFxPopup::dismiss() {
    if (m_closed) return;
    [[maybe_unused]] Ref<RankedEliminationFxPopup> keepAlive(this);
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    unschedule(schedule_selector(RankedEliminationFxPopup::previewTick));
    Popup::onClose(nullptr);
}

void RankedEliminationFxPopup::onExit() {
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    unschedule(schedule_selector(RankedEliminationFxPopup::previewTick));
    if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
    Popup::onExit();
}

} // namespace corum::ranked::ui
