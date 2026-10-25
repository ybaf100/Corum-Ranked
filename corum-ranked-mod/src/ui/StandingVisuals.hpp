#pragma once

#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "../domain/ResultScorePresentation.hpp"

namespace corum::ranked::ui {
using namespace geode::prelude;

inline ccColor3B standingColor(int rank) {
    if (rank == 1) return Gold;
    if (rank == 2) return ccc3(240, 247, 255);
    if (rank == 3) return ccc3(225, 144, 69);
    return rank > 3 ? ccc3(16, 20, 28) : TextMuted;
}

inline std::string topPercentText(StandingView const& standing) {
    if (!standing.rank || !standing.topPercent) return "Top —%";
    if (*standing.topPercent < 0.01) return "Top <0.01%";
    auto percent = fmt::format("{:.2f}", *standing.topPercent);
    while (percent.back() == '0') percent.pop_back();
    if (percent.back() == '.') percent.pop_back();
    return "Top " + percent + "%";
}

inline CCNode* standingNumber(std::optional<int> rank, float scale) {
    auto* root = CCNode::create();
    auto const text = rank ? fmt::format("#{}", *rank) : "#—";
    auto* front = menuLabel(text, scale, {}, standingColor(rank.value_or(0)), FontWeight::Black);
    auto const size = CCSize {front->getContentSize().width * front->getScaleX(),
        front->getContentSize().height * front->getScaleY()};
    root->setContentSize(size); root->setAnchorPoint({0.5f, 0.5f});
    auto const center = ccp(size.width / 2, size.height / 2);
    // The drawing uses black for #4 onward. A light outline keeps it readable
    // over Corum's dark panels without replacing the requested rank color.
    if (rank && *rank >= 4) {
        for (auto const offset : {ccp(-0.65f,0),ccp(0.65f,0),ccp(0,-0.65f),ccp(0,0.65f),
            ccp(-0.5f,-0.5f),ccp(0.5f,-0.5f),ccp(-0.5f,0.5f),ccp(0.5f,0.5f)}) {
            auto* stroke = menuLabel(text, scale, center + offset, ccc3(224,230,238), FontWeight::Black);
            root->addChild(stroke, 0);
        }
    }
    front->setPosition(center); root->addChild(front, 1);
    return root;
}

inline CCNode* standingLine(StandingView const& standing, float scale, std::optional<int> lp = std::nullopt) {
    auto* root = CCNode::create(); root->setAnchorPoint({0.5f, 0.5f});
    auto* rank = standingNumber(standing.rank, scale);
    auto const total = fmt::format(fmt::runtime(i18n::text("players-total-suffix"," ({} players)")),standing.total);
    auto* text = menuLabel(lp ? fmt::format("{} LP /", *lp) : topPercentText(standing) + total,
        scale, {}, Text, FontWeight::ExtraBold);
    auto const textSize = CCSize {text->getContentSize().width * text->getScaleX(),
        text->getContentSize().height * text->getScaleY()};
    auto const size = CCSize {rank->getContentSize().width + 5 + textSize.width,
        std::max(rank->getContentSize().height, textSize.height)};
    root->setContentSize(size);
    if (lp) {
        text->setPosition({textSize.width / 2, size.height / 2});
        rank->setPosition({textSize.width + 5 + rank->getContentSize().width / 2, size.height / 2});
    } else {
        rank->setPosition({rank->getContentSize().width / 2, size.height / 2});
        text->setPosition({rank->getContentSize().width + 5 + textSize.width / 2, size.height / 2});
    }
    root->addChild(rank); root->addChild(text);
    return root;
}

} // namespace corum::ranked::ui
