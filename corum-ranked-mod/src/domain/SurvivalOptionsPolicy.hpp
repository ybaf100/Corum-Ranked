#pragma once
#include <string>
namespace corum::ranked {
struct SurvivalNativeOptions { bool lowDetail=false,disableShake=false;bool operator==(SurvivalNativeOptions const&)const=default; };
class SurvivalRoundOptions {
    std::string m_context;
    SurvivalNativeOptions m_value;
public:
    SurvivalNativeOptions const& enter(std::string const& context,SurvivalNativeOptions defaults) {
        if(!context.empty() && context!=m_context){m_context=context;m_value=defaults;}
        return m_value;
    }
    void setLowDetail(bool value){m_value.lowDetail=value;}
    void setDisableShake(bool value){m_value.disableShake=value;}
};
}
