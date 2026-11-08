#include "SurvivalEliminationFx.hpp"

#include <Geode/cocos/actions/CCActionEase.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "../domain/CompetitiveHudLayout.hpp"

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

constexpr float kPi = 3.14159265f;

CCPoint unit(CCPoint value) {
    auto const length = std::sqrt(value.x * value.x + value.y * value.y);
    return length > 0.001f ? CCPoint{value.x / length, value.y / length} : CCPoint{0, -1};
}

// One clipped copy of the victim icon; the mask is a polygon around the icon centre.
CCNode* iconPiece(EliminationFxTarget const& target, std::vector<CCPoint> polygon) {
    auto* mask = CCDrawNode::create();
    mask->drawPolygon(polygon.data(), static_cast<unsigned int>(polygon.size()), {1, 1, 1, 1}, 0, {1, 1, 1, 1});
    auto* clip = CCClippingNode::create(mask);
    clip->setPosition(target.center);
    if (auto* icon = target.icon ? target.icon() : nullptr) {
        icon->setScale(target.iconScale);
        icon->setPosition(CCPointZero);
        clip->addChild(icon);
    }
    target.host->addChild(clip, 20);
    return clip;
}

// Thin light bar along `direction` (a gold glow under a white core).
void slashLine(CCNode* host, CCPoint center, CCPoint direction, float length) {
    auto const angle = -std::atan2(direction.y, direction.x) * 180.0f / kPi;
    struct Layer { float width; ccColor4B color; };
    for (auto const& layer : {Layer{7.0f, {255, 211, 78, 120}}, Layer{2.5f, {255, 255, 255, 255}}}) {
        auto* bar = CCLayerColor::create(layer.color, length, layer.width);
        bar->ignoreAnchorPointForPosition(false);
        bar->setAnchorPoint({0.5f, 0.5f});
        bar->setPosition(center);
        bar->setRotation(angle);
        bar->setScaleX(0.15f);
        host->addChild(bar, 22);
        bar->runAction(CCSequence::create(CCScaleTo::create(0.07f, 1.0f, 1.0f), CCFadeOut::create(0.2f), CCRemoveSelf::create(), nullptr));
    }
}

CCDrawNode* disc(float radius, ccColor4F color) {
    auto* node = CCDrawNode::create();
    std::vector<CCPoint> points;
    for (int i = 0; i < 18; ++i) {
        auto const a = 2.0f * kPi * static_cast<float>(i) / 18.0f;
        points.push_back({std::cos(a) * radius, std::sin(a) * radius});
    }
    node->drawPolygon(points.data(), static_cast<unsigned int>(points.size()), color, 0, color);
    return node;
}

void flash(CCNode* host, CCPoint center, float radius, ccColor4F color) {
    auto* node = disc(radius, color);
    node->setPosition(center);
    node->setScale(0.4f);
    host->addChild(node, 21);
    node->runAction(CCSequence::create(CCScaleTo::create(0.1f, 1.4f), CCScaleTo::create(0.08f, 0.2f), CCRemoveSelf::create(), nullptr));
}

void sparks(CCNode* host, CCPoint center, int count, ccColor4B color, float distance) {
    for (int i = 0; i < count; ++i) {
        auto const a = 2.0f * kPi * (static_cast<float>(i) + 0.3f) / static_cast<float>(count);
        auto* spark = CCLayerColor::create(color, 4, 4);
        spark->ignoreAnchorPointForPosition(false);
        spark->setAnchorPoint({0.5f, 0.5f});
        spark->setPosition(center);
        spark->setRotation(45);
        host->addChild(spark, 21);
        spark->runAction(CCSequence::create(CCSpawn::create(
            CCEaseSineOut::create(CCMoveBy::create(0.3f, {std::cos(a) * distance, std::sin(a) * distance})),
            CCFadeOut::create(0.3f), nullptr), CCRemoveSelf::create(), nullptr));
    }
}

// A short jagged bolt just above the icon, polygons only; sized from the
// icon scale so it stays small on compact cards.
void bolt(CCNode* host, CCPoint center, int order, float iconScale) {
    auto* node = CCDrawNode::create();
    std::vector<CCPoint> path;
    auto const top = 46.0f;
    for (int i = 0; i <= 4; ++i) {
        auto const t = static_cast<float>(i) / 4.0f;
        auto const jitter = i == 0 || i == 4 ? 0.0f : ((i + order) % 2 ? 5.0f : -5.0f);
        path.push_back({jitter, top * (1.0f - t)});
    }
    auto quad = [&](CCPoint a, CCPoint b, float width, ccColor4F color) {
        auto const n = unit({-(b.y - a.y), b.x - a.x});
        CCPoint q[] {{a.x + n.x * width, a.y + n.y * width}, {b.x + n.x * width, b.y + n.y * width},
            {b.x - n.x * width, b.y - n.y * width}, {a.x - n.x * width, a.y - n.y * width}};
        node->drawPolygon(q, 4, color, 0, color);
    };
    for (std::size_t i = 0; i + 1 < path.size(); ++i) quad(path[i], path[i + 1], 2.0f, {1.0f, 0.84f, 0.3f, 0.85f});
    for (std::size_t i = 0; i + 1 < path.size(); ++i) quad(path[i], path[i + 1], 0.8f, {1, 1, 1, 1});
    node->setPosition(center);
    node->setScale(std::clamp(iconScale / 0.65f, 0.6f, 1.0f));
    host->addChild(node, 22);
    node->runAction(CCSequence::create(CCBlink::create(0.16f, 2), CCRemoveSelf::create(), nullptr));
}

} // namespace

float eliminationFxSeconds() { return SurvivalEliminationFallMilliseconds / 1000.0f; }

void playSliceFx(EliminationFxTarget const& target, float fall, CCPoint swipe) {
    if (!target.slot || !target.host) return;
    target.slot->setVisible(false);
    auto const d = unit(swipe), n = CCPoint{-d.y, d.x};
    auto const r = 60.0f;
    CCPoint const p1{d.x * r, d.y * r}, p2{-d.x * r, -d.y * r};
    auto const duration = eliminationFxSeconds();
    for (int side : {1, -1}) {
        auto const off = CCPoint{n.x * r * static_cast<float>(side), n.y * r * static_cast<float>(side)};
        auto* half = iconPiece(target, {p1, p2, {p2.x + off.x, p2.y + off.y}, {p1.x + off.x, p1.y + off.y}});
        auto const push = CCPoint{n.x * 16.0f * static_cast<float>(side), n.y * 16.0f * static_cast<float>(side)};
        // Push apart, hold a beat so the cut reads, then fall.
        half->runAction(CCSequence::create(
            CCEaseSineOut::create(CCMoveBy::create(0.12f, push)),
            CCDelayTime::create(0.1f),
            CCSpawn::create(
                CCEaseSineIn::create(CCMoveBy::create(duration - 0.22f, {push.x * 1.4f, -fall})),
                CCRotateBy::create(duration - 0.22f, side > 0 ? 70.0f : -70.0f),
                CCScaleTo::create(duration - 0.22f, 0.45f), nullptr),
            CCRemoveSelf::create(), nullptr));
    }
    slashLine(target.host, target.center, d, 74.0f);
    sparks(target.host, target.center, 5, {255, 240, 190, 255}, 26.0f);
}

void playDefaultFall(CCNode* slot, int order, float fall) {
    if (!slot) return;
    auto const seconds = eliminationFxSeconds();
    slot->runAction(CCSequence::create(CCSpawn::create(
        CCEaseSineIn::create(CCMoveBy::create(seconds, {order % 4 < 2 ? -90.0f : 90.0f, -fall})),
        CCRotateBy::create(seconds, order % 2 ? 270.0f : -270.0f), CCScaleTo::create(seconds, 0.18f), nullptr),
        CCHide::create(), nullptr));
}

void runSliceGhost(CCNode* ghost, CCPoint victim, CCPoint home) {
    if (!ghost) return;
    ghost->stopAllActions();
    ghost->setVisible(true);
    ghost->runAction(CCSequence::create(
        CCEaseSineOut::create(CCMoveTo::create(kSliceCutDelay, {victim.x + kSliceEnter.x, victim.y + kSliceEnter.y})),
        CCMoveTo::create(0.06f, {victim.x + kSliceLeave.x, victim.y + kSliceLeave.y}),
        CCDelayTime::create(0.2f),
        CCEaseSineInOut::create(CCMoveTo::create(0.24f, home)),
        CCRemoveSelf::create(), nullptr));
}

void scheduleSliceCut(CCNode* host, std::function<void()> cut) {
    if (!host) return;
    host->runAction(CCSequence::create(CCDelayTime::create(kSliceCutDelay), CallFuncExt::create(std::move(cut)), nullptr));
}

void playEliminationFx(EliminationEffect effect, EliminationFxTarget const& target, float fall, CCPoint executor) {
    if (!target.slot || !target.host) return;
    auto const total = eliminationFxSeconds();
    auto* slot = target.slot;
    switch (effect) {
    case EliminationEffect::Bolt: {
        // Simplified: a short bolt, a small flash, four sparks, then the icon
        // shrinks away upward.
        bolt(target.host, target.center, target.order, target.iconScale);
        flash(target.host, target.center, 11.0f, {1, 1, 0.85f, 0.8f});
        sparks(target.host, target.center, 4, {255, 214, 90, 255}, 16.0f);
        slot->runAction(CCSequence::create(CCDelayTime::create(0.06f),
            CCSpawn::create(CCEaseSineIn::create(CCScaleTo::create(total - 0.06f, 0.0f)), CCMoveBy::create(total - 0.06f, {0, 14}), nullptr),
            CCHide::create(), nullptr));
        break;
    }
    case EliminationEffect::Shatter: {
        slot->setVisible(false);
        flash(target.host, target.center, 16.0f, {1, 1, 1, 0.8f});
        auto const pieces = 5;
        auto const start = static_cast<float>(target.order % 3) * 0.4f;
        auto const r = 60.0f;
        for (int i = 0; i < pieces; ++i) {
            auto const a0 = start + 2.0f * kPi * static_cast<float>(i) / pieces;
            auto const a1 = start + 2.0f * kPi * static_cast<float>(i + 1) / pieces;
            auto const am = (a0 + a1) / 2.0f;
            auto* shard = iconPiece(target, {{0, 0}, {std::cos(a0) * r, std::sin(a0) * r}, {std::cos(am) * r * 1.2f, std::sin(am) * r * 1.2f},
                {std::cos(a1) * r, std::sin(a1) * r}});
            CCPoint const out{std::cos(am), std::sin(am)};
            shard->runAction(CCSequence::create(CCSpawn::create(
                CCSequence::create(CCEaseSineOut::create(CCMoveBy::create(0.16f, {out.x * 20.0f, out.y * 20.0f})),
                    CCEaseSineIn::create(CCMoveBy::create(total - 0.16f, {out.x * 16.0f, -fall})), nullptr),
                CCRotateBy::create(total, (i % 2 ? 1.0f : -1.0f) * (110.0f + 30.0f * static_cast<float>(i))),
                CCScaleTo::create(total, 0.5f), nullptr), CCRemoveSelf::create(), nullptr));
        }
        break;
    }
    case EliminationEffect::Vortex: {
        auto const from = slot->getPosition();
        auto const to = slot->getParent() ? slot->getParent()->convertToNodeSpace(target.host->convertToWorldSpace(executor)) : executor;
        auto const delta = CCPoint{to.x - from.x, to.y - from.y};
        auto const side = unit({-delta.y, delta.x});
        auto const swing = std::clamp(std::sqrt(delta.x * delta.x + delta.y * delta.y) * 0.45f, 40.0f, 140.0f);
        ccBezierConfig path;
        path.controlPoint_1 = CCPoint{from.x + side.x * swing, from.y + side.y * swing};
        path.controlPoint_2 = CCPoint{to.x - side.x * swing * 0.6f, to.y - side.y * swing * 0.6f};
        path.endPosition = to;
        sparks(target.host, target.center, 6, {176, 132, 255, 255}, 22.0f);
        slot->runAction(CCSequence::create(CCSpawn::create(
            CCEaseSineIn::create(CCBezierTo::create(total, path)),
            CCRotateBy::create(total, target.order % 2 ? -720.0f : 720.0f),
            CCSequence::create(CCScaleTo::create(total * 0.6f, 0.7f), CCScaleTo::create(total * 0.4f, 0.0f), nullptr), nullptr),
            CCHide::create(), nullptr));
        break;
    }
    default: break;
    }
}

} // namespace corum::ranked::ui
