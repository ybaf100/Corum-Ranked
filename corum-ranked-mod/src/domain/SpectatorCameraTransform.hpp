#pragma once
#include <array>
#include <cmath>

namespace corum::ranked {
struct SpectatorCameraLayerTransform {
    bool present = false;
    float scaleX = 1, scaleY = 1, rotation = 0;
};
struct SpectatorCameraTransform {
    float zoom = 1, angle = 0;
    std::array<SpectatorCameraLayerTransform,3> layers{};
    bool nativeTweening=false; // capture-only proof for instant-vs-continuous camera boundaries
};
inline bool validSpectatorCameraTransform(SpectatorCameraTransform const& t) {
    if (!std::isfinite(t.zoom) || t.zoom < .01f || t.zoom > 100 ||
        !std::isfinite(t.angle) || std::abs(t.angle)>100000) return false;
    for(auto const& l:t.layers)if(!std::isfinite(l.scaleX) || !std::isfinite(l.scaleY) ||
        (l.present && (std::abs(l.scaleX)<.001f || std::abs(l.scaleY)<.001f)) || std::abs(l.scaleX)>100 || std::abs(l.scaleY)>100 ||
        !std::isfinite(l.rotation) || std::abs(l.rotation)>100000)return false;
    return t.layers[0].present;
}
inline SpectatorCameraTransform interpolateSpectatorCameraTransform(SpectatorCameraTransform a,
        SpectatorCameraTransform const& b,double t){
    auto lerp=[t](float x,float y){return static_cast<float>(x+(y-x)*t);};
    auto angle=[t](float x,float y){return static_cast<float>(x+std::remainder(y-x,360.f)*t);};
    a.zoom=lerp(a.zoom,b.zoom);a.angle=angle(a.angle,b.angle);
    for(std::size_t i=0;i<a.layers.size();++i)if(a.layers[i].present && b.layers[i].present){
        if(a.layers[i].scaleX*b.layers[i].scaleX>0)a.layers[i].scaleX=lerp(a.layers[i].scaleX,b.layers[i].scaleX);
        if(a.layers[i].scaleY*b.layers[i].scaleY>0)a.layers[i].scaleY=lerp(a.layers[i].scaleY,b.layers[i].scaleY);
        a.layers[i].rotation=angle(a.layers[i].rotation,b.layers[i].rotation);
    }
    return a;
}
}
