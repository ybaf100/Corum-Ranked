#pragma once
#include "SurvivalReplayCodec.hpp"
#include <functional>
#include <memory>
#include <map>
#include <future>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace corum::ranked::replay {
// IO and checksums run on this serial worker, never in the native update callback.
// Commands own serialized data only. No scene/PlayerObject/WeakRef enters the worker.
class Playback;
class Storage final {
public:
    explicit Storage(std::filesystem::path root);
    ~Storage();
    std::future<void> begin(Json metadata);
    std::future<void> append(std::string attempt,std::vector<Json> records,bool final=false);
    std::future<void> discard(std::string attempt);
    std::future<bool> reconcile(Receipt receipt);
    std::future<std::optional<Document>> best(Json identity);
    std::future<std::optional<Json>> inspect(Json identity);
    std::future<std::unique_ptr<Playback>> playback(Json identity);
    std::future<std::vector<Json>> recover();
    std::string error()const;
private:
    std::filesystem::path m_root;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_queue;
    std::size_t m_queuedBytes=0;
    bool m_stop=false;
    std::string m_error;
    struct Pending{Json metadata;std::string chain;std::size_t sequence=0,count=0,bytes=0,inputs=0,poses=0;std::int64_t ticks=0;double elapsed=0;};
    std::map<std::string,Pending> m_pending;
    std::thread m_worker;
    std::filesystem::path pending(std::string const& attempt)const;
    Json manifest()const;
    std::optional<Document> loadBest(Json const& identity);
    void saveManifest(Json const&);
    void safeDirectories(std::filesystem::path const&);
    void diskBudget(std::uintmax_t);
    void durable(std::filesystem::path const&);
    void replace(std::filesystem::path const&,std::filesystem::path const&);
    template<class F> auto submit(F f,std::size_t charge=0)->std::future<decltype(f())>{
        using R=decltype(f());auto task=std::make_shared<std::packaged_task<R()>>([this,f=std::move(f)]()mutable->R{
            try{return f();}catch(std::exception const&e){std::lock_guard lock(m_mutex);m_error=e.what();throw;}});
        auto result=task->get_future();{
            std::lock_guard lock(m_mutex);if(m_stop||m_queue.size()>=128||charge>2*1024*1024-m_queuedBytes)throw std::runtime_error("Replay writer backlog; existing best preserved");
            m_queuedBytes+=charge;m_queue.push_back([this,task,charge]{(*task)();std::lock_guard lock(m_mutex);m_queuedBytes-=charge;});
        }m_cv.notify_one();return result;
    }
};
}
