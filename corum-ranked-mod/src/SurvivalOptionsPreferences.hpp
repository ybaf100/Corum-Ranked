#pragma once
#include "domain/SurvivalOptionsPolicy.hpp"
class GJGameLevel;
namespace corum::ranked {
class SurvivalOptionsPreferences {
    SurvivalNativeOptions m_defaults;
    bool m_thumbnails=true,m_selectionMotion=true;
    SurvivalRoundOptions m_round;
    SurvivalOptionsPreferences();
public:
    static SurvivalOptionsPreferences& get();
    SurvivalNativeOptions defaults()const{return m_defaults;}
    void setDefaultLowDetail(bool value);
    void setDefaultDisableShake(bool value);
    SurvivalNativeOptions current(std::string const& context){return m_round.enter(context,m_defaults);}
    void setRoundLowDetail(std::string const& context,bool value){current(context);m_round.setLowDetail(value);}
    void setRoundDisableShake(std::string const& context,bool value){current(context);m_round.setDisableShake(value);}
    bool thumbnails() const{return m_thumbnails;}
    bool selectionMotion() const{return m_selectionMotion;}
    void setThumbnails(bool value);
    void setSelectionMotion(bool value);
    void apply(GJGameLevel* level,std::string const& context);
};
}
