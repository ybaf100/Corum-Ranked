#include "SurvivalDifficultyVote.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "ModeLobbyMotion.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
// S1 board colours: Easy #4BE3B0, Normal lime, Hard red, no preference muted.
ccColor3B cardColor(std::size_t index) {
    return index == 0 ? ccColor3B{75, 227, 176} : index == 1 ? Accent : index == 2 ? Red : TextMuted;
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
    // S1 board (1136x640 artifact = 2x points; CSS px / 64 = label scale):
    // ink ground, 72 px header, the instruction line, four panels and the
    // 90 px footer. The page covers the standings board while it is open.
    auto const css = [](float pxValue) { return pxValue / 64.0f; };
    m_ground = CCLayerColor::create({Ink.r, Ink.g, Ink.b, 255}, w, h);
    m_ground->setID("survival-vote-ground"_spr);addChild(m_ground, 0);
    m_page = CCNode::create();m_page->setContentSize(size);addChild(m_page, 4);
    auto const headY = h - 18.0f;
    // .num chip (round 01), the leaning brand and the matched count.
    auto* number = menuLabel("01", css(15), {0, 0}, AccentInk, FontWeight::Black);
    auto const numberW = number->getScaledContentSize().width + 8.0f;
    auto* numberFill = CCLayerColor::create({AccentFill.r, AccentFill.g, AccentFill.b, 255}, numberW, 12.0f);
    numberFill->setPosition({14.0f, headY - 6.0f});m_page->addChild(numberFill, 1);m_pageLayers.emplace_back(numberFill, 255);
    number->setPosition({14.0f + numberW / 2.0f, headY});m_page->addChild(number, 2);
    auto* brand = menuLabel(i18n::text("survival-vote-heading", "DIFFICULTY VOTE"), css(30), {14.0f + numberW + 8.0f, headY}, Text, FontWeight::Black);
    brand->setAnchorPoint({0, .5f});leanLabel(brand);fitMenuLabelWidth(brand, w * .42f, css(30), .2f);m_page->addChild(brand, 2);
    m_matched = menuLabel("", css(13), {14.0f + numberW + 8.0f + brand->getScaledContentSize().width + 8.0f, headY}, TextMuted, FontWeight::SemiBold);
    m_matched->setAnchorPoint({0, .5f});m_page->addChild(m_matched, 2);
    // Time panel (.panel.rule): TIME LEFT over the 24 px clock.
    constexpr float timerW = 64.0f, timerH = 27.0f;
    auto const timerX = w - 14.0f - timerW;
    auto* timerFill = CCLayerColor::create({Surface.r, Surface.g, Surface.b, 255}, timerW, timerH);
    timerFill->setPosition({timerX, headY - timerH / 2.0f});m_page->addChild(timerFill, 1);m_pageLayers.emplace_back(timerFill, 255);
    auto* timerRule = CCLayerColor::create({Accent.r, Accent.g, Accent.b, 255}, timerW, 1.0f);
    timerRule->setPosition({timerX, headY + timerH / 2.0f - 1.0f});m_page->addChild(timerRule, 2);m_pageLayers.emplace_back(timerRule, 255);
    m_page->addChild(menuLabel(i18n::text("time-left", "TIME LEFT"), css(13), {timerX + timerW / 2.0f, headY + 6.0f}, TextMuted, FontWeight::SemiBold), 3);
    m_timer = menuLabel("0:00", css(24), {timerX + timerW / 2.0f, headY - 5.0f}, Text, FontWeight::Black);m_page->addChild(m_timer, 3);
    // Instruction line (20 px) and the privacy caption after it.
    auto const lineY = h - 36.0f - 9.0f;
    m_title = menuLabel(i18n::text("survival-vote-title", "SELECT YOUR PREFERRED DIFFICULTY."), css(20), {14.0f, lineY}, Text, FontWeight::Black);
    m_title->setAnchorPoint({0, .5f});fitMenuLabelWidth(m_title, w * .52f, css(20), .18f);m_page->addChild(m_title, 2);
    auto* privacy = menuLabel(i18n::text("survival-vote-private", "VOTES AND COUNTS STAY HIDDEN."), css(13),
        {14.0f + m_title->getScaledContentSize().width + 6.0f, lineY - 1.0f}, TextMuted, FontWeight::SemiBold);
    privacy->setAnchorPoint({0, .5f});fitMenuLabelWidth(privacy, w - 28.0f - m_title->getScaledContentSize().width - 6.0f, css(13), .1f);m_page->addChild(privacy, 2);
    // Footer: the confirm line and the 160-point confirm bar. They sit between
    // the debug Force button (left) and SURRENDER (right) at y 22.
    m_status = menuLabel("", css(16), {136.0f, 22.0f}, Text, FontWeight::ExtraBold);
    m_status->setAnchorPoint({0, .5f});m_page->addChild(m_status, 2);
    m_barWidth = 110.0f;
    m_bar = CCNode::create();m_bar->setPosition({w - 136.0f - m_barWidth, 22.0f});m_page->addChild(m_bar, 2);
    auto* track = CCLayerColor::create({SurfaceRaised.r, SurfaceRaised.g, SurfaceRaised.b, 255}, m_barWidth, 6.0f);
    track->setPosition({0, -3.0f});track->setSkewX(20.0f);m_bar->addChild(track, 0);m_pageLayers.emplace_back(track, 255);
    m_barFill = CCLayerColor::create({Accent.r, Accent.g, Accent.b, 255}, 0.0f, 6.0f);
    m_barFill->setPosition({0, -3.0f});m_barFill->setSkewX(20.0f);m_bar->addChild(m_barFill, 1);m_pageLayers.emplace_back(m_barFill, 255);
    m_menu = CCMenu::create();m_menu->setPosition(CCPointZero);addChild(m_menu, 6);
    // Four panels in a grid (gap 14 px, padding 18 px 28 px) down to the footer.
    constexpr float gap = 7.0f, bottom = 45.0f;
    auto const top = lineY - 9.0f - 9.0f;
    auto const cardW = (w - 28.0f - gap * 3) / 4;
    auto const cardH = std::max(80.0f, top - bottom);
    for (std::size_t i = 0; i < SurvivalVoteCards.size(); ++i) {
        auto const& spec = SurvivalVoteCards[i];
        auto* body = CCNodeRGBA::create();body->setCascadeOpacityEnabled(true);body->setContentSize({cardW, cardH});
        body->setAnchorPoint({.5f, .5f});
        auto* chrome = CCLayerColor::create({Surface.r, Surface.g, Surface.b, 255}, cardW, cardH);
        body->addChild(chrome, 1);
        // Picked: a 3 px lime inset outline.
        auto* outline = CCNode::create();outline->setContentSize({cardW, cardH});body->addChild(outline, 2);
        for (auto const& [x, y, ow, oh] : {std::tuple{0.0f, 0.0f, cardW, 1.5f}, std::tuple{0.0f, cardH - 1.5f, cardW, 1.5f},
                std::tuple{0.0f, 0.0f, 1.5f, cardH}, std::tuple{cardW - 1.5f, 0.0f, 1.5f, cardH}}) {
            auto* edge = CCLayerColor::create({Accent.r, Accent.g, Accent.b, 255}, ow, oh);edge->setPosition({x, y});outline->addChild(edge);
        }
        outline->setVisible(false);
        // Title (56 px, leaning; NO PREFERENCE 44 px) in the difficulty colour.
        auto const titleScale = i == 3 ? css(44) : css(56);
        auto* title = menuLabel(i18n::text(spec.titleKey, spec.titleFallback), titleScale, {10.0f, cardH - 11.0f - 12.0f}, cardColor(i), FontWeight::Black);
        title->setAnchorPoint({0, .5f});leanLabel(title);
        fitMenuLabelWidth(title, i == 3 ? cardW - 20.0f : cardW - 58.0f, titleScale, .2f);body->addChild(title, 5);
        // MY PICK chip (.chip.on.sm) at the right of the title row.
        auto* pick = CCNode::create();body->addChild(pick, 6);
        auto* pickLabel = menuLabel(i18n::text("my-pick", "MY PICK"), css(13), {0, 0}, AccentInk, FontWeight::Black);
        auto const pickW = pickLabel->getScaledContentSize().width + 10.0f, pickH = 10.0f;
        auto* pickFill = CCLayerColor::create({AccentFill.r, AccentFill.g, AccentFill.b, 255}, pickW, pickH);
        pickFill->setPosition({cardW - 8.0f - pickW, (i == 3 ? cardH - 11.0f - 34.0f : cardH - 11.0f - 12.0f) - pickH / 2.0f});pickFill->setSkewX(8.0f);pick->addChild(pickFill, 0);
        pickLabel->setPosition({cardW - 8.0f - pickW / 2.0f, pickFill->getPositionY() + pickH / 2.0f});pick->addChild(pickLabel, 1);
        pick->setVisible(false);
        // Description (.cap, line-height 1.45) under the title.
        auto* desc = menuLabel("", css(13), {10.0f, cardH - 11.0f - 30.0f}, TextMuted, FontWeight::SemiBold);
        fitWrappedMenuLabel(desc, i18n::text(spec.descKey, spec.descFallback), cardW - 20.0f, cardH * .45f, css(13));
        desc->setAlignment(kCCTextAlignmentLeft);desc->setAnchorPoint({0, 1});
        if (i == 3) desc->setPositionY(cardH - 11.0f - 46.0f);
        body->addChild(desc, 5);
        auto* item = CCMenuItemSpriteExtra::create(body, this, menu_selector(SurvivalDifficultyVote::onCard));
        item->setTag(static_cast<int>(i));
        auto const x = 14.0f + cardW / 2 + static_cast<float>(i) * (cardW + gap);
        auto const y = bottom + cardH / 2;
        item->setPosition({x, y});m_menu->addChild(item);
        std::vector<std::pair<CCLayerColor*, GLubyte>> layers;
        layers.emplace_back(chrome, 255);
        for (auto* child : CCArrayExt<CCNode*>(outline->getChildren()))
            if (auto* layer = typeinfo_cast<CCLayerColor*>(child)) layers.emplace_back(layer, 255);
        layers.emplace_back(pickFill, 255);
        m_cards[i] = {item, body, chrome, outline, pick, std::move(layers), y};
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
        if (pending) scale = 1.03f;
        else if (chosen && m_stage != SurvivalVoteStage::Choosing)
            scale = 1.0f + .06f * std::sin(std::min(m_pulse / .45f, 1.0f) * 3.14159265f) + .06f * std::clamp((m_leave - .45f) / .20f, 0.0f, 1.0f);
        card.item->setScale(scale);
        card.item->setEnabled(m_stage == SurvivalVoteStage::Choosing);
    }
    // The page (header, line, footer) follows the title timing; the ink
    // ground leaves with the board's return so the standings take over.
    auto const pageOpacity = static_cast<GLubyte>(std::clamp(std::min(m_entry / .32f, 1.0f - leave), 0.0f, 1.0f) * 255);
    propagateModeOpacity(m_page, pageOpacity);
    for (auto const& [layer, base] : m_pageLayers) layer->setOpacity(static_cast<GLubyte>(base * pageOpacity / 255));
    auto const groundAlpha = m_stage == SurvivalVoteStage::Choosing ? std::clamp(m_entry / .25f, 0.0f, 1.0f)
        : 1.0f - std::clamp((m_leave - .25f) / .45f, 0.0f, 1.0f);
    m_ground->setOpacity(static_cast<GLubyte>(groundAlpha * 255));
    m_title->setPositionY(getContentSize().height - 45.0f - (1.0f - std::clamp(m_entry / .42f, 0.0f, 1.0f)) * 8.0f);
    for (std::size_t i = 0; i < m_cards.size(); ++i) {
        auto const picked = static_cast<int>(i) == (m_pending >= 0 ? m_pending : m_chosen);
        m_cards[i].outline->setVisible(picked);m_cards[i].pick->setVisible(picked);
    }
    auto const secondsLeft = survivalVoteSecondsLeft(closesInMs);
    // Votes stay private: neither who voted nor how many is shown; the
    // header names only the matched field.
    (void)voted;
    if (total != m_matchedTotal) {
        m_matchedTotal = total;
        m_matched->setString(total > 0 ? fmt::format(fmt::runtime(i18n::text("survival-vote-matched", "{} PLAYERS MATCHED")), total).c_str() : "");
        fitMenuLabelWidth(m_matched, getContentSize().width * .3f, 13.0f / 64.0f, .1f);
    }
    m_timer->setString(fmt::format("0:{:02}", std::clamp(secondsLeft, 0, 59)).c_str());
    // Last seconds of the window: the clock turns red.
    m_timer->setColor(m_stage == SurvivalVoteStage::Choosing && secondsLeft <= SurvivalVoteUrgentSeconds ? Red : Text);
    std::string status;
    if (m_pending >= 0) status = fmt::format(fmt::runtime(i18n::text("survival-vote-confirming", "CONFIRMING IN {}s · TAP ANOTHER CARD TO CHANGE")),
        std::max(1, static_cast<int>(std::ceil(SurvivalVoteConfirmSeconds - m_pendingFor))));
    if (status != m_statusText) { m_statusText = status;m_status->setString(status.c_str());fitMenuLabelWidth(m_status, getContentSize().width - 272.0f - m_barWidth - 8.0f, 16.0f / 64.0f, .12f); }
    // Confirm bar: how far the pending pick is towards being sent.
    m_bar->setVisible(m_pending >= 0);
    m_barFill->setContentSize({m_barWidth * std::clamp(m_pendingFor / SurvivalVoteConfirmSeconds, 0.0f, 1.0f), 6.0f});
}

} // namespace corum::ranked::ui
