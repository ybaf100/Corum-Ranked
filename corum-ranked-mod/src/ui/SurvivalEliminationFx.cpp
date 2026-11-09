#include "SurvivalEliminationFx.hpp"

#include <Geode/cocos/actions/CCActionEase.h>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include "../domain/CompetitiveHudLayout.hpp"
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
// Gather and cut timing (seconds from the elimination window start).
constexpr float kGatherLift = 0.24f;      // gold icon rises out of its card
constexpr float kGatherSpread = 0.55f;    // victims leave over this span
constexpr float kGatherTravel = 0.70f;    // each victim's flight to the pile
constexpr float kGatherWindup = 0.24f;    // pull back before the first sweep
constexpr float kGatherCutStart = 1.3f;   // first sweep
constexpr float kGatherSweep = 0.26f;     // one big sweep through the pile
constexpr float kGatherFinalBeat = 0.30f; // slow-motion beat before the last player
constexpr float kGatherFinalSwing = 0.14f;
constexpr float kGatherReturn = 0.35f;
constexpr float kGatherBudget = 3.45f;    // inside the 1.55 s window + 2.0 s hold
constexpr int kShakeTag = 7481;

struct GatherPlan {
    std::size_t sweeps = 0;
    float sweep = kGatherSweep;
    std::vector<float> cuts; // per victim, in cut order
    float lastCut = 0.0f;
};

// One cut per player: about five per big sweep, spread over the middle of
// the sweep in blade order; the last player gets its own final swing.
GatherPlan gatherPlan(std::size_t victims) {
    GatherPlan plan;
    if (victims == 0) return plan;
    auto const regular = victims - 1;
    plan.sweeps = regular == 0 ? 0 : std::min<std::size_t>(6, (regular + 4) / 5);
    if (plan.sweeps > 0)
        plan.sweep = std::min(kGatherSweep, (kGatherBudget - kGatherCutStart - kGatherFinalBeat - 0.15f - kGatherReturn) / static_cast<float>(plan.sweeps));
    for (std::size_t k = 0; k < regular; ++k) {
        auto const sweep = std::min(plan.sweeps - 1, k * plan.sweeps / regular);
        auto const first = (sweep * regular + plan.sweeps - 1) / plan.sweeps;
        auto const next = ((sweep + 1) * regular + plan.sweeps - 1) / plan.sweeps;
        auto const count = next > first ? next - first : 1;
        auto const q = count > 1 ? static_cast<float>(k - first) / static_cast<float>(count - 1) : 0.5f;
        plan.cuts.push_back(kGatherCutStart + static_cast<float>(sweep) * plan.sweep + plan.sweep * (0.2f + 0.6f * q));
    }
    plan.lastCut = kGatherCutStart + static_cast<float>(plan.sweeps) * plan.sweep + kGatherFinalBeat;
    plan.cuts.push_back(plan.lastCut);
    return plan;
}

// The GD crash sound, pitched up for a sweep and down for the finale, at the
// Ranked volume.
void playSlashSound(float speed, float volume) {
    auto const gain = RankedVolumeControl::get().volume();
    if (gain <= 0) return;
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->playEffect("explode_11.ogg", speed, 0.0f, volume * gain);
}

// A short shake of `target` around `base` (stops the previous one first).
void shakeNode(CCNode* target, CCPoint base, float amplitude, int steps) {
    if (!target) return;
    target->stopActionByTag(kShakeTag);
    target->setPosition(base);
    auto* actions = CCArray::create();
    for (int i = 0; i < steps; ++i) {
        auto const fade = 1.0f - static_cast<float>(i) / static_cast<float>(steps);
        auto const x = (i % 2 ? amplitude : -amplitude) * fade, y = (i % 3 == 0 ? amplitude : -amplitude) * 0.7f * fade;
        actions->addObject(CCMoveTo::create(0.03f, {base.x + x, base.y + y}));
    }
    actions->addObject(CCMoveTo::create(0.03f, base));
    auto* sequence = CCSequence::create(actions);
    sequence->setTag(kShakeTag);
    target->runAction(sequence);
}

// The finale: a gold shockwave ring and a short screen flash.
void finaleBurst(CCNode* stage, CCPoint center) {
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
    ring->runAction(CCSequence::create(CCEaseSineOut::create(CCScaleTo::create(0.38f, 3.2f)), CCRemoveSelf::create(), nullptr));
    auto const size = stage->getContentSize();
    auto* white = CCLayerColor::create({255, 248, 224, 0}, size.width, size.height);
    stage->addChild(white, 300);
    white->runAction(CCSequence::create(CCFadeTo::create(0.04f, 70), CCFadeTo::create(0.22f, 0), CCRemoveSelf::create(), nullptr));
}

// Executor motion (the main gold icon, or a trailing afterimage `lag` later).
CCSequence* executorActions(SliceGatherExecutor const& executor, GatherPlan const& plan, CCPoint pile, float reach, float big, float lag,
    std::function<void()> done) {
    auto const scale = executor.iconScale;
    CCPoint const a{pile.x - 0.8f * reach, pile.y + reach}, b{pile.x + 0.8f * reach, pile.y - reach};
    CCPoint const fa{pile.x - 0.95f * reach * 1.15f, pile.y + reach * 1.15f}, fb{pile.x + 0.95f * reach * 1.15f, pile.y - reach * 1.15f};
    auto* actions = CCArray::create();
    if (lag > 0) actions->addObject(CCDelayTime::create(lag));
    actions->addObject(CCSpawn::create(CCEaseSineOut::create(CCMoveBy::create(kGatherLift, {0, 18})),
        CCEaseBackOut::create(CCScaleTo::create(kGatherLift, big)), nullptr));
    // Fly over the pile, then wind up to the first sweep's start.
    auto const windStart = kGatherCutStart - kGatherWindup;
    actions->addObject(CCSpawn::create(CCEaseSineInOut::create(CCMoveTo::create(windStart - kGatherLift, {pile.x - 0.4f * reach, pile.y + 0.5f * reach})),
        CCRotateTo::create(windStart - kGatherLift, -20.0f), nullptr));
    actions->addObject(CCSpawn::create(CCEaseSineIn::create(CCMoveTo::create(kGatherWindup, a)), CCRotateTo::create(kGatherWindup, -45.0f), nullptr));
    // Big sweeps, alternating, eased in and out so each one reads as a swing.
    for (std::size_t sweep = 0; sweep < plan.sweeps; ++sweep) {
        auto const forward = sweep % 2 == 0;
        actions->addObject(CCSpawn::create(CCEaseSineInOut::create(CCMoveTo::create(plan.sweep, forward ? b : a)),
            CCRotateTo::create(plan.sweep, forward ? 35.0f : -35.0f), nullptr));
    }
    // Final swing: pull back to the far side during the beat, then cut through.
    auto const atA = plan.sweeps % 2 == 0;
    auto const from = atA ? fa : fb, to = atA ? fb : fa;
    auto const beat = std::max(0.05f, kGatherFinalBeat - kGatherFinalSwing / 2.0f);
    actions->addObject(CCSpawn::create(CCEaseSineOut::create(CCMoveTo::create(beat, from)), CCScaleTo::create(beat, big * 1.15f),
        CCRotateTo::create(beat, atA ? -50.0f : 50.0f), nullptr));
    actions->addObject(CCSpawn::create(CCEaseSineIn::create(CCMoveTo::create(kGatherFinalSwing, to)),
        CCRotateTo::create(kGatherFinalSwing, atA ? 50.0f : -50.0f), nullptr));
    actions->addObject(CCDelayTime::create(0.15f));
    actions->addObject(CCSpawn::create(CCEaseSineInOut::create(CCMoveTo::create(kGatherReturn, executor.home)),
        CCRotateTo::create(kGatherReturn, 0.0f), CCScaleTo::create(kGatherReturn, scale), nullptr));
    if (done) actions->addObject(CallFuncExt::create(std::move(done)));
    actions->addObject(CCRemoveSelf::create());
    return CCSequence::create(actions);
}
} // namespace

float sliceGatherSeconds(std::size_t victims) {
    auto const plan = gatherPlan(victims);
    return plan.lastCut + 0.15f + kGatherReturn;
}

void runSliceGathering(CCNode* layer, SliceGatherExecutor const& executor, std::vector<SliceGatherVictim> const& victims,
    CCPoint pile, float fall, CCNode* shakeTarget) {
    if (!layer || victims.empty()) return;
    auto* stage = CCNode::create();
    stage->setContentSize(layer->getContentSize());
    layer->addChild(stage, 60);
    auto const n = victims.size();
    auto const plan = gatherPlan(n);
    // On compact 64-player cards the icons are tiny: the pile and the gold
    // icon grow while they are off the cards so every cut reads.
    auto const pileScale = std::clamp(executor.iconScale * 2.2f, std::max(executor.iconScale, 0.5f), 1.15f);
    auto const big = pileScale * 1.3f;
    // Sweeps stay within the stage (the picker preview is small).
    auto const reach = std::min(std::clamp(pileScale / 0.6f, 0.6f, 2.0f) * 52.0f, layer->getContentSize().height * 0.22f);
    auto const spreadScale = pileScale / 0.6f;
    // A dark backdrop over the board while the pile is cut.
    auto const end = sliceGatherSeconds(n);
    auto* dim = CCLayerColor::create({2, 4, 10, 0}, stage->getContentSize().width, stage->getContentSize().height);
    stage->addChild(dim, -1000);
    dim->runAction(CCSequence::create(CCDelayTime::create(0.45f), CCFadeTo::create(0.6f, 150),
        CCDelayTime::create(std::max(0.0f, end - 0.35f - 1.05f)), CCFadeTo::create(0.35f, 0), CCRemoveSelf::create(), nullptr));
    // Combo counter over the pile: x1, x2, ... punching on every cut.
    auto* counter = menuLabel("", 0.62f, {pile.x, std::min(pile.y + reach * 1.35f, layer->getContentSize().height - 16.0f)}, Gold, FontWeight::Black);
    counter->setVisible(false);
    stage->addChild(counter, 250);
    auto const counterScale = counter->getScale();
    std::optional<WeakRef<CCNode>> shake;
    CCPoint shakeBase{};
    if (shakeTarget) { shake.emplace(shakeTarget); shakeBase = shakeTarget->getPosition(); }

    for (std::size_t k = 0; k < n; ++k) {
        auto const& victim = victims[k];
        auto* copy = victim.icon ? victim.icon() : nullptr;
        if (!copy) copy = CCNode::create();
        copy->setScale(victim.iconScale);
        copy->setPosition(victim.from);
        // The first to be cut lies on top; cut halves (z 20) fall above the pile.
        stage->addChild(copy, -static_cast<int>(std::min<std::size_t>(k, 200)));
        auto const leave = 0.05f + kGatherSpread * static_cast<float>(k) / static_cast<float>(n);
        auto const jitter = CCPoint{static_cast<float>(static_cast<int>(k * 37 % 23) - 11) * spreadScale, static_cast<float>(static_cast<int>(k * 53 % 19) - 9) * spreadScale};
        auto const spin = static_cast<float>(static_cast<int>(k * 29 % 40) - 20);
        std::optional<WeakRef<CCNode>> slot;
        if (victim.slot) slot.emplace(victim.slot);
        auto detach = victim.detach;
        copy->runAction(CCSequence::create(CCDelayTime::create(leave),
            CallFuncExt::create([slot, detach]() mutable {
                if (slot) if (auto node = slot->lock()) node->setVisible(false);
                if (detach) detach();
            }),
            CCSpawn::create(
                CCEaseSineInOut::create(CCMoveTo::create(kGatherTravel, {pile.x + jitter.x, pile.y + jitter.y})),
                CCScaleTo::create(kGatherTravel, pileScale),
                CCRotateBy::create(kGatherTravel, spin), nullptr),
            nullptr));
        // The cut, along the sweep the blade is in.
        auto const cutAt = plan.cuts[k];
        auto const last = k + 1 == n;
        std::size_t sweep = plan.sweeps;
        if (!last && plan.sweeps > 0)
            sweep = std::min(plan.sweeps - 1, static_cast<std::size_t>(std::max(0.0f, (cutAt - kGatherCutStart) / plan.sweep)));
        auto const forward = sweep % 2 == 0;
        EliminationFxTarget target{copy, stage, {}, pileScale, victim.icon, static_cast<int>(k)};
        auto const count = static_cast<int>(k) + 1;
        stage->runAction(CCSequence::create(CCDelayTime::create(cutAt), CallFuncExt::create([target, copy, fall, forward, last, stage, counter, counterScale, count]() mutable {
            target.center = copy->getPosition();
            playSliceFx(target, fall, forward ? CCPoint{0.8f, -1.0f} : CCPoint{-0.8f, 1.0f}, last);
            counter->setVisible(true);
            counter->setString(fmt::format("x{}", count).c_str());
            counter->stopAllActions();
            counter->setScale(counterScale * (last ? 1.9f : 1.3f));
            counter->runAction(CCEaseBackOut::create(CCScaleTo::create(last ? 0.3f : 0.12f, counterScale * (last ? 1.35f : 1.0f))));
            if (last) {
                finaleBurst(stage, target.center);
                counter->setColor({255, 255, 255});
                counter->runAction(CCSequence::create(CCDelayTime::create(0.45f), CCFadeOut::create(0.3f), nullptr));
            }
        }), nullptr));
    }
    // One shake per sweep, a big one on the final swing.
    for (std::size_t sweep = 0; sweep < plan.sweeps; ++sweep) {
        stage->runAction(CCSequence::create(CCDelayTime::create(kGatherCutStart + plan.sweep * (static_cast<float>(sweep) + 0.25f)),
            CallFuncExt::create([shake, shakeBase]() mutable {
                if (shake) if (auto node = shake->lock()) shakeNode(node.data(), shakeBase, 4.0f, 3);
                playSlashSound(1.35f, 0.12f);
            }), nullptr));
    }
    stage->runAction(CCSequence::create(CCDelayTime::create(plan.lastCut),
        CallFuncExt::create([shake, shakeBase]() mutable {
            if (shake) if (auto node = shake->lock()) shakeNode(node.data(), shakeBase, 10.0f, 7);
            playSlashSound(0.9f, 0.26f);
        }), nullptr));

    // The executor: gold, out of its card, over the pile, sweeping, home.
    // Two gold afterimages trail it.
    if (auto* ghost = executor.icon ? executor.icon() : nullptr) {
        if (auto* player = typeinfo_cast<PlayerIcon*>(ghost)) player->setGold(true);
        ghost->setScale(executor.iconScale);
        ghost->setPosition(executor.home);
        stage->addChild(ghost, 200);
        std::optional<WeakRef<CCNode>> home;
        if (executor.slot) { home.emplace(executor.slot); executor.slot->setVisible(false); }
        ghost->runAction(executorActions(executor, plan, pile, reach, big, 0.0f,
            [home]() mutable { if (home) if (auto node = home->lock()) node->setVisible(true); }));
        int trailZ = 199;
        for (auto const [lag, opacity] : {std::pair<float, GLubyte>{0.035f, 100}, std::pair<float, GLubyte>{0.07f, 48}}) {
            auto* trail = executor.icon();
            if (!trail) continue;
            if (auto* player = typeinfo_cast<PlayerIcon*>(trail)) { player->setGold(true); player->setIconOpacity(opacity); }
            trail->setScale(executor.iconScale);
            trail->setPosition(executor.home);
            stage->addChild(trail, trailZ--);
            trail->runAction(executorActions(executor, plan, pile, reach, big, lag, {}));
        }
    }
    stage->runAction(CCSequence::create(CCDelayTime::create(sliceGatherSeconds(n) + 1.0f), CCRemoveSelf::create(), nullptr));
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
