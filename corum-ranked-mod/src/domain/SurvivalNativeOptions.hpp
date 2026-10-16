#pragma once
namespace corum::ranked {
template<class Level>
bool survivalSupportsLowDetail(Level* level,int levelId,bool nativeReady) {
    return nativeReady && level && static_cast<int>(level->m_levelID)==levelId && level->m_lowDetailMode;
}
// m_lowDetailMode is the author's capability, not the user's toggle. Keep
// preferences elsewhere so a map without LDM cannot erase the next map's choice.
template<class Level>
void applySurvivalNativeOptions(Level* level,bool preferLowDetail,bool disableShake) {
    if(!level)return;
    level->m_lowDetailModeToggled=level->m_lowDetailMode && preferLowDetail;
    level->m_disableShakeToggled=disableShake;
}
}
