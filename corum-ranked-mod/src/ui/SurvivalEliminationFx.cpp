#include "SurvivalEliminationFx.hpp"

#include <Geode/cocos/actions/CCActionEase.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <vector>
#include "../domain/CompetitiveHudLayout.hpp"
#include "../domain/SurvivalSlicePlan.hpp"
#include "PlayerIcon.hpp"
#include "RankedVisuals.hpp"
#include "../RankedVolumeControl.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>

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
    // Sharp: a thin gold glow under a hairline white core, gone quickly.
    for (auto const& layer : {Layer{3.6f, {255, 211, 78, 120}}, Layer{1.4f, {255, 255, 255, 255}}}) {
        auto* bar = CCLayerColor::create(layer.color, length, layer.width);
        bar->ignoreAnchorPointForPosition(false);
        bar->setAnchorPoint({0.5f, 0.5f});
        bar->setPosition(center);
        bar->setRotation(angle);
        bar->setScaleX(0.15f);
        host->addChild(bar, 22);
        bar->runAction(CCSequence::create(CCScaleTo::create(0.05f, 1.0f, 1.0f), CCFadeOut::create(0.14f), CCRemoveSelf::create(), nullptr));
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

void playSliceFx(EliminationFxTarget const& target, float fall, CCPoint swipe, bool finale) {
    if (!target.slot || !target.host) return;
    target.slot->setVisible(false);
    auto const d = unit(swipe), n = CCPoint{-d.y, d.x};
    auto const r = 60.0f;
    CCPoint const p1{d.x * r, d.y * r}, p2{-d.x * r, -d.y * r};
    auto const duration = finale ? 0.95f : 0.65f;
    // The hit: a white flash on the icon, then the halves pop apart and spin away.
    flash(target.host, target.center, (finale ? 14.0f : 8.0f) * std::max(0.6f, target.iconScale), {1, 1, 1, 0.75f});
    for (int side : {1, -1}) {
        auto const off = CCPoint{n.x * r * static_cast<float>(side), n.y * r * static_cast<float>(side)};
        auto* half = iconPiece(target, {p1, p2, {p2.x + off.x, p2.y + off.y}, {p1.x + off.x, p1.y + off.y}});
        auto const push = CCPoint{n.x * 16.0f * static_cast<float>(side), n.y * 16.0f * static_cast<float>(side)};
        auto const spin = side > 0 ? 200.0f : -190.0f;
        half->runAction(CCSequence::create(
            CCSpawn::create(CCEaseSineOut::create(CCMoveBy::create(0.065f, push)), CCRotateBy::create(0.065f, spin * 0.11f),
                CCScaleTo::create(0.065f, target.iconScale * 1.08f), nullptr),
            CCSpawn::create(
                CCEaseSineIn::create(CCMoveBy::create(duration - 0.065f, {push.x * 3.4f, -fall})),
                CCRotateBy::create(duration - 0.065f, spin),
                CCScaleTo::create(duration - 0.065f, target.iconScale * 0.5f), nullptr),
            CCRemoveSelf::create(), nullptr));
    }
    auto const reach = 52.0f * std::max(0.6f, target.iconScale);
    slashLine(target.host, target.center, d, finale ? reach * 2.6f : reach * 1.5f);
    if (finale) slashLine(target.host, target.center, d, reach * 1.9f);
    sparks(target.host, target.center, 5, {255, 240, 190, 255}, finale ? 58.0f : 28.0f);
}

void playDefaultFall(CCNode* slot, int order, float fall) {
    if (!slot) return;
    auto const seconds = eliminationFxSeconds();
    slot->runAction(CCSequence::create(CCSpawn::create(
        CCEaseSineIn::create(CCMoveBy::create(seconds, {order % 4 < 2 ? -90.0f : 90.0f, -fall})),
        CCRotateBy::create(seconds, order % 2 ? 270.0f : -270.0f), CCScaleTo::create(seconds, 0.18f), nullptr),
        CCHide::create(), nullptr));
}

namespace {
constexpr int kShakeTag = 7481;

// The GD crash sound at the Ranked volume (pitched per hit).
void playSlashSound(float speed, float volume) {
    auto const gain = RankedVolumeControl::get().volume();
    if (gain <= 0) return;
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->playEffect("explode_11.ogg", speed, 0.0f, volume * gain);
}

// A short shake of `target` around `base` along `axis` (the hit's push,
// perpendicular to the blade), stopping the previous shake first.
void shakeNode(CCNode* target, CCPoint base, CCPoint axis, float amplitude, int steps) {
    if (!target) return;
    target->stopActionByTag(kShakeTag);
    target->setPosition(base);
    auto* actions = CCArray::create();
    for (int i = 0; i < steps; ++i) {
        auto const fade = 1.0f - static_cast<float>(i) / static_cast<float>(steps);
        auto const sign = i % 2 ? -1.0f : 1.0f;
        // A little cross jitter keeps the big ones from reading as a slide.
        auto const cross = (i % 3 == 1 ? 0.35f : -0.2f) * amplitude * fade;
        actions->addObject(CCMoveTo::create(0.03f, {base.x + axis.x * amplitude * fade * sign - axis.y * cross,
            base.y + axis.y * amplitude * fade * sign + axis.x * cross}));
    }
    actions->addObject(CCMoveTo::create(0.03f, base));
    auto* sequence = CCSequence::create(actions);
    sequence->setTag(kShakeTag);
    target->runAction(sequence);
}

// The finale: a gold shockwave ring and a short screen flash.
void finaleBurst(CCNode* stage, CCPoint center, float size) {
    auto* ring = CCDrawNode::create();
    constexpr int segments = 40;
    for (int i = 0; i < segments; ++i) {
        auto const a0 = 2.0f * kPi * static_cast<float>(i) / segments, a1 = 2.0f * kPi * static_cast<float>(i + 1) / segments;
        CCPoint q[] {{std::cos(a0) * 14.0f, std::sin(a0) * 14.0f}, {std::cos(a1) * 14.0f, std::sin(a1) * 14.0f},
            {std::cos(a1) * 15.5f, std::sin(a1) * 15.5f}, {std::cos(a0) * 15.5f, std::sin(a0) * 15.5f}};
        ring->drawPolygon(q, 4, {1.0f, 0.9f, 0.55f, 0.95f}, 0, {1.0f, 0.9f, 0.55f, 0.95f});
    }
    ring->setPosition(center);
    ring->setScale(0.3f);
    stage->addChild(ring, 150);
    ring->runAction(CCSequence::create(CCEaseSineOut::create(CCScaleTo::create(0.38f, 3.2f * size)), CCRemoveSelf::create(), nullptr));
    auto const area = stage->getContentSize();
    auto* white = CCLayerColor::create({255, 248, 224, 0}, area.width, area.height);
    stage->addChild(white, 300);
    white->runAction(CCSequence::create(CCFadeTo::create(0.04f, 80), CCFadeTo::create(0.28f, 0), CCRemoveSelf::create(), nullptr));
}

CCPoint point(SliceVec v) { return {v.x, v.y}; }

// Long-exposure light trail (like car lights in a night road photo): the
// icon's recent path as a tapered gold glow with a white core. Short: only
// the last `life` seconds stay, so it hugs the icon during the flash dashes.
class LightTrail final : public CCNode {
public:
    static LightTrail* create(CCNode* target, float life, float width) {
        auto* node = new LightTrail();
        if (!node->init()) { delete node; return nullptr; }
        node->autorelease();
        node->m_target.emplace(target);
        node->m_life = life;
        node->m_width = width;
        node->m_draw = CCDrawNode::create();
        node->addChild(node->m_draw);
        node->scheduleUpdate();
        return node;
    }
    void update(float dt) override {
        if (m_done) return;
        m_time += dt;
        auto alive = false;
        if (m_target) if (auto node = m_target->lock(); node && node->getParent()) {
            alive = true;
            auto const p = node->getPosition();
            if (m_points.empty() || ccpDistance(m_points.back().at, p) > 0.5f) m_points.push_back({m_time, p});
            else m_points.back().time = m_time;
        }
        while (!m_points.empty() && m_time - m_points.front().time > m_life) m_points.pop_front();
        m_draw->clear();
        if (!alive && m_points.size() < 2) {
            m_done = true;
            runAction(CCRemoveSelf::create());
            return;
        }
        struct Layer { float width; ccColor4F color; };
        for (auto const& layer : {Layer{1.0f, {1.0f, 0.83f, 0.31f, 0.38f}}, Layer{0.32f, {1.0f, 0.97f, 0.85f, 0.9f}}}) {
            for (std::size_t i = 0; i + 1 < m_points.size(); ++i) {
                auto const& a = m_points[i];
                auto const& b = m_points[i + 1];
                auto const fa = std::clamp((m_time - a.time) / m_life, 0.0f, 1.0f), fb = std::clamp((m_time - b.time) / m_life, 0.0f, 1.0f);
                auto const wa = m_width * layer.width * (1.0f - fa) / 2.0f, wb = m_width * layer.width * (1.0f - fb) / 2.0f;
                auto const d = unit({b.at.x - a.at.x, b.at.y - a.at.y});
                CCPoint const n{-d.y, d.x};
                CCPoint quad[] {{a.at.x + n.x * wa, a.at.y + n.y * wa}, {b.at.x + n.x * wb, b.at.y + n.y * wb},
                    {b.at.x - n.x * wb, b.at.y - n.y * wb}, {a.at.x - n.x * wa, a.at.y - n.y * wa}};
                auto color = layer.color;
                color.a *= 1.0f - (fa + fb) / 2.0f;
                m_draw->drawPolygon(quad, 4, color, 0, color);
            }
        }
    }
private:
    struct Sample { float time; CCPoint at; };
    std::optional<WeakRef<CCNode>> m_target;
    std::deque<Sample> m_points;
    CCDrawNode* m_draw = nullptr;
    float m_time = 0, m_life = 0.12f, m_width = 10;
    bool m_done = false;
};

// Cocos rotation is clockwise: lean into the dash direction.
float lean(SliceVec dir, float degrees) { return dir.x >= 0 ? degrees : -degrees; }

// Executor `e`'s motion: out of its card, over the field, one flash dash per
// victim it cuts (the second of two joint executors half a dash later), the
// finale wind-up, strike, hit-stop, follow-through, and the flight home.
CCSequence* executorActions(SliceGatherExecutor const& executor, SliceMusouPlan const& plan, std::size_t e, float big, float lift,
    float follow, std::function<void()> done) {
    auto* actions = CCArray::create();
    auto const start = plan.start[e], windup = plan.windup[e], through = plan.through[e], finaleDir = plan.finaleDir[e];
    actions->addObject(CCSpawn::create(CCEaseSineOut::create(CCMoveBy::create(SliceLift, {0, lift})),
        CCEaseBackOut::create(CCScaleTo::create(SliceLift, big)), nullptr));
    CCPoint const raised{executor.home.x, executor.home.y + lift};
    ccBezierConfig over;
    over.controlPoint_1 = CCPoint{raised.x + (start.x - raised.x) * 0.3f, std::max(raised.y, start.y) + lift * 3.0f};
    over.controlPoint_2 = CCPoint{raised.x + (start.x - raised.x) * 0.7f, std::max(raised.y, start.y) + lift * 2.0f};
    over.endPosition = point(start);
    actions->addObject(CCSpawn::create(CCEaseSineInOut::create(CCBezierTo::create(SliceTravel, over)),
        CCSequence::create(CCRotateTo::create(SliceTravel / 2, start.x >= raised.x ? 20.0f : -20.0f), CCRotateTo::create(SliceTravel / 2, 0.0f), nullptr), nullptr));
    // A short charge before the first dash.
    auto const charge = SliceCutStartMs / 1000.0f - SliceLift - SliceTravel;
    actions->addObject(CCSequence::create(
        CCSpawn::create(CCEaseSineOut::create(CCMoveBy::create(charge / 2, {0, lift * 0.5f})), CCScaleTo::create(charge / 2, big * 1.12f), CCRotateTo::create(charge / 2, -18.0f), nullptr),
        CCSpawn::create(CCEaseSineIn::create(CCMoveBy::create(charge / 2, {0, -lift * 0.5f})), CCScaleTo::create(charge / 2, big), CCRotateTo::create(charge / 2, 0.0f), nullptr), nullptr));
    // Its dashes back to back from its first start; then it waits for the
    // other executor before the finale.
    auto clock = SliceCutStartMs / 1000.0f;
    for (auto const& dash : plan.dashes) {
        if (dash.executor != e) continue;
        if (dash.start > clock + 0.0005f) actions->addObject(CCDelayTime::create(dash.start - clock));
        actions->addObject(CCSpawn::create(CCEaseExponentialOut::create(CCMoveTo::create(plan.dash, point(dash.to))),
            CCSequence::create(CCRotateTo::create(plan.dash * 0.3f, lean(dash.dir, 28.0f)), CCRotateTo::create(plan.dash * 0.7f, lean(dash.dir, 8.0f)), nullptr),
            CCSequence::create(CCScaleTo::create(plan.dash * 0.3f, big * 1.12f), CCScaleTo::create(plan.dash * 0.7f, big), nullptr), nullptr));
        clock = dash.start + plan.dash;
    }
    if (plan.windupAt > clock + 0.0005f) actions->addObject(CCDelayTime::create(plan.windupAt - clock));
    // Finale: draw back and charge, strike, freeze on the hit, cut through.
    auto const target = point(plan.field[plan.finale]);
    actions->addObject(CCSpawn::create(CCEaseSineInOut::create(CCMoveTo::create(SliceFinalWindup, point(windup))),
        CCScaleTo::create(SliceFinalWindup, big * 1.25f), CCRotateTo::create(SliceFinalWindup, lean(finaleDir, -40.0f)), nullptr));
    actions->addObject(CCEaseSineIn::create(CCMoveTo::create(SliceFinalStrike, target)));
    actions->addObject(CCDelayTime::create(SliceHitStop));
    actions->addObject(CCSpawn::create(CCEaseOut::create(CCMoveTo::create(SliceFinalThrough, point(through)), 3.0f),
        CCRotateTo::create(SliceFinalThrough, lean(finaleDir, 50.0f)), nullptr));
    actions->addObject(CCSpawn::create(CCEaseSineOut::create(CCMoveBy::create(SliceFollow, {finaleDir.x * follow, finaleDir.y * follow})),
        CCScaleTo::create(SliceFollow, big), CCRotateTo::create(SliceFollow, 0.0f), nullptr));
    // Home along an arc.
    CCPoint const after{through.x + finaleDir.x * follow, through.y + finaleDir.y * follow};
    ccBezierConfig back;
    back.controlPoint_1 = CCPoint{after.x + (executor.home.x - after.x) * 0.3f, std::max(after.y, executor.home.y) + lift * 4.0f};
    back.controlPoint_2 = CCPoint{after.x + (executor.home.x - after.x) * 0.75f, std::max(after.y, executor.home.y) + lift * 2.5f};
    back.endPosition = executor.home;
    actions->addObject(CCSpawn::create(CCEaseSineInOut::create(CCBezierTo::create(SliceReturn, back)),
        CCScaleTo::create(SliceReturn, executor.iconScale),
        CCSequence::create(CCRotateTo::create(SliceReturn / 2, executor.home.x < after.x ? -25.0f : 25.0f), CCRotateTo::create(SliceReturn / 2, 0.0f), nullptr), nullptr));
    if (done) actions->addObject(CallFuncExt::create(std::move(done)));
    actions->addObject(CCRemoveSelf::create());
    return CCSequence::create(actions);
}
} // namespace

float sliceGatherSeconds(std::size_t victims, std::size_t executors) {
    return static_cast<float>(executors >= 2 ? sliceJointMilliseconds(victims) : sliceMusouMilliseconds(victims)) / 1000.0f;
}

void runSliceGathering(CCNode* layer, std::vector<SliceGatherExecutor> const& executors, std::vector<SliceGatherVictim> const& victims,
    CCPoint center, CCSize spread, float fall, CCNode* shakeTarget) {
    if (!layer || victims.empty() || executors.empty()) return;
    auto const& executor = executors.front();
    auto const joint = std::min<std::size_t>(executors.size(), 2);
    auto* stage = CCNode::create();
    stage->setContentSize(layer->getContentSize());
    layer->addChild(stage, 60);
    auto const n = victims.size();
    auto const area = layer->getContentSize();
    // On compact 64-player cards the icons are tiny: the scattered icons and
    // the gold icon grow while they are off the cards so every cut reads.
    auto const fieldScale = std::clamp(executor.iconScale * 2.0f, std::max(executor.iconScale, 0.45f), 1.0f);
    auto const big = fieldScale * 1.3f;
    auto const iconSize = 32.0f * fieldScale;
    auto const plan = sliceMusouPlan(n, joint, {center.x, center.y}, {spread.width, spread.height}, iconSize * 1.25f, iconSize * 0.95f,
        std::min(iconSize * 3.8f, std::max(spread.width, spread.height) * 0.9f), {iconSize * 0.8f, iconSize * 0.8f},
        {area.width - iconSize * 0.8f, area.height - iconSize * 0.8f});
    auto const lift = iconSize * 0.45f;
    auto const follow = iconSize * 0.7f;
    auto const end = plan.home;
    // A dark backdrop over the board while the field is cut; it lifts while
    // the icon flies home.
    auto* dim = CCLayerColor::create({8, 9, 11, 0}, area.width, area.height);
    stage->addChild(dim, -1000);
    auto const dimOut = plan.finaleCut + SliceHitStop + 0.25f;
    dim->runAction(CCSequence::create(CCDelayTime::create(0.6f), CCFadeTo::create(0.45f, 160),
        CCDelayTime::create(std::max(0.0f, dimOut - 1.05f)), CCFadeTo::create(std::max(0.2f, end - dimOut), 0), CCRemoveSelf::create(), nullptr));
    // Combo counter at the field's top right: x1, x2, ... punching on every cut.
    auto* counter = menuLabel("", 0.62f, {std::min(center.x + spread.width * 0.95f, area.width - 26.0f), std::min(center.y + spread.height + iconSize, area.height - 16.0f)},
        Gold, FontWeight::Black);
    counter->setVisible(false);
    stage->addChild(counter, 250);
    auto const counterScale = counter->getScale();
    std::optional<WeakRef<CCNode>> shake;
    CCPoint shakeBase{};
    if (shakeTarget && shakeTarget != layer) { shake.emplace(shakeTarget); shakeBase = shakeTarget->getPosition(); }
    auto const stageBase = stage->getPosition();

    std::vector<CCNode*> copies;
    for (std::size_t k = 0; k < n; ++k) {
        auto const& victim = victims[k];
        auto* copy = victim.icon ? victim.icon() : nullptr;
        if (!copy) copy = CCNode::create();
        copy->setScale(victim.iconScale);
        copy->setPosition(victim.from);
        stage->addChild(copy, 10);
        copies.push_back(copy);
        auto const to = point(plan.field[k]);
        ccBezierConfig arc;
        auto const top = std::max(victim.from.y, to.y) + iconSize * 2.2f;
        arc.controlPoint_1 = CCPoint{victim.from.x + (to.x - victim.from.x) * 0.25f, top};
        arc.controlPoint_2 = CCPoint{victim.from.x + (to.x - victim.from.x) * 0.75f, top};
        arc.endPosition = to;
        auto const spin = static_cast<float>(static_cast<int>(k * 29 % 40) - 20);
        auto const bob = iconSize * 0.08f + 1.0f;
        std::optional<WeakRef<CCNode>> slot;
        if (victim.slot) slot.emplace(victim.slot);
        auto detach = victim.detach;
        copy->runAction(CCSequence::create(CCDelayTime::create(plan.leave[k]),
            CallFuncExt::create([slot, detach]() mutable {
                if (slot) if (auto node = slot->lock()) node->setVisible(false);
                if (detach) detach();
            }),
            CCSpawn::create(CCEaseSineInOut::create(CCBezierTo::create(SliceVictimFlight, arc)),
                CCScaleTo::create(SliceVictimFlight, fieldScale), CCRotateBy::create(SliceVictimFlight, 360.0f + spin), nullptr),
            // Waiting to be cut: a slight bob.
            CCRepeat::create(CCSequence::create(CCEaseSineInOut::create(CCMoveBy::create(0.32f, {0, bob})),
                CCEaseSineInOut::create(CCMoveBy::create(0.32f, {0, -bob})), nullptr), 20),
            nullptr));
    }

    std::optional<WeakRef<CCNode>> stageRef;
    stageRef.emplace(stage);
    // One cut per dash: the halves split along the dash; a short shake
    // perpendicular to the blade (harder every fifth), a crash sound. One
    // shared counter, in cut order (two joint executors interleave).
    std::vector<std::size_t> byCut(plan.dashes.size());
    for (std::size_t j = 0; j < byCut.size(); ++j) byCut[j] = j;
    std::stable_sort(byCut.begin(), byCut.end(), [&](std::size_t a, std::size_t b) { return plan.dashes[a].cut < plan.dashes[b].cut; });
    for (std::size_t rank = 0; rank < byCut.size(); ++rank) {
        auto const& dash = plan.dashes[byCut[rank]];
        auto* copy = copies[dash.victim];
        EliminationFxTarget target{copy, stage, {}, fieldScale, victims[dash.victim].icon, static_cast<int>(rank)};
        auto const count = static_cast<int>(rank) + 1;
        auto const strong = count % 5 == 0;
        CCPoint const dir{dash.dir.x, dash.dir.y}, push{-dash.dir.y, dash.dir.x};
        stage->runAction(CCSequence::create(CCDelayTime::create(dash.cut), CallFuncExt::create(
            [target, copy, fall, dir, push, strong, count, counter, counterScale, shake, shakeBase, stageRef, stageBase]() mutable {
                target.center = copy->getPosition();
                playSliceFx(target, fall, dir, false);
                counter->setVisible(true);
                counter->setString(fmt::format("x{}", count).c_str());
                counter->stopAllActions();
                counter->setScale(counterScale * 1.32f);
                counter->runAction(CCEaseBackOut::create(CCScaleTo::create(0.12f, counterScale)));
                auto const amplitude = strong ? 4.5f : 2.5f;
                if (shake) if (auto node = shake->lock()) shakeNode(node.data(), shakeBase, push, amplitude, strong ? 4 : 3);
                if (stageRef) if (auto node = stageRef->lock()) shakeNode(node.data(), stageBase, push, amplitude * 0.8f, strong ? 4 : 3);
                playSlashSound(count % 2 ? 1.3f : 1.45f, strong ? 0.14f : 0.08f);
            }), nullptr));
    }
    // Finale: the blade stops on the last victim (hit-stop: a white flash and
    // the counter), then it splits with a long slash, ring, flash, hard shake.
    // Two joint executors strike it at once from opposite diagonals: an X.
    {
        auto const k = plan.finale;
        auto* copy = copies[k];
        EliminationFxTarget target{copy, stage, {}, fieldScale, victims[k].icon, static_cast<int>(n)};
        CCPoint const dir{plan.finaleDir[0].x, plan.finaleDir[0].y}, push{-plan.finaleDir[0].y, plan.finaleDir[0].x};
        std::optional<CCPoint> cross;
        if (plan.finaleDir.size() > 1) cross = CCPoint{plan.finaleDir[1].x, plan.finaleDir[1].y};
        auto const crossLength = 52.0f * std::max(0.6f, fieldScale) * 2.6f;
        auto const count = static_cast<int>(n);
        stage->runAction(CCSequence::create(CCDelayTime::create(plan.finaleCut), CallFuncExt::create([stage, copy, iconSize, counter, counterScale, count]() {
                flash(stage, copy->getPosition(), iconSize * 0.42f, {1, 1, 1, 0.9f});
                counter->setVisible(true);
                counter->setString(fmt::format("x{}", count).c_str());
                counter->stopAllActions();
                counter->setColor({255, 255, 255});
                counter->setScale(counterScale * 1.9f);
                counter->runAction(CCSequence::create(CCEaseBackOut::create(CCScaleTo::create(0.3f, counterScale * 1.35f)), CCDelayTime::create(0.45f), CCFadeOut::create(0.3f), nullptr));
            }),
            CCDelayTime::create(SliceHitStop),
            CallFuncExt::create([target, copy, fall, dir, push, cross, crossLength, stage, fieldScale, shake, shakeBase, stageRef, stageBase]() mutable {
                target.center = copy->getPosition();
                playSliceFx(target, fall, dir, true);
                if (cross) slashLine(stage, target.center, *cross, crossLength);
                finaleBurst(stage, target.center, std::max(0.7f, fieldScale));
                if (shake) if (auto node = shake->lock()) shakeNode(node.data(), shakeBase, push, 10.0f, 8);
                if (stageRef) if (auto node = stageRef->lock()) shakeNode(node.data(), stageBase, push, 8.0f, 8);
                playSlashSound(0.9f, 0.28f);
            }), nullptr));
    }

    // The executors: gold, out of their cards, flash-dashing through the
    // field, home, each leaving a short long-exposure light trail.
    for (std::size_t e = 0; e < joint; ++e) {
        auto const& owner = executors[e];
        auto* ghost = owner.icon ? owner.icon() : nullptr;
        if (!ghost) continue;
        if (auto* player = typeinfo_cast<PlayerIcon*>(ghost)) player->setGold(true);
        ghost->setScale(owner.iconScale);
        ghost->setPosition(owner.home);
        stage->addChild(ghost, 200);
        std::optional<WeakRef<CCNode>> home;
        auto const homeScale = owner.slot ? owner.slot->getScale() : 1.0f;
        if (owner.slot) { home.emplace(owner.slot); owner.slot->setVisible(false); }
        // Landing: the card icon shows again with a bump and a small flash.
        ghost->runAction(executorActions(owner, plan, e, big, lift, follow, [home, homeScale, stageRef, at = owner.home, iconSize]() mutable {
            if (home) if (auto node = home->lock()) {
                node->setVisible(true);
                node->setScale(homeScale * 1.3f);
                node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.22f, homeScale)));
            }
            if (stageRef) if (auto node = stageRef->lock()) flash(node.data(), at, iconSize * 0.3f, {1.0f, 0.95f, 0.75f, 0.8f});
        }));
        // A short light trail behind it (not afterimage copies).
        if (auto* trail = LightTrail::create(ghost, 0.12f, iconSize * 0.6f)) stage->addChild(trail, 199);
    }
    stage->runAction(CCSequence::create(CCDelayTime::create(end + 1.0f), CCRemoveSelf::create(), nullptr));
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
