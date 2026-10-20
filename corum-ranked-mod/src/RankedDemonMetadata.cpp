#include "RankedMapDetails.hpp"
#include "domain/GddlMetadata.hpp"
#include <Geode/utils/web.hpp>
using namespace geode::prelude;
namespace corum::ranked {
void RankedMapDetails::requestDemonTier(int levelId){
    // Display-only enrichment never claims GD's global search delegate or
    // holds up map/audio preparation. Share one bounded request across panels.
    struct Requests {int pending=0;std::map<int,std::chrono::steady_clock::time_point> retry;
        geode::async::TaskHolder<web::WebResponse> request;};
    static Requests requests;auto& state=requests;
    auto it=m_cache.find(levelId);auto const now=std::chrono::steady_clock::now();
    if(it==m_cache.end() || it->second.metadata.kind!="DEMON" || it->second.metadata.gddlTier ||
        state.pending || now<state.retry[levelId])return;
    state.pending=levelId;state.retry[levelId]=now+std::chrono::minutes(5);
    while(state.retry.size()>256)state.retry.erase(state.retry.begin());
    auto http=web::WebRequest().timeout(std::chrono::seconds(4)).followRedirects(false).certVerification(true);
    state.request.spawn(http.get("https://gdladder.com/api/levels/"+std::to_string(levelId)),[this,levelId](web::WebResponse response){
        if(requests.pending!=levelId)return;requests.pending=0;
        auto found=m_cache.find(levelId);
        if(response.code()!=200 || response.data().size()>512*1024 || found==m_cache.end() || found->second.metadata.kind!="DEMON")return;
        auto metadata=found->second.metadata;
        if(applyGddlMetadata(response.json().unwrapOr(matjson::Value()),levelId,metadata) && metadata!=found->second.metadata){
            found->second.metadata=std::move(metadata);++m_revision;
        }
    });
}
}
