#include "SurvivalDifficultyVote.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "ModeLobbyMotion.hpp"
#include <Geode/binding/GJDifficultySprite.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
ccColor3B cardColor(std::size_t index) {
    return index == 0 ? Green : index == 1 ? Orange : index == 2 ? Red : TextMuted;
}
}

SurvivalDifficultyVote* SurvivalDifficultyVote::create(CCSize size, std::function<void(std::string const&)> vote) {
    auto* node = new SurvivalDifficultyVote;
    if (node->init(size, std::move(vote))) { node->autorelease(); return node; }
    delete node; return nullptr;
}

bool SurvivalDifficultyVote::init(CCSize size, std::function<void(std::string const&)> vote) {
    if (!CCNode::init()) return false;
    setID("survival-difficulty-vote"_spr);
    setContentSize(size);m_vote = std::move(vote);
    auto const w = size.width, h = size.height;
    // The shrunken board keeps the top ~40%; title, timer and cards fill the rest.
    m_title = menuLabel(i18n::text("survival-vote-title", "SELECT YOUR PREFERRED DIFFICULTY."), .50f, {w / 2, h * .56f}, Text, FontWeight::Black);
    fitFullMenuLabel(m_title, w - 40, 26, .50f);addChild(m_title, 5);
    m_status = menuLabel("", .26f, {w / 2, h * .56f - 20}, TextMuted, FontWeight::SemiBold);addChild(m_status, 5);
    m_menu = CCMenu::create();m_menu->setPosition(CCPointZero);addChild(m_menu, 6);
    auto const gap = 10.0f;
    // Cards sit between the timer line and the bottom buttons (SURRENDER,
    // debug Force), which span roughly y 8-36.
    constexpr float bottom = 44.0f;
    auto const cardW = std::min(126.0f, (w - 40 - gap * 3) / 4);
    auto const cardH = std::clamp(h * .56f - 30.0f - bottom, 80.0f, 150.0f);
    for (std::size_t i = 0; i < SurvivalVoteCards.size(); ++i) {
        auto const& spec = SurvivalVoteCards[i];
        auto* body = CCNodeRGBA::create();body->setCascadeOpacityEnabled(true);body->setContentSize({cardW, cardH});
        body->setAnchorPoint({.5f, .5f});
        auto* chrome = surface({cardW, cardH}, {cardW / 2, cardH / 2}, Blue, SurfaceRaised, 248, true);body->addChild(chrome, 1);
        if (auto* face = GJDifficultySprite::create(spec.nativeDifficulty, GJDifficultyName::Short)) {
            face->setScale(std::min(1.0f, cardH * .30f / std::max(1.0f, face->getContentSize().height)));
            face->setPosition({cardW / 2, cardH - cardH * .22f});body->addChild(face, 5);
        }
        auto* title = menuLabel(i18n::text(spec.titleKey, spec.titleFallback), .42f, {cardW / 2, cardH * .50f}, cardColor(i), FontWeight::Black);
        fitFullMenuLabel(title, cardW - 12, 18, .42f);body->addChild(title, 5);
        auto* caption = menuLabel(spec.face, .20f, {cardW / 2, cardH * .40f}, TextMuted, FontWeight::SemiBold);body->addChild(caption, 5);
        auto* desc = menuLabel("", .22f, {cardW / 2, cardH * .20f}, Text, FontWeight::SemiBold);
        fitWrappedMenuLabel(desc, i18n::text(spec.descKey, spec.descFallback), cardW - 14, cardH * .30f, .22f);body->addChild(desc, 5);
        auto* item = CCMenuItemSpriteExtra::create(body, this, menu_selector(SurvivalDifficultyVote::onCard));
        item->setTag(static_cast<int>(i));
        auto const x = w / 2 + (static_cast<float>(i) - 1.5f) * (cardW + gap);
        auto const y = bottom + cardH / 2;
        item->setPosition({x, y});m_menu->addChild(item);
        std::vector<std::pair<CCLayerColor*, GLubyte>> layers;
        for (auto* child : CCArrayExt<CCNode*>(chrome->getChildren()))
            if (auto* layer = typeinfo_cast<CCLayerColor*>(child)) layers.emplace_back(layer, layer->getOpacity());
        m_cards[i] = {item, body, chrome, std::move(layers), y};
    }
    setVisible(false);
    return true;
}

void SurvivalDifficultyVote::onCard(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item || m_stage != SurvivalVoteStage::Choosing || !m_vote) return;
    auto const index = item->getTag();
    if (index < 0 || index >= static_cast<int>(SurvivalVoteCards.size())) return;
    // Not sent yet: the pick can still change for SurvivalVoteConfirmSeconds.
    m_pending = index;m_pendingFor = 0.0f;
}

void SurvivalDifficultyVote::commitPending() {
    if (m_pending < 0 || !m_vote) return;
    m_chosen = m_pending;m_pending = -1;
    m_vote(SurvivalVoteCards[static_cast<std::size_t>(m_chosen)].ballot);
}

float SurvivalDifficultyVote::boardShrink() const {
    if (m_stage == SurvivalVoteStage::Choosing) return std::clamp(m_entry / .45f, 0.0f, 1.0f);
    if (m_stage == SurvivalVoteStage::Chosen) return 1.0f - std::clamp((m_leave - .25f) / .45f, 0.0f, 1.0f);
    return 0.0f;
}

void SurvivalDifficultyVote::present(SurvivalVoteStage stage, std::string const& myBallot,
    std::optional<std::int64_t> closesInMs, int voted, int total, float dt) {
    if (stage != m_stage) {
        if (stage == SurvivalVoteStage::Choosing) { m_entry = 0;m_leave = 0;m_chosen = -1;m_pending = -1; }
        if (stage == SurvivalVoteStage::Chosen) { m_leave = 0;m_pulse = 0;if (m_entry <= 0) m_entry = 1; }
        m_stage = stage;
    }
    // A ballot echoed by the server (e.g. after a scene rebuild) still shows its card.
    if (m_chosen < 0) m_chosen = survivalVoteCardIndex(myBallot);
    auto const visible = showing() || (m_stage == SurvivalVoteStage::Closed && m_leave < .7f && m_entry > 0);
    setVisible(visible);
    if (!visible) return;
    if (m_stage == SurvivalVoteStage::Choosing) {
        m_entry += dt;
        if (m_pending >= 0) {
            m_pendingFor += dt;
            // Send at 1 s, or at once when the window is about to close.
            if (m_pendingFor >= SurvivalVoteConfirmSeconds || (closesInMs && *closesInMs < 300)) commitPending();
        }
    }
    else { m_leave += dt;m_pulse += dt;m_pending = -1; }
    auto const leave = std::clamp(m_leave / .30f, 0.0f, 1.0f);
    for (std::size_t i = 0; i < m_cards.size(); ++i) {
        auto& card = m_cards[i];
        auto const entry = survivalVoteCardEntry(m_entry, i);
        auto const chosen = static_cast<int>(i) == m_chosen;
        auto const pending = static_cast<int>(i) == m_pending;
        // After the vote: the other cards vanish in place quickly; the chosen
        // card pulses, then fades out while growing slightly (no falling cards).
        auto fade = m_stage == SurvivalVoteStage::Choosing ? (m_pending >= 0 && !pending ? 0.55f : 1.0f)
            : chosen ? 1.0f - std::clamp((m_leave - .45f) / .20f, 0.0f, 1.0f) : 1.0f - std::clamp(m_leave / .18f, 0.0f, 1.0f);
        if (m_stage == SurvivalVoteStage::Closed) fade = std::min(fade, 1.0f - std::clamp(m_leave / .2f, 0.0f, 1.0f));
        auto const opacity = static_cast<GLubyte>(std::clamp(entry * fade, 0.0f, 1.0f) * 255);
        card.body->setOpacity(opacity);propagateModeOpacity(card.body, opacity);
        for (auto const& [layer, base] : card.layers) layer->setOpacity(static_cast<GLubyte>(base * opacity / 255));
        // A faded card leaves nothing behind over the returning board.
        card.item->setVisible(opacity > 3);
        card.item->setPosition({card.item->getPositionX(), card.baseY - (1.0f - entry) * 24.0f});
        auto scale = 1.0f;
        if (pending) scale = 1.05f;
        else if (chosen && m_stage != SurvivalVoteStage::Choosing)
            scale = 1.0f + .06f * std::sin(std::min(m_pulse / .45f, 1.0f) * 3.14159265f) + .06f * std::clamp((m_leave - .45f) / .20f, 0.0f, 1.0f);
        card.item->setScale(scale);
        card.item->setEnabled(m_stage == SurvivalVoteStage::Choosing);
    }
    auto const titleOpacity = static_cast<GLubyte>(std::clamp(std::min(m_entry / .32f, 1.0f - leave), 0.0f, 1.0f) * 255);
    m_title->setOpacity(titleOpacity);m_status->setOpacity(titleOpacity);
    m_title->setPositionY(getContentSize().height * .56f - (1.0f - std::clamp(m_entry / .42f, 0.0f, 1.0f)) * 16.0f);
    auto const secondsLeft = survivalVoteSecondsLeft(closesInMs);
    auto status = fmt::format("{}  ·  {}",
        fmt::format(fmt::runtime(i18n::text("survival-vote-time", "TIME LEFT {}s")), secondsLeft),
        fmt::format(fmt::runtime(i18n::text("survival-vote-count", "VOTED {}/{}")), std::max(0, voted), std::max(0, total)));
    if (m_pending >= 0) status = fmt::format(fmt::runtime(i18n::text("survival-vote-confirming", "CONFIRMING IN {}s · TAP ANOTHER CARD TO CHANGE")),
        std::max(1, static_cast<int>(std::ceil(SurvivalVoteConfirmSeconds - m_pendingFor))));
    // Last seconds of the window: the timer line turns red.
    m_status->setColor(m_stage == SurvivalVoteStage::Choosing && secondsLeft <= SurvivalVoteUrgentSeconds ? Red : TextMuted);
    if (status != m_statusText) { m_statusText = status;m_status->setString(status.c_str());fitMenuLabelWidth(m_status, getContentSize().width - 40, .26f, .18f); }
}

} // namespace corum::ranked::ui
