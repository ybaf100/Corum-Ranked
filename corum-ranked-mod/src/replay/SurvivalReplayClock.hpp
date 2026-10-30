#pragma once
#include <matjson.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
namespace corum::ranked::replay {
// m_timestamp is the Native input clock, distinct from levelTime / dt / tick.
// A recording contains its observed assignments at Native call boundaries.
// Replay injects these external clock inputs; computed physical state is never
// copied. Unchanged clock boundaries remain exact equality checks.
using ClockJson=matjson::Value;
inline constexpr std::size_t NoClockBoundary=std::numeric_limits<std::size_t>::max();
inline bool replayClockFinite(ClockJson const& j){return j.isNumber()&&std::isfinite(j.asDouble().unwrap());}
inline bool replayClockDataValid(ClockJson const& j){
    if(j["schema"].asInt().unwrapOr(0)!=1||!j["events"].isArray()||j["events"].size()==0||j["events"].size()>8192)return false;
    for(auto const& e:j["events"]){auto p=e["phase"].asString().unwrapOr("");if(p!="FRAME"&&p!="DELTA"&&p!="STEP"&&p!="INPUT")return false;
        if(!replayClockFinite(e["before"])||!replayClockFinite(e["after"]))return false;}
    return j["events"][0]["phase"].asString().unwrapOr("")=="FRAME";
}
class ReplayInputClock final {
public:
    void source(){reset();m_source=true;m_data=ClockJson::object();m_data["kind"]="clock";m_data["schema"]=1;m_data["events"]=ClockJson::array();}
    void replay(ClockJson trace,double entry,double exit){reset();m_source=false;m_data=std::move(trace);m_lastExpected=entry;
        if(m_data.isNull()){
            if(!std::isfinite(entry)||!std::isfinite(exit)){fail("LEGACY_INPUT_CLOCK_UNAVAILABLE");return;}
            m_legacy=true;m_data=ClockJson::object();m_data["schema"]=1;m_data["events"]=ClockJson::array();ClockJson f;f["phase"]="FRAME";f["before"]=entry;f["after"]=exit;m_data["events"].push(f);
        }
        if(!replayClockDataValid(m_data))fail("INPUT_CLOCK_TRACE_INVALID");
    }
    std::size_t enter(char const* phase,double& live,double legacyInput=std::numeric_limits<double>::quiet_NaN()){
        if(!m_error.empty())return NoClockBoundary;
        if(!std::isfinite(live)){fail("INPUT_CLOCK_NONFINITE");return NoClockBoundary;}
        if(m_legacy&&std::string_view(phase)!="FRAME"){
            // Old v2/v3 dispatches explicitly contain this input-clock value.
            // No missing tick/pose/time is synthesized for other boundaries.
            if(std::string_view(phase)=="INPUT"&&std::isfinite(legacyInput))live=legacyInput;
            return NoClockBoundary;
        }
        auto index=m_index++;
        if(m_source){if(index>=8192){fail("INPUT_CLOCK_BUDGET");return NoClockBoundary;}ClockJson e;e["phase"]=phase;e["before"]=live;m_data["events"].push(e);}
        else{
            if(index>=m_data["events"].size()){fail("INPUT_CLOCK_EXTRA_BOUNDARY");return NoClockBoundary;}
            auto const& e=m_data["events"][index];if(e["phase"].asString().unwrapOr("")!=phase){fail("INPUT_CLOCK_BOUNDARY_ORDER");return NoClockBoundary;}
            auto expected=e["before"].asDouble().unwrap();observe(index,"enter",live,expected);
            if(expected==m_lastExpected&&live!=expected){fail("INPUT_CLOCK_UNEXPECTED_WRITE");return NoClockBoundary;}
            live=expected;m_lastExpected=expected;
        }
        return index;
    }
    void leave(std::size_t index,double& live){
        if(index==NoClockBoundary||!m_error.empty())return;
        if(!std::isfinite(live)){fail("INPUT_CLOCK_NONFINITE");return;}
        auto& e=m_data["events"][index];
        if(m_source)e["after"]=live;
        else{auto before=e["before"].asDouble().unwrap(),after=e["after"].asDouble().unwrap();observe(index,"leave",live,after);
            // Recorded writes define the input-clock timeline. A missing write
            // or an unrecorded write is a real failure, not a tolerated drift.
            bool frameAssignment=e["phase"].asString().unwrapOr("")=="FRAME"&&before!=after;
            if(!frameAssignment&&after==m_lastExpected&&live!=after){fail("INPUT_CLOCK_UNEXPECTED_WRITE");return;}
            if(after!=m_lastExpected&&live==m_lastExpected&&after!=before){fail("INPUT_CLOCK_MISSING_WRITE");return;}
            live=after;m_lastExpected=after;}
    }
    ClockJson finish(){if(!m_source&&m_index!=m_data["events"].size())fail("INPUT_CLOCK_UNCONSUMED_BOUNDARIES");return m_data;}
    std::string const& error()const{return m_error;}ClockJson const& observations()const{return m_observed;}
private:
    ClockJson m_data,m_observed=ClockJson::array();bool m_source=true,m_legacy=false;std::size_t m_index=0;double m_lastExpected=0;std::string m_error;
    void reset(){m_data=nullptr;m_observed=ClockJson::array();m_error.clear();m_index=0;m_legacy=false;m_lastExpected=0;}
    void fail(char const* reason){if(m_error.empty())m_error=reason;}
    void observe(std::size_t index,char const* phase,double actual,double expected){ClockJson e;e["boundary"]=static_cast<std::int64_t>(index);e["edge"]=phase;e["nativeObserved"]=actual;e["recordedInputClock"]=expected;if(m_observed.size()<32)m_observed.push(e);}
};
}
