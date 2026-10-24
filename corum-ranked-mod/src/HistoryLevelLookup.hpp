#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <deque>

namespace corum::ranked {
// Process lifetime owner. GD responses never hold a popup or a raw scene pointer.
// Response routing temporarily borrows GD's delegate only for our exact key.
class HistoryLevelLookup final:public LevelManagerDelegate {
    std::uint64_t m_generation=0;
    int m_id=0;
    std::string m_key,m_error;
    std::deque<std::string> m_retired;
    geode::Ref<GJGameLevel> m_level;
public:
    static HistoryLevelLookup& get();
    std::uint64_t begin(int levelId);
    void cancel(std::uint64_t generation);
    bool ownsResponse(char const* key)const;
    void responseCompleted(char const* key);
    geode::Ref<GJGameLevel> level(std::uint64_t generation)const;
    std::string error(std::uint64_t generation)const;
    void loadLevelsFinished(cocos2d::CCArray*,char const*)override;
    void loadLevelsFinished(cocos2d::CCArray* levels,char const* key,int)override{loadLevelsFinished(levels,key);}
    void loadLevelsFailed(char const*)override;
    void loadLevelsFailed(char const* key,int)override{loadLevelsFailed(key);}
    void setupPageInfo(gd::string,char const*)override{}
};
bool canOpenHistoryLevel();
void showHistoryLevel(int levelId);
}
