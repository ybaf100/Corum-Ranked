#pragma once
#include "SurvivalReplayCodec.hpp"
#include <deque>
#include <utility>
namespace corum::ranked::replay {
// Drives verified native callbacks; no remote poses are used to move the local player.
class Playback final {
public:
    struct Frame{double dt=0;Json queued=nullptr,preInputs=nullptr,cbf=nullptr,clock=nullptr;std::vector<Json> inputs,steps,poses,anchors,buttons,dispatches;std::vector<SpectatorRandomEvent> randoms;};
    explicit Playback(Document document):m_document(std::move(document)){
        if(!m_document.complete||m_document.records.empty()||str(m_document.records.front(),"kind")!="initial")throw std::runtime_error("Replay initial state unavailable");
        m_initial=m_document.records.front();Frame frame;
        for(auto& r:m_document.records){auto kind=str(r,"kind");
          if(kind=="frame"){if(frame.dt>0)m_frames.push_back(std::move(frame));frame={};frame.dt=number(r,"dt");frame.queued=r["queued"];frame.preInputs=r["preInputs"];}
          else if(kind=="random"){if(auto event=decodeSpectatorRandomEvent(r["event"]))frame.randoms.push_back(std::move(*event));}
          else if(kind=="clock")frame.clock=std::move(r);else if(kind=="cbf")frame.cbf=std::move(r);else if(kind=="dispatch")frame.dispatches.push_back(std::move(r));else if(kind=="buttons")frame.buttons.push_back(std::move(r));else if(kind=="anchor")frame.anchors.push_back(std::move(r));else if(kind=="input")frame.inputs.push_back(std::move(r));else if(kind=="step")frame.steps.push_back(std::move(r));else if(kind=="pose")frame.poses.push_back(std::move(r));
        }if(frame.dt>0)m_frames.push_back(std::move(frame));if(m_frames.empty())throw std::runtime_error("Replay native timeline unavailable");
        if(integer(metadata(),"verifiedFrameCount")>0){auto n=static_cast<std::size_t>(integer(metadata(),"verifiedFrameCount"));if(n>m_frames.size())throw std::runtime_error("Official replay prefix exceeds capture");m_frames.resize(n);}
        if(cbf())for(auto const& frame:m_frames)if(!cbfFrameValid(frame.cbf))throw std::runtime_error("CBF Native boundary tape incomplete; existing file preserved");
        if(integer(metadata(),"nativeClockVersion")==1)for(auto const& f:m_frames)if(!replayClockDataValid(f.clock))throw std::runtime_error("Native input clock trace incomplete; existing file preserved");
        double time=0;for(auto& f:m_frames){std::sort(f.randoms.begin(),f.randoms.end(),[](auto const&a,auto const&b){return a.ordinal<b.ordinal;});time+=f.dt;m_times.push_back(time);if(!f.poses.empty())m_anchors.push_back({number(f.poses.back(),"progress"),time});}
        std::sort(m_anchors.begin(),m_anchors.end(),[](auto const&a,auto const&b){return a.progress<b.progress||(a.progress==b.progress&&a.time<b.time);});
        buildMarkers();
        m_document.metadata["captureCompleteness"]=m_document.footer["captureCompleteness"];
        m_document.records.clear();m_document.records.shrink_to_fit();
    }
    void restart(){m_trail.clear();m_totals={};m_index=0;m_time=0;m_budget=0;m_desync=false;m_failure=Json();m_errorCounts=Json::object();m_recordedTick=0;m_step=m_anchor=m_random=m_buttons=m_legacyInput=m_dispatch=0;m_presentedPose=nullptr;m_seeking=m_target>0;}
    // P1-D: each request supersedes the previous one (newest target wins). A
    // backward target always replays forward from a verified reset; no pose
    // snapshot is ever used as a mid-replay start point.
    void seek(double seconds){++m_seekGeneration;m_seekOrigin=m_time;m_target=std::clamp(seconds,0.,duration());m_seeking=std::abs(m_target-m_time)>1e-6;if(m_target<m_time){m_reset=true;m_seekOrigin=0;}}
    std::uint64_t seekGeneration()const{return m_seekGeneration;}
    double seekTarget()const{return m_target;}
    /** 0..1 progress of the active seek simulation. */
    double seekProgress()const{if(!m_seeking)return 1;auto span=m_target-m_seekOrigin;return span>1e-6?std::clamp((m_time-m_seekOrigin)/span,0.,1.):1;}
    struct Marker{double time;char kind;}; // D death, C clear, R random, T native teleport/snap anchor
    std::vector<Marker> const& markers()const{return m_markers;}
    static constexpr std::size_t kMaxMarkers=96;
    void followRemote(std::string const& attempt,double elapsed,double progress){
        if(!std::isfinite(elapsed)||!std::isfinite(progress)||progress<0||progress>100)return;
        if(attempt!=m_followAttempt){m_followAttempt=attempt;m_reset=true;m_seeking=true;m_target=0;}
        double best=1e30,target=0;
        auto low=std::lower_bound(m_anchors.begin(),m_anchors.end(),progress-.5,[](auto const&a,double value){return a.progress<value;});
        auto high=std::upper_bound(low,m_anchors.end(),progress+.5,[](double value,auto const&a){return value<a.progress;});
        auto candidates=static_cast<std::size_t>(high-low);
        auto stride=std::max<std::size_t>(1,(candidates+127)/128);
        for(std::size_t i=0;i<candidates;i+=stride){auto const& anchor=*(low+i);auto delta=std::abs(anchor.progress-progress);
          auto cost=delta*10+std::min(20.,std::abs(anchor.time-elapsed))*.01;
          if(cost<best){best=cost;target=anchor.time;}
        }
        m_partial=best>5||std::abs(target-elapsed)>2||candidates>120;
        if(m_reset){m_target=target;m_seeking=true;return;}
        // Tiny regressions hold the existing world. Large discontinuities replay forward from a clean reset.
        if(target>m_time+.05){m_target=target;m_seeking=true;}
        else if(target<m_time-1){m_reset=true;m_target=target;m_seeking=true;}
        m_budget=0;
    }
    bool partial()const{return m_partial;}
    bool takeReset(){return std::exchange(m_reset,false);}
    void pause(bool paused){m_paused=paused;}
    bool paused()const{return m_paused;}
    void speed(double value){if(value==.5||value==1||value==2||value==4)m_speed=value;}
    double speed()const{return m_speed;}
    double time()const{return m_time;}
    double duration()const{return m_times.empty()?0:m_times.back();}
    bool desync()const{return m_desync;}
    bool unprovenCollisionIdentity()const{auto reason=str(m_failure,"reason");return m_desync&&(reason=="INITIAL_COLLISION_IDENTITY_UNAVAILABLE"||reason=="CBF_LEGACY_COLLISION_IDENTITY_UNAVAILABLE"||reason=="CBF_COLLISION_IDENTITY_UNPROVEN");}
    Json const& failure()const{return m_failure;}
    void reject(std::string const& reason,Json expected=Json(),Json actual=Json(),std::string comparisonPath="$"){
        auto category=reason.substr(0,reason.find(':'));m_errorCounts[category]=m_errorCounts[category].asInt().unwrapOr(0)+1;
        if(m_desync){m_failure["errorCounts"]=m_errorCounts;return;}
        m_desync=true;m_failure["category"]=replayFailureCategory(reason);m_failure["errorCounts"]=m_errorCounts;m_failure["accuracyMode"]=metadata()["accuracyMode"];m_failure["physicsEnvironment"]=metadata()["physicsEnvironment"];m_failure["reason"]=reason;m_failure["frame"]=static_cast<std::int64_t>(m_index);
        m_failure["step"]=static_cast<std::int64_t>(m_step);m_failure["buttonDispatches"]=static_cast<std::int64_t>(m_buttons);
        m_failure["nativeTick"]=m_recordedTick;m_failure["presentationElapsed"]=m_time;
        if(cbf()){auto names=Json::array();for(auto name:{"x","y","rotation","yVelocity","shipRotationX","shipRotationY","jumpBuffered","isOnGround","holdingMask","lastCollisionBottom","lastCollisionTop","lastCollisionLeft","lastCollisionRight"})names.push(name);m_failure["cbfMotionFields"]=std::move(names);}
        m_failure["dispatchesConsumed"]=static_cast<std::int64_t>(m_dispatch);m_failure["previousExpected"]=m_presentedPose?*m_presentedPose:Json();
        m_failure["firstDifferentField"]=cbfFirstDifference(expected,actual,std::move(comparisonPath));m_failure["nativeInputVersion"]=integer(metadata(),"nativeInputVersion");m_failure["physicsSchema"]=integer(initial(),"physicsSchema");
        m_failure["expected"]=std::move(expected);m_failure["actual"]=std::move(actual);
    }
    Json const* nativeButtons(double dt,bool clear,int step){
        auto* f=current();if(!f||f->buttons.empty())return nullptr; // legacy codec
        if(m_buttons>=f->buttons.size()){reject("EXTRA_INPUT_DISPATCH");return nullptr;}
        auto const& queue=f->buttons[m_buttons++];
        if(std::abs(number(queue,"dt")-dt)>1e-6||queue["clear"].asBool().unwrapOr(!clear)!=clear||integer(queue,"nativeStep")!=step){Json actual;actual["dt"]=dt;actual["clear"]=clear;actual["nativeStep"]=step;reject("INPUT_DISPATCH_ORDER",queue,actual);return nullptr;}
        return &queue;
    }
    std::vector<Json> legacyInputsForStep(){
        std::vector<Json> due;auto* f=current();if(!f||!f->buttons.empty())return due;
        while(m_legacyInput<f->inputs.size() && integer(f->inputs[m_legacyInput],"tick")<=m_recordedTick)due.push_back(f->inputs[m_legacyInput++]);
        return due;
    }
    void nativeButtonsConsumed(Json const& expected,Json const& actual){if(expected!=actual)reject("INPUT_CONSUMPTION",expected,actual);}

    bool inputV2()const{return integer(metadata(),"nativeInputVersion")>=2;}
    bool cbf()const{return integer(metadata(),"nativeInputVersion")==3;}
    std::int64_t recordedTick()const{return m_recordedTick;}
    double frameEntryClock()const{if(m_index>0&&m_frames[m_index-1].poses.empty())return std::numeric_limits<double>::quiet_NaN();auto const& j=m_index==0?m_initial:m_frames[m_index-1].poses.back()["physics"];return j["timestamp"].asDouble().unwrapOr(std::numeric_limits<double>::quiet_NaN());}
    double frameExitClock()const{auto* f=current();return f&&!f->poses.empty()?f->poses.back()["physics"]["timestamp"].asDouble().unwrapOr(std::numeric_limits<double>::quiet_NaN()):std::numeric_limits<double>::quiet_NaN();}
    double nextInputClock()const{auto* f=current();return f&&m_dispatch<f->dispatches.size()?number(f->dispatches[m_dispatch],"timestamp"):std::numeric_limits<double>::quiet_NaN();}
    Json pendingInputs()const{
        if(m_index==0)return m_initial["queued"];
        auto const& previous=m_frames[m_index-1];return previous.poses.empty()?Json():previous.poses.back()["physics"]["pendingButtons"];
    }
    void nativeDispatch(Json const& actual){
        auto* f=current();if(!f||!inputV2())return;
        if(m_dispatch>=f->dispatches.size()){reject("EXTRA_HANDLE_BUTTON",Json(),actual);return;}
        auto const& expected=f->dispatches[m_dispatch++];
        for(auto key:{"button","down","player2","nativeStep","timestamp","before","after"})
            if(expected[key]!=actual[key]){reject("HANDLE_BUTTON_"+std::string(key),expected,actual);break;}
    }
    bool ended()const{return m_index>=m_frames.size();}
    bool seeking()const{return m_seeking;}
    Json const& initial()const{return m_initial;}
    Json const& metadata()const{return m_document.metadata;}
    Json const* presentedPose()const{return m_presentedPose;}
    Frame const* current()const{return m_index<m_frames.size()?&m_frames[m_index]:nullptr;}
    void accumulate(double dt){if(!m_paused&&!m_desync)m_budget+=std::clamp(dt,0.,.1)*m_speed;}
    bool due()const{return !m_desync&&!ended()&&(m_seeking||(!m_paused&&m_budget+1e-8>=m_frames[m_index].dt));}
    void nativeStep(double dt,bool half,bool last,int step){
        auto* f=current();if(!f||m_step>=f->steps.size()){reject("UNEXPECTED_NATIVE_CALLBACK");return;}
        auto const&r=f->steps[m_step++];m_recordedTick=integer(r,"tick");
        if(std::abs(number(r,"dt")-dt)>1e-6||r["half"].asBool().unwrapOr(!half)!=half||r["last"].asBool().unwrapOr(!last)!=last||integer(r,"nativeStep")!=step){Json actual;actual["dt"]=dt;actual["half"]=half;actual["last"]=last;actual["nativeStep"]=step;reject("PHYSICS_STEP",r,actual);}
    }
    void nativeAnchor(Json const& actual){
        auto* f=current();if(!f||m_anchor>=f->anchors.size()){reject("UNEXPECTED_NATIVE_CALLBACK");return;}
        auto const& expected=f->anchors[m_anchor++];
        if(expected["event"]!=actual["event"]||expected["player2"]!=actual["player2"])reject("NATIVE_ANCHOR_IDENTITY",expected,actual);
        for(auto key:{"fromX","fromY","toX","toY"})if(std::abs(number(expected,key)-number(actual,key))>2)reject("NATIVE_ANCHOR_POSITION",expected,actual);
    }
    std::optional<SpectatorRandomEvent> randomEvent(int index,int id,float x,float y){
        Json actual;actual["objectIndex"]=index;actual["objectId"]=id;actual["originX"]=static_cast<double>(x);actual["originY"]=static_cast<double>(y);actual["ordinal"]=static_cast<std::int64_t>(m_random);
        auto* f=current();
        // Playback activated a Random trigger the Source never recorded in this frame.
        if(!f||m_random>=f->randoms.size()){reject("RANDOM_IDENTITY_OR_HISTORY",Json(),actual,"$.random.CAPTURE_DROPPED");return {};}
        auto const& e=f->randoms[m_random];
        if(e.objectIndex!=index||e.objectId!=id||std::hypot(e.originX-x,e.originY-y)>.1){
            Json expected;expected["objectIndex"]=e.objectIndex;expected["objectId"]=e.objectId;expected["originX"]=static_cast<double>(e.originX);expected["originY"]=static_cast<double>(e.originY);expected["ordinal"]=static_cast<std::int64_t>(e.ordinal);
            reject("RANDOM_IDENTITY_OR_HISTORY",expected,actual,"$.random.IDENTITY_UNAVAILABLE");return {};}
        ++m_random;return e;}
    void completeFrame(Json const& actual){
        auto* f=current();if(!f)return;if(m_step!=f->steps.size()||m_anchor!=f->anchors.size()||m_random!=f->randoms.size()||m_buttons!=f->buttons.size()||(inputV2()&&m_dispatch!=f->dispatches.size())){
            // Which recorded Source evidence the Playback engine never consumed.
            Json expected,actual;auto pair=[&](char const* key,std::size_t recorded,std::size_t consumed){expected[key]=static_cast<std::int64_t>(recorded);actual[key]=static_cast<std::int64_t>(consumed);};
            pair("steps",f->steps.size(),m_step);pair("anchors",f->anchors.size(),m_anchor);pair("randoms",f->randoms.size(),m_random);pair("buttons",f->buttons.size(),m_buttons);
            if(inputV2())pair("dispatches",f->dispatches.size(),m_dispatch);
            reject("UNCONSUMED_NATIVE_EVENTS",expected,actual,m_random<f->randoms.size()?"$.random.HOOK_NOT_CALLED":"$.unconsumed");
        }
        if(!f->poses.empty()){
            auto const& expected=f->poses.back();
            if(inputV2()&&expected["physics"]!=actual["physics"])reject("NATIVE_PHYSICS_STATE",expected["physics"],actual["physics"],"$.physics");
            auto validatePose=[&](Json const&a,Json const&b,std::string const& path){
                if(std::hypot(number(a,"x")-number(b,"x"),number(a,"y")-number(b,"y"))>2){Json left,right;for(auto k:{"x","y"}){left[k]=a[k];right[k]=b[k];}reject("POSE_OR_PHYSICS_STATE",left,right,path);return;}
                if(a["rotation"].isNumber()&&b["rotation"].isNumber()&&std::abs(std::remainder(number(a,"rotation")-number(b,"rotation"),360.))>5){reject("POSE_OR_PHYSICS_STATE",a["rotation"],b["rotation"],path+".rotation");return;}
                for(auto key:{"mode","mini","upsideDown","flipped","dual","dead","finished"})if(a[key]!=b[key]){reject("POSE_OR_PHYSICS_STATE",a[key],b[key],path+"."+key);return;}
            };
            validatePose(expected,actual,"$");
            if(expected["dual"].asBool().unwrapOr(false))validatePose(expected["player2"],actual["player2"],"$.player2");
        }
        if(!f->poses.empty())m_presentedPose=&f->poses.back();
        if(!m_desync)noteTrail(*f,actual);
        m_time+=f->dt;m_budget=std::max(0.,m_budget-f->dt);++m_index;m_step=m_anchor=m_random=m_buttons=m_legacyInput=m_dispatch=0;
        if(m_seeking&&(m_time>=m_target||ended())){m_seeking=false;m_budget=0;}
    }
    /** P0-C bounded first-divergence report: up to kTrailBefore verified
     * Playback frames before the failure, the next kTrailAfter recorded Source
     * frames, per-stage consumed counts and the exact first failure. No pointers
     * or credentials; identity is limited to match/round/attempt metadata. */
    static constexpr std::size_t kTrailBefore=32,kTrailAfter=8;
    Json diagnosticsReport()const{
        Json report=Json::object();report["schema"]=1;report["failure"]=m_failure;
        for(auto key:{"attemptId","matchId","roundKey","mapGeneration","levelId","mapVersion","mapGeometrySha256","nativeInputVersion","nativeClockVersion","gameVersion","geodeVersion","modVersion","accuracyMode","captureCompleteness","officialCompletion","verifiedFrameCount"})
            if(metadata().contains(key))report["identity"][key]=metadata()[key];
        report["environment"]=metadata()["physicsEnvironment"];
        auto before=Json::array();for(auto const& t:m_trail)before.push(t);report["verifiedBefore"]=before;
        auto after=Json::array();auto start=m_failure.isObject()?static_cast<std::size_t>(std::max<std::int64_t>(0,integer(m_failure,"frame"))):m_index;
        for(std::size_t i=start;i<m_frames.size()&&i<start+kTrailAfter;++i)after.push(frameSummary(i,m_frames[i],nullptr));
        report["sourceAfter"]=after;
        Json stages;stages["framesVerified"]=static_cast<std::int64_t>(m_totals.frames);stages["nativeSteps"]=static_cast<std::int64_t>(m_totals.steps);
        stages["dispatches"]=static_cast<std::int64_t>(m_totals.dispatches);stages["buttons"]=static_cast<std::int64_t>(m_totals.buttons);
        stages["randoms"]=static_cast<std::int64_t>(m_totals.randoms);stages["anchors"]=static_cast<std::int64_t>(m_totals.anchors);
        stages["recordedFrames"]=static_cast<std::int64_t>(m_frames.size());report["stageCounts"]=stages;
        report["presentationElapsed"]=m_time;report["duration"]=duration();report["speed"]=m_speed;return report;
    }
    std::size_t trailSize()const{return m_trail.size();}
private:
    std::vector<Marker> m_markers;std::uint64_t m_seekGeneration=0;double m_seekOrigin=0;
    void buildMarkers(){
        // Only events actually present in the recorded native tape.
        bool dead=false,cleared=false;double lastRandom=-1,lastAnchor=-1;
        for(std::size_t i=0;i<m_frames.size()&&m_markers.size()<kMaxMarkers;++i){
            auto const& f=m_frames[i];auto t=m_times[i];
            if(!f.randoms.empty()&&t-lastRandom>=.5){m_markers.push_back({t,'R'});lastRandom=t;}
            if(!f.anchors.empty()&&t-lastAnchor>=.5){m_markers.push_back({t,'T'});lastAnchor=t;}
            if(!f.poses.empty()){auto const& p=f.poses.back();
                if(!dead&&p["dead"].asBool().unwrapOr(false)){m_markers.push_back({t,'D'});dead=true;}
                if(!cleared&&(p["finished"].asBool().unwrapOr(false)||number(p,"progress")>=100)){m_markers.push_back({t,'C'});cleared=true;}}
        }
    }
    struct Totals{std::size_t frames=0,steps=0,dispatches=0,buttons=0,randoms=0,anchors=0;};
    std::deque<Json> m_trail;Totals m_totals;
    static Json poseSummary(Json const& pose){Json p;for(auto key:{"x","y","rotation","mode","dead","finished","dual"}){if(pose.contains(key))p[key]=pose[key];}
        if(pose["physics"].isObject()){for(auto key:{"nativeStep","timestamp","randomSeed"}){if(pose["physics"].contains(key))p["physics"][key]=pose["physics"][key];}}
        return p;}
    Json frameSummary(std::size_t index,Frame const& f,Json const* actual)const{
        Json t;t["frame"]=static_cast<std::int64_t>(index);t["time"]=index<m_times.size()?m_times[index]:0.;t["dt"]=f.dt;
        t["steps"]=static_cast<std::int64_t>(f.steps.size());t["dispatches"]=static_cast<std::int64_t>(f.dispatches.size());t["buttons"]=static_cast<std::int64_t>(f.buttons.size());
        t["randoms"]=static_cast<std::int64_t>(f.randoms.size());t["anchors"]=static_cast<std::int64_t>(f.anchors.size());
        if(!f.steps.empty())t["tick"]=f.steps.back()["tick"];
        if(!f.poses.empty())t["expected"]=poseSummary(f.poses.back());
        if(actual&&actual->isObject())t["actual"]=poseSummary(*actual);
        if(!f.randoms.empty()){auto ids=Json::array();for(auto const& r:f.randoms){Json j;j["objectId"]=r.objectId;j["ordinal"]=static_cast<std::int64_t>(r.ordinal);ids.push(j);}t["randomIds"]=ids;}
        return t;
    }
    void noteTrail(Frame const& f,Json const& actual){
        ++m_totals.frames;m_totals.steps+=f.steps.size();m_totals.dispatches+=f.dispatches.size();m_totals.buttons+=f.buttons.size();m_totals.randoms+=f.randoms.size();m_totals.anchors+=f.anchors.size();
        if(m_trail.size()>=kTrailBefore)m_trail.pop_front();
        m_trail.push_back(frameSummary(m_index,f,&actual));
    }
    Document m_document;Json m_initial,m_failure,m_errorCounts=Json::object();Json const* m_presentedPose=nullptr;
    struct Anchor{double progress,time;};
    std::vector<Anchor> m_anchors;
    std::vector<Frame> m_frames;std::vector<double> m_times;
    std::size_t m_index=0,m_step=0,m_anchor=0,m_random=0,m_buttons=0,m_legacyInput=0,m_dispatch=0;
    std::int64_t m_recordedTick=0;
    double m_time=0,m_budget=0,m_speed=1,m_target=0;
    bool m_paused=false,m_desync=false,m_seeking=false,m_reset=false,m_partial=false;
    std::string m_followAttempt;
};
}
