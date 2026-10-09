#include "ResultScoreLayer.hpp"

#include "PlayerIcon.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "RankedInput.hpp"
#include "ResultBackdropBlur.hpp"
#include "StandingVisuals.hpp"

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {
CCLayerColor* iconRoot(CCPoint position) {
    auto* root = CCLayerColor::create({0,0,0,0});
    root->setContentSize(CCSizeZero); root->setPosition(position);
    root->setCascadeOpacityEnabled(true); return root;
}

std::string targetKey(ScoreProgressSample const& sample) {
    if (sample.tier != "GOLD") return sample.nextTier;
    if (sample.goldLeader) return "gold-self";
    auto const& target = sample.standing.target;
    return target ? fmt::format("gold:{}:{}:{}", target->accountId, target->rank, target->score) : "gold-unknown";
}

void drawRoundedBar(CCDrawNode* node, float width, ccColor3B color, GLubyte opacity = 255) {
    constexpr float height = 10;
    node->clear(); node->setContentSize({width,height});
    if (width <= 0) return;
    auto const radius = std::min(height / 2,width / 2);
    ccColor4F const fill {color.r / 255.0f,color.g / 255.0f,color.b / 255.0f,opacity / 255.0f};
    if (width <= height) node->drawDot({width / 2,height / 2},radius,fill);
    else node->drawSegment({radius,height / 2},{width - radius,height / 2},radius,fill);
}
} // namespace

ResultScoreLayer* ResultScoreLayer::create(ScoreAnimationInput input, CCNode* resultScreen,
    std::function<void()> completion) {
    auto* layer = new ResultScoreLayer();
    if (layer && layer->init(std::move(input), resultScreen, std::move(completion))) {
        layer->autorelease(); return layer;
    }
    delete layer; return nullptr;
}

bool ResultScoreLayer::init(ScoreAnimationInput input, CCNode* resultScreen, std::function<void()> completion) {
    if (!validScoreBands(input.bands) || !CCLayerColor::initWithColor({0,0,0,0})) return false;
    m_input = std::move(input); m_completion = std::move(completion);
    auto const size = CCDirector::sharedDirector()->getWinSize();
    setContentSize(size); setPosition(CCPointZero); setID("ranked-result-score-layer"_spr);
    if (auto* blur = captureBlurredResult(resultScreen, size)) addChild(blur, 0);
    else log::warn("[RankedResult] result backdrop blur could not be created");
    auto* dim = CCLayerColor::create({2,5,14,148}); dim->setContentSize(size); addChild(dim, 1);

    m_barLeft = std::max(50.0f, size.width * 0.125f);
    m_barWidth = size.width - m_barLeft * 2;
    m_barY = size.height * 0.36f; m_iconY = m_barY + 92;
    auto const previous = m_input.beforeTier == "RED" ? ccc3(100,108,124) : tierColor(m_input.beforeTier);
    auto* track = CCDrawNode::create(); drawRoundedBar(track,m_barWidth,previous,180);
    track->setPosition({m_barLeft,m_barY}); addChild(track, 10);
    m_fill = CCDrawNode::create(); m_fill->setPosition({m_barLeft,m_barY});
    m_fill->setID("ranked-result-score-fill"_spr); addChild(m_fill, 11);
    for (auto x : {m_barLeft, m_barLeft + m_barWidth}) {
        auto* cap = CCLayerColor::create({225,235,245,190}); cap->setContentSize({1.5f,15});
        cap->setPosition({x - 0.75f,m_barY - 3.5f}); addChild(cap, 12);
    }

    auto* outcome = menuLabel(m_input.won ? i18n::text("win","WIN") : i18n::text("lose","LOSE"),
        1.05f, {size.width / 2, size.height * 0.80f}, m_input.won ? ccc3(90,239,133) : ccc3(255,93,113), FontWeight::Black);
    fitMenuLabelWidth(outcome, size.width * 0.48f, 1.05f, 0.68f); addChild(outcome, 20);
    m_score = menuLabel("",0.64f,{},Text,FontWeight::Black); addChild(m_score, 20);
    m_delta = menuLabel(fmt::format("{:+d} LP", m_input.after - m_input.before), 0.44f, {},
        m_input.after == m_input.before ? TextMuted : m_input.after > m_input.before ? ccc3(90,239,133) : ccc3(255,93,113), FontWeight::ExtraBold);
    addChild(m_delta, 20);
    m_leftTier = menuLabel("",0.48f,{m_barLeft,m_barY + 22},Text,FontWeight::Black); addChild(m_leftTier,20);
    m_rightTier = menuLabel("",0.48f,{m_barLeft + m_barWidth,m_barY + 22},Text,FontWeight::Black); addChild(m_rightTier,20);
    m_leftCutoff = menuLabel("",0.40f,{m_barLeft,m_barY - 18},Text,FontWeight::ExtraBold); addChild(m_leftCutoff,20);
    m_rightCutoff = menuLabel("",0.40f,{m_barLeft + m_barWidth,m_barY - 18},Text,FontWeight::ExtraBold); addChild(m_rightCutoff,20);
    m_seriesRoot = CCNode::create(); m_seriesRoot->setPosition({size.width / 2, size.height * 0.59f}); addChild(m_seriesRoot,30);

    m_menu = CCMenu::create(); m_menu->setPosition(CCPointZero); addChild(m_menu,100);
    auto* face = surface({118,30},{},ccc3(136,147,169),SurfaceRaised,240,true);
    auto* label = menuLabel(i18n::text("tap-to-skip","SKIP"),0.44f,{59,15},Text,FontWeight::ExtraBold);
    fitMenuLabelWidth(label,100,0.44f,0.34f); face->addChild(label,5);
    auto* skip = CCMenuItemSpriteExtra::create(face,this,menu_selector(ResultScoreLayer::onSkip));
    skip->setID("ranked-result-score-skip"_spr); skip->setPosition({size.width / 2,26}); m_menu->addChild(skip);
    applySample(sampleScoreProgress(m_input,0));
    if (m_input.promotionInProgress) showSeries(false);
    return true;
}

void ResultScoreLayer::onEnter() {
    CCLayerColor::onEnter();
    if (!m_closed) {
        setKeypadEnabled(true);
        if (m_menu) { cancelMenuTouch(m_menu); m_menu->setTouchEnabled(true); }
        if (!m_complete) scheduleUpdate();
    }
}

void ResultScoreLayer::onExit() {
    unscheduleUpdate(); setKeypadEnabled(false);
    if (m_menu) { cancelMenuTouch(m_menu); m_menu->setTouchEnabled(false); }
    CCLayerColor::onExit();
}

void ResultScoreLayer::update(float dt) {
    if (m_closed || m_complete) return;
    m_elapsed = std::min(m_input.duration(),m_elapsed + std::max(0.0f,dt));
    auto const sample = sampleScoreProgress(m_input,m_elapsed); applySample(sample);
    if (sample.complete) { m_complete = true; unscheduleUpdate(); showNotices(); }
}

CCLayerColor* ResultScoreLayer::tierIcon(std::string const& tier, CCPoint position, bool assemble) {
    auto const badgeSize = std::min(78.0f,getContentSize().height * 0.245f);
    auto assembly = createRankBadgeAssembly(tier,badgeSize);
    assembly.root->addChild(createRankBadgeBackdrop(tier,{badgeSize,badgeSize},RankBadgeDisplay::Large),-10);
    assembly.root->setPosition(position); addChild(assembly.root,5);
    if (assemble && m_input.motion() == ScoreMotion::Promotion) {
        // Same same-canvas body/core + ornament assembly used by Placed.
        for (auto const& part : assembly.baseParts) {
            part.sprite->setOpacity(0); part.sprite->setScale(part.assembledScale * 0.55f);
            part.sprite->runAction(CCSequence::create(CCDelayTime::create(0.25f),
                CCSpawn::create(CCFadeIn::create(0.20f),
                    CCEaseBackOut::create(CCScaleTo::create(0.45f,part.assembledScale)),nullptr),nullptr));
        }
        for (std::size_t i = 0; i < assembly.ornaments.size(); ++i) {
            auto const& part = assembly.ornaments[i];
            auto const offset = ccp((i % 2 ? -1.0f : 1.0f) * badgeSize * 0.65f,badgeSize * 0.3f);
            part.sprite->setPosition(offset); part.sprite->setOpacity(0); part.sprite->setScale(part.assembledScale * 0.7f);
            part.sprite->runAction(CCSequence::create(CCDelayTime::create(0.38f + static_cast<float>(i) * 0.07f),
                CCSpawn::create(CCFadeIn::create(0.18f),CCEaseBackOut::create(CCMoveTo::create(0.42f,CCPointZero)),
                    CCEaseBackOut::create(CCScaleTo::create(0.42f,part.assembledScale)),nullptr),nullptr));
        }
    } else if (assemble) {
        assembly.root->setScale(0.25f); assembly.root->setOpacity(0);
        assembly.root->runAction(CCSequence::create(CCDelayTime::create(0.25f),
            CCSpawn::create(CCFadeIn::create(0.22f),
                CCEaseBackOut::create(CCScaleTo::create(0.45f,1)),nullptr),nullptr));
    }
    return assembly.root;
}

CCLayerColor* ResultScoreLayer::targetIcon(ScoreProgressSample const& sample, bool assemble) {
    auto const position = ccp(m_barLeft + m_barWidth,m_iconY);
    if (sample.tier != "GOLD") return tierIcon(sample.nextTier,position,assemble);
    auto* root = iconRoot(position); addChild(root,5);
    auto* icon = sample.goldLeader ? PlayerIcon::createLocal()
        : PlayerIcon::createRemote(sample.standing.target && !sample.standing.target->bot ? sample.standing.target->accountId : 0);
    if (icon) { icon->setScale(1.55f); root->addChild(icon); }
    if (assemble) {
        root->setScale(0.25f); root->setOpacity(0);
        root->runAction(CCSpawn::create(CCFadeIn::create(0.20f),CCEaseBackOut::create(CCScaleTo::create(0.42f,1)),nullptr));
    }
    return root;
}

void ResultScoreLayer::retireIcon(CCLayerColor* icon, bool demotion) {
    if (!icon) return;
    icon->stopAllActions();
    if (demotion) {
        // Tier parts separate and fall as in the existing full-screen demotion.
        int index = 0;
        for (auto* part : icon->getChildrenExt()) {
            // The persistent halo fades with the root; only badge parts fall.
            if (!geode::cast::typeinfo_cast<CCSprite*>(part)) continue;
            auto const horizontal = (index % 2 ? -1 : 1) * (12 + index * 2);
            ++index;
            part->stopAllActions();
            part->runAction(CCSpawn::create(CCEaseSineIn::create(CCMoveBy::create(0.55f,
                {static_cast<float>(horizontal),-95})),
                CCRotateBy::create(0.55f,index % 2 ? -50 : 50),nullptr));
        }
        icon->runAction(CCSequence::create(CCFadeOut::create(0.60f),CCRemoveSelf::create(),nullptr));
    } else {
        icon->runAction(CCSequence::create(CCSpawn::create(CCScaleTo::create(0.25f,0.05f),
            CCFadeOut::create(0.25f),nullptr),CCRemoveSelf::create(),nullptr));
    }
}

void ResultScoreLayer::replaceTierIcons(ScoreProgressSample const& sample, bool animate) {
    auto const changedTier = m_tier != sample.tier;
    auto const changedTarget = m_targetKey != targetKey(sample);
    auto const demotion = m_input.motion() == ScoreMotion::Demotion;
    if (changedTier) {
        retireIcon(m_leftIcon,animate && demotion);
        m_leftIcon = tierIcon(sample.tier,{m_barLeft,m_iconY},animate);
        m_tier = sample.tier;
    }
    if (changedTier || changedTarget) {
        retireIcon(m_rightIcon,animate && demotion);
        m_rightIcon = targetIcon(sample,animate);
        m_targetKey = targetKey(sample);
        if (m_rightRank) { m_rightRank->removeFromParentAndCleanup(true); m_rightRank = nullptr; }
        if (sample.tier == "GOLD") {
            auto const rank = sample.goldLeader ? sample.standing.rank
                : sample.standing.target ? std::optional<int>(sample.standing.target->rank) : std::nullopt;
            m_rightRank = standingNumber(rank,0.60f); m_rightRank->setPosition({m_barLeft + m_barWidth,m_barY + 25});
            addChild(m_rightRank,22);
        }
    }
}

void ResultScoreLayer::applySample(ScoreProgressSample const& sample) {
    replaceTierIcons(sample,m_elapsed > 0);
    auto const color = tierColor(sample.tier);
    drawRoundedBar(m_fill,m_barWidth * static_cast<float>(sample.fill),color);
    m_leftTier->setString(sample.tier.c_str()); m_leftTier->setColor(color);
    m_rightTier->setString(sample.nextTier.c_str());
    if (!sample.nextTier.empty()) m_rightTier->setColor(tierColor(sample.nextTier));
    m_leftCutoff->setString(fmt::format("{} LP",sample.minimum).c_str());
    m_rightCutoff->setString(fmt::format("{} LP",sample.target).c_str());
    auto const labelWidth = std::clamp(m_barWidth * 0.26f,75.0f,132.0f);
    for (auto* label : {m_leftTier,m_rightTier}) fitMenuLabelWidth(label,labelWidth,0.48f,0.34f);
    for (auto* label : {m_leftCutoff,m_rightCutoff}) fitMenuLabelWidth(label,labelWidth,0.40f,0.28f);
    m_score->setString(fmt::format("{} LP",sample.score).c_str()); m_score->setColor(color);
    fitMenuLabelWidth(m_score,140,0.64f,0.46f);
    auto const scoreWidth = m_score->getContentSize().width * m_score->getScaleX();
    auto const deltaWidth = m_delta->getContentSize().width * m_delta->getScaleX();
    auto const end = m_barLeft + m_barWidth * static_cast<float>(sample.fill);
    // Center the score alone on the actual fill end. Delta is an independent
    // suffix, moved to the left near the viewport edge without shifting LP.
    m_score->setPosition({end,m_barY + 44});
    auto deltaX = end + (scoreWidth + deltaWidth) / 2 + 10;
    if (deltaX + deltaWidth / 2 > getContentSize().width - 12)
        deltaX = end - (scoreWidth + deltaWidth) / 2 - 10;
    m_delta->setPosition({deltaX,m_barY + 44});
}

void ResultScoreLayer::stamp(CCNode* node, float delay) {
    auto const scale = node->getScale(); node->setScale(scale * 2.15f); node->setVisible(false);
    node->runAction(CCSequence::create(CCDelayTime::create(delay),CCShow::create(),
        CCEaseBackIn::create(CCScaleTo::create(0.15f,scale * 0.86f)),
        CCEaseBackOut::create(CCScaleTo::create(0.12f,scale)),nullptr));
}

void ResultScoreLayer::showSeries(bool entering) {
    if (m_seriesShown || !m_input.promotion.active) return;
    m_seriesShown = true;
    auto const games = m_input.promotion.gamesRequired;
    auto const text = m_input.promoted ? i18n::text("promoted","PROMOTED!")
        : m_input.promotionFailed ? i18n::text("promotion-failed","PROMOTION FAILED")
        : i18n::text("promotion-match","PROMOTION MATCH");
    auto* title = menuLabel(text,0.47f,{0,39},m_input.promotionFailed ? ccc3(255,93,113) : Gold,FontWeight::Black);
    fitMenuLabelWidth(title,getContentSize().width * 0.56f,0.47f,0.34f); m_seriesRoot->addChild(title,5);
    if (m_input.promoted) {
        auto* detail = menuLabel(i18n::isKorean() ? fmt::format("{} 티어로 승급했습니다!",m_input.afterTier)
            : fmt::format("Promoted to {}!",m_input.afterTier),0.30f,{0,22},tierColor(m_input.afterTier),FontWeight::ExtraBold);
        fitMenuLabelWidth(detail,getContentSize().width * 0.55f,0.30f,0.24f); m_seriesRoot->addChild(detail,5);
    }
    // Wrap longer server-configured series instead of assuming three games or
    // creating negative-sized squares on a narrow screen.
    auto const columns = std::min(games,9);
    auto const rows = (games - 1) / columns + 1;
    constexpr float square = 21, spacing = 28;
    auto* grid = CCNode::create(); grid->setPosition({0,-5}); m_seriesRoot->addChild(grid,1);
    grid->setScale(std::min({1.0f,getContentSize().width * 0.40f / ((columns - 1) * spacing + square),
        getContentSize().height * 0.12f / ((rows - 1) * spacing + square)}));
    for (int i = 0; i < games; ++i) {
        auto const row = i / columns;
        auto const inRow = std::min(columns,games - row * columns);
        auto const x = (static_cast<float>(i % columns) - (inRow - 1) / 2.0f) * spacing;
        auto const y = ((rows - 1) / 2.0f - row) * spacing;
        auto* face = surface({square,square},{x,y},ccc3(138,145,157),ccc3(82,89,105),255,false);
        grid->addChild(face,1);
        auto const delay = std::min(0.60f,static_cast<float>(i) * 0.08f);
        stamp(face,delay);
        if (static_cast<std::size_t>(i) >= m_input.promotion.results.size()) continue;
        auto const won = m_input.promotion.results[static_cast<std::size_t>(i)] == "WIN";
        CCNode* mark = nullptr;
        if (won) {
            auto* check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            if (!check) continue;
            fitRankSprite(check,{square * 0.80f,square * 0.80f}); check->setColor(ccc3(90,239,133));
            mark = check;
        } else {
            mark = CCNode::create();
            for (auto const angle : {-45.0f,45.0f}) {
                auto* stroke = CCLayerColor::create({255,93,113,255});
                stroke->setContentSize({square * 0.80f,std::max(1.0f,square * 0.12f)});
                stroke->setAnchorPoint({0.5f,0.5f}); stroke->ignoreAnchorPointForPosition(false);
                stroke->setRotation(angle); mark->addChild(stroke);
            }
        }
        mark->setPosition({square / 2,square / 2}); face->addChild(mark,5);
        if (entering || i >= m_input.previousMarks) stamp(mark,delay + 0.35f);
    }
}

void ResultScoreLayer::showNotices() {
    if (m_input.promotionStarted) showSeries(true);
    if (m_input.demotionWarning) {
        auto* warning = menuLabel(i18n::text("demotion-warning","DEMOTION WARNING"),0.49f,{0,19},ccc3(255,93,113),FontWeight::Black);
        fitMenuLabelWidth(warning,getContentSize().width * 0.60f,0.49f,0.36f); m_seriesRoot->addChild(warning,5);
        auto* detail = menuLabel(i18n::text("demotion-next-loss","A loss may demote you in the next match."),0.30f,{0,-3},Text,FontWeight::ExtraBold);
        fitMenuLabelWidth(detail,getContentSize().width * 0.64f,0.30f,0.24f); m_seriesRoot->addChild(detail,5);
    }
}

void ResultScoreLayer::finish() {
    if (m_closed) return;
    m_closed = true; unscheduleUpdate(); setVisible(false);
    // The native item/menu still owns this input dispatch. Do not clear its
    // selected item, unregister delegates or destroy its callback target here.
    if (m_menu) m_menu->setEnabled(false);
    log::info("[RankedResult] score close requested match={}",m_input.matchId);
    auto const keepAlive = Ref<ResultScoreLayer>(this);
    queueInMainThread([keepAlive] {
        keepAlive->stopAllActions(); keepAlive->setKeypadEnabled(false);
        if (keepAlive->m_menu) {
            cancelMenuTouch(keepAlive->m_menu); keepAlive->m_menu->setTouchEnabled(false);
        }
        auto completion = std::move(keepAlive->m_completion);
        keepAlive->removeFromParentAndCleanup(true);
        log::info("[RankedResult] score retired match={}",keepAlive->m_input.matchId);
        if (completion) completion();
    });
}

void ResultScoreLayer::onSkip(CCObject*) { finish(); }
void ResultScoreLayer::keyBackClicked() { finish(); }

} // namespace corum::ranked::ui
