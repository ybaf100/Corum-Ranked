#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
namespace corum::ranked {
/** Presentation can delay native entry but can never advance server authority.
 * The steady deadline survives UI recreation and ignores frame quantization. */
class SurvivalEntryHold {
    std::string m_context;
    std::int64_t m_requested=0,m_visibleUntil=0;
public:
    std::optional<std::int64_t> remaining(std::string const& context,std::int64_t requested,
        std::int64_t opens,std::int64_t serverNow,bool visible,bool authorized,
        std::optional<std::int64_t> monotonicNow=std::nullopt){
        if(context!=m_context || requested!=m_requested){m_context=context;m_requested=requested;m_visibleUntil=0;}
        if(!authorized || requested<=0 || opens-requested<5000){m_visibleUntil=0;return {};}
        if(!visible)return {};
        auto const now=monotonicNow.value_or(serverNow);
        if(!m_visibleUntil)m_visibleUntil=now+5000;
        return std::max({std::int64_t{0},opens-serverNow,m_visibleUntil-now});
    }
};
}
