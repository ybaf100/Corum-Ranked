#pragma once
#include "domain/SurvivalMapSelection.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <array>
#include <atomic>
#include <deque>
#include <future>
#include <map>

namespace corum::ranked {
// No UI pointers cross the network/decoder boundary. This process-lifetime
// owner publishes textures only from its scheduled main-thread update.
class SurvivalThumbnailProvider final : public cocos2d::CCNode {
    struct Job {std::uint64_t id=0;bool medium=false,random=false,network=false;int attempt=0;std::int64_t notBefore=0;};
    struct Entry {geode::Ref<cocos2d::CCTexture2D> texture;std::size_t bytes=0;std::int64_t used=0;std::uint64_t id=0;bool medium=false;};
    struct Slot {
        bool busy=false;std::uint64_t ticket=0;std::int64_t began=0;Job job;
        std::shared_ptr<std::atomic_bool> exceeded;
        geode::async::TaskHolder<geode::utils::web::WebResponse> request;
    };
    struct Encoded {Job job;std::uint64_t generation;std::vector<std::uint8_t> bytes;};
    struct Decoded {std::unique_ptr<cocos2d::CCImage> image;std::int64_t millis=0;};
    struct Decode {Job job;std::uint64_t generation;std::future<Decoded> result;};
    std::array<Slot,2> m_slots;
    std::deque<Job> m_jobs;
    std::deque<Encoded> m_encoded;
    std::optional<Decode> m_decode;
    std::map<std::string,Entry> m_cache;
    std::map<std::string,std::int64_t> m_negative;
    std::string m_context,m_reveal,m_poolMatch,m_poolReveal;
    std::vector<std::uint64_t> m_candidates;
    void candidate(std::uint64_t id);
    std::uint64_t m_generation=0,m_selected=0;
    int m_randomRequests=0,m_httpRequests=0;
    bool m_enabled=false,m_invalidTextureCache=false;
    bool m_history=false;
    std::shared_ptr<std::atomic_bool> m_liveness;
    std::filesystem::path m_directory;
    static std::int64_t now();
    static std::string key(Job const&);
    void enqueue(Job,bool front=false);
    void begin(std::size_t,Job);
    void response(std::size_t,std::uint64_t,std::uint64_t,geode::utils::web::WebResponse);
    void failed(Job,int status);
    void decodeNext();
    void cache(Decode const&,Decoded);
    void trim();
    void trimDisk();
public:
    static SurvivalThumbnailProvider& get();
    static SurvivalThumbnailProvider& history();
    static bool supported();
    static void onApplicationPause();
    static std::uint64_t applicationEpoch();
    void activate(SurvivalSelectionContext const&);
    void release(SurvivalSelectionContext const&);
    void deactivate();
    void update(float) override;
    geode::Ref<cocos2d::CCTexture2D> selected();
    std::vector<std::pair<std::uint64_t,geode::Ref<cocos2d::CCTexture2D>>> previews();
};
}
