#pragma once
#include "SurvivalReplayStorage.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
namespace corum::ranked::replay {
enum class ReplayNotice { None, Preparation, Data, Space, File, Receipt, Unsupported };
class Recorder final {
public:
    static Recorder& get();
    void begin(Json metadata);
    void accepted(std::string const& attempt,bool accepted);
    void add(Json record);
    void finish(std::string reason,double progress);
    void officialEnd(Json metadata,Json event,std::string reason,double progress);
    void sceneExited(std::string const& attempt,std::string reason);
    std::string const& currentAttemptId()const{return m_current;}
    bool hasPendingClose(std::string const& attempt)const{return m_closing.contains(attempt);}
    bool recovering()const{return m_recovery.valid();}
    void unsupported(std::string reason);
    void nativeInput(Json value);
    Json takePreInputs(){auto values=std::move(m_preInputs);m_preInputs=Json::array();return values;}
    void pump();
    void finishFromLast(std::string reason){finish(std::move(reason),m_bestProgress);}
    bool active()const{return !m_current.empty();}
    bool cbfCapture()const{return active()&&m_cbf;}
    bool physicsEnvironmentUnchanged();
    std::uint64_t tick()const{return m_tick;}
    void nextTick(){++m_tick;}
    bool inNativeFrame()const{return m_nativeFrame;}
    void nativeFrameBegin();
    void nativeFrameEnd();
    void elapsed(double value){if(std::isfinite(value)&&value>=0)m_elapsed=value;}
    std::string const& error()const{return m_error;}
    ReplayNotice notice()const{return m_notice;}
    void retryPending();
    Storage& storage(){return m_storage;}
private:
    Recorder();
    struct Capture{Json metadata;std::vector<Json> buffer;std::size_t bytes=0;bool accepted=false,ended=false,damaged=false,uncertain=false;};
    Storage m_storage;
    std::map<std::string,Capture> m_captures;
    struct Write {std::future<void> result;std::string stage,attempt,round;void get(){result.get();}};
    struct Promotion {std::future<bool> result;Json metadata;};
    std::vector<Write> m_writes;
    std::vector<Promotion> m_promotions;
    std::future<std::vector<Json>> m_recovery;
    std::deque<Json> m_receipts;
    std::string m_current,m_error;
    ReplayNotice m_notice=ReplayNotice::None;
    std::string m_displayAttempt,m_receiptToken;
    std::map<std::string,Json> m_deferredReceipts;
    std::map<std::string,Json> m_closing;
    std::deque<Json> m_endQueue;
    bool m_endBusy=false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_endRequest;
    std::vector<Json> m_frameRecords;
    std::size_t m_frameBytes=0;
    std::uint64_t m_frameTick=0;
    double m_frameElapsed=0;
    std::uint64_t m_tick=0;
    double m_elapsed=0,m_bestProgress=0;
    bool m_receiptBusy=false,m_nativeFrame=false,m_cbf=false;
    Json m_environment;
    std::optional<std::pair<std::string,double>> m_terminal;
    Json m_preInputs=Json::array();
    std::chrono::steady_clock::time_point m_nextReceipt{},m_flushAt{};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    void flush(Capture&);
    void queueReceipt(Json metadata);
    void note(ReplayNotice,std::string stage,std::string reason,Json const& metadata);
    void commitRecord(Json record);
};
}
