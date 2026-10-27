#pragma once
#include "SurvivalReplayCodec.hpp"
#include <utility>
namespace corum::ranked::replay {
// Drives verified native callbacks; no remote poses are used to move the local player.
class Playback final {
public:
    struct Frame{double dt=0;std::vector<Json> inputs,steps,poses,anchors;};
    explicit Playback(Document document):m_document(std::move(document)){
        if(!m_document.complete||m_document.records.empty()||str(m_document.records.front(),"kind")!="initial")throw std::runtime_error("Replay initial state unavailable");
        m_initial=m_document.records.front();Frame frame;
        for(auto& r:m_document.records){auto kind=str(r,"kind");
          if(kind=="frame"){if(frame.dt>0)m_frames.push_back(std::move(frame));frame={};frame.dt=number(r,"dt");}
          else if(kind=="anchor")frame.anchors.push_back(std::move(r));else if(kind=="input")frame.inputs.push_back(std::move(r));else if(kind=="step")frame.steps.push_back(std::move(r));else if(kind=="pose")frame.poses.push_back(std::move(r));
        }if(frame.dt>0)m_frames.push_back(std::move(frame));if(m_frames.empty())throw std::runtime_error("Replay native timeline unavailable");
        double time=0;for(auto const& f:m_frames){time+=f.dt;m_times.push_back(time);if(!f.poses.empty())m_anchors.push_back({number(f.poses.back(),"progress"),time});}
        std::sort(m_anchors.begin(),m_anchors.end(),[](auto const&a,auto const&b){return a.progress<b.progress||(a.progress==b.progress&&a.time<b.time);});
        m_document.metadata["captureCompleteness"]=m_document.footer["captureCompleteness"];
        m_document.records.clear();m_document.records.shrink_to_fit();
    }
    void restart(){m_index=0;m_time=0;m_budget=0;m_desync=false;m_step=m_anchor=0;m_presentedPose=nullptr;m_seeking=m_target>0;}
    void seek(double seconds){m_target=std::clamp(seconds,0.,duration());m_seeking=std::abs(m_target-m_time)>1e-6;if(m_target<m_time)m_reset=true;}
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
    bool ended()const{return m_index>=m_frames.size();}
    bool seeking()const{return m_seeking;}
    Json const& initial()const{return m_initial;}
    Json const& metadata()const{return m_document.metadata;}
    Json const* presentedPose()const{return m_presentedPose;}
    Frame const* current()const{return m_index<m_frames.size()?&m_frames[m_index]:nullptr;}
    void accumulate(double dt){if(!m_paused&&!m_desync)m_budget+=std::clamp(dt,0.,.1)*m_speed;}
    bool due()const{return !m_desync&&!ended()&&(m_seeking||(!m_paused&&m_budget+1e-8>=m_frames[m_index].dt));}
    void nativeStep(double dt,bool half,bool last,int step){
        auto* f=current();if(!f||m_step>=f->steps.size()){m_desync=true;return;}
        auto const&r=f->steps[m_step++];
        if(std::abs(number(r,"dt")-dt)>1e-6||r["half"].asBool().unwrapOr(!half)!=half||r["last"].asBool().unwrapOr(!last)!=last||integer(r,"nativeStep")!=step)m_desync=true;
    }
    void nativeAnchor(Json const& actual){
        auto* f=current();if(!f||m_anchor>=f->anchors.size()){m_desync=true;return;}
        auto const& expected=f->anchors[m_anchor++];
        if(expected["event"]!=actual["event"]||expected["player2"]!=actual["player2"])m_desync=true;
        for(auto key:{"fromX","fromY","toX","toY"})if(std::abs(number(expected,key)-number(actual,key))>2)m_desync=true;
    }
    void completeFrame(Json const& actual){
        auto* f=current();if(!f)return;if(m_step!=f->steps.size()||m_anchor!=f->anchors.size())m_desync=true;
        if(!f->poses.empty()){
            auto const& expected=f->poses.back();m_presentedPose=&expected;
            auto samePose=[](Json const&a,Json const&b){
                if(std::hypot(number(a,"x")-number(b,"x"),number(a,"y")-number(b,"y"))>2)return false;
                if(a["rotation"].isNumber()&&b["rotation"].isNumber()&&std::abs(std::remainder(number(a,"rotation")-number(b,"rotation"),360.))>5)return false;
                for(auto key:{"mode","mini","upsideDown","flipped","dual","dead","finished"})if(a[key]!=b[key])return false;
                return true;
            };
            if(!samePose(expected,actual)||(expected["dual"].asBool().unwrapOr(false)&&!samePose(expected["player2"],actual["player2"])))m_desync=true;
        }
        m_time+=f->dt;m_budget=std::max(0.,m_budget-f->dt);++m_index;m_step=m_anchor=0;
        if(m_seeking&&(m_time>=m_target||ended())){m_seeking=false;m_budget=0;}
    }
private:
    Document m_document;Json m_initial;Json const* m_presentedPose=nullptr;
    struct Anchor{double progress,time;};
    std::vector<Anchor> m_anchors;
    std::vector<Frame> m_frames;std::vector<double> m_times;
    std::size_t m_index=0,m_step=0,m_anchor=0;
    double m_time=0,m_budget=0,m_speed=1,m_target=0;
    bool m_paused=false,m_desync=false,m_seeking=false,m_reset=false,m_partial=false;
    std::string m_followAttempt;
};
}
