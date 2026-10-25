#pragma once
namespace corum::ranked {
// A native collision query may inspect trigger objects only. This scope never
// grants input/physics, item collection, damage, scoring or persistence rights.
class ScopedSpectatorTriggerQuery final {
    inline static thread_local void const* s_layer=nullptr;
    inline static thread_local void const* s_player=nullptr;
    void const* m_layer;void const* m_player;
public:
    ScopedSpectatorTriggerQuery(void const* layer,void const* player):m_layer(s_layer),m_player(s_player){s_layer=layer;s_player=player;}
    ~ScopedSpectatorTriggerQuery(){s_layer=m_layer;s_player=m_player;}
    static bool owns(void const* layer,void const* player){return layer && player && s_layer==layer && s_player==player;}
};
}
