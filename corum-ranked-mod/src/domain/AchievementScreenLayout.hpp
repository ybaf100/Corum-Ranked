#pragma once
#include <algorithm>
namespace corum::ranked {
struct AchievementScreenLayout {float cardWidth,cardHeight,cardScale,firstY,rowStep;};
inline AchievementScreenLayout achievementScreenLayout(float width,float height){
 auto const available=std::max(60.f,height-154.f);
 auto const h=std::min(88.f,(available-6.f)/2.f);
 return {std::min(250.f,width/2.f-22.f),h,h/88.f,height-114.f-h/2.f,h+6.f};
}
}
