#include "AchievementUnlockToast.hpp"
#include "AchievementEffects.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include <deque>
#include <unordered_set>
using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {std::unordered_set<std::string> shown;std::deque<std::string> order;}
void showAchievementUnlocks(CCNode* owner,std::string const& resultKey,matjson::Value const& notices){
 if(!owner || !owner->isRunning() || resultKey.empty())return;
 auto list=notices.asArray();if(list.isErr())return;
 auto const size=CCDirector::sharedDirector()->getWinSize();int index=0;
 for(auto const& value:list.unwrap()){
  auto const id=value["achievementId"].asString().unwrapOr("");auto const type=value["type"].asString().unwrapOr("");
  auto const level=static_cast<int>(value["level"].asInt().unwrapOr(0));
  auto const tier=type=="PROGRESSIVE"?level:achievementRarityTier(value["rarity"].asString().unwrapOr(""));
  auto const name=value[i18n::isKorean()?"nameKo":"nameEn"].asString().unwrapOr("");
  if(id.empty() || name.empty() || tier<1 || tier>5 || (type!="PROGRESSIVE" && type!="ONE_SHOT"))continue;
  auto const key=resultKey+":"+id+":"+std::to_string(level);if(!shown.insert(key).second)continue;
  order.push_back(key);while(order.size()>512){shown.erase(order.front());order.pop_front();}
  auto const width=std::min(330.f,size.width-24);auto* group=CCNode::create();group->setID("survival-achievement-toast"_spr);
  group->setPosition({size.width/2,size.height+50});owner->addChild(group,2000);
  auto* face=surface({width,61},{},achievementColor(tier),Surface,255,true);group->addChild(face);
  auto* heading=menuLabel(i18n::text("achievement-unlocked","ACHIEVEMENT UNLOCKED"),.22f,{width/2,47},TextMuted,FontWeight::SemiBold);face->addChild(heading,5);
  auto label=name;auto const stage=value["displayLabel"].asString().unwrapOr("");if(!stage.empty())label+=" - "+stage;
  auto* title=menuLabel(label,.32f,{width/2,29},Text,FontWeight::Black);fitFullMenuLabel(title,width-24,19,.32f);styleAchievementText(title,tier);face->addChild(title,5);
  auto const ap=value["rewardAp"].asInt().unwrapOr(0);
  auto* reward=menuLabel(fmt::format("+{} AP · {}",ap,i18n::text("available","AVAILABLE")),.20f,{width/2,12},Gold,FontWeight::SemiBold);face->addChild(reward,5);
  // Every action belongs to this UI subtree; leaving the result cancels it.
  group->runAction(CCSequence::create(CCDelayTime::create(.25f+index*2.45f),CCEaseSineOut::create(CCMoveTo::create(.25f,{size.width/2,size.height-36})),CCDelayTime::create(1.9f),CCEaseSineIn::create(CCMoveTo::create(.25f,{size.width/2,size.height+50})),CCRemoveSelf::create(),nullptr));
  ++index;
 }
}
}
