#pragma once
#include <algorithm>
#include <vector>
namespace corum::ranked {
struct SurvivalRect {
    float x=0,y=0,w=0,h=0;
    float cx()const{return x+w/2;}float cy()const{return y+h/2;}
    float right()const{return x+w;}float top()const{return y+h;}
    bool contains(SurvivalRect const& b,float epsilon=.01f)const{return b.x>=x-epsilon&&b.y>=y-epsilon&&b.right()<=right()+epsilon&&b.top()<=top()+epsilon;}
    bool overlaps(SurvivalRect const& b)const{return x<b.right()-.01f&&right()>b.x+.01f&&y<b.top()-.01f&&top()>b.y+.01f;}
};
struct SurvivalSelectionLayout {
    SurvivalRect safe, header, subtitle, carousel, numberFinal, panel, thumbnail, title, creator, difficulty, metadata, ldm, shake, jukebox, ready, readiness, hint;
    bool compact=false;float scale=1;
};
struct SurvivalNumberLayout {SurvivalRect ribbon;std::vector<SurvivalRect> columns;float lineStep=0;};
inline SurvivalNumberLayout survivalNumberLayout(float width,float height,std::size_t digits){
    SurvivalNumberLayout r;if(digits==0 || digits>10)return r;
    auto const scale=std::min({1.f,width/400.f,height/320.f});
    auto const gap=4*scale,column=std::min(44*scale,(width-56*scale-gap*(digits-1))/digits);
    auto const total=column*digits+gap*(digits-1),centerY=height*.5f;
    r.ribbon={(width-total)/2-8*scale,centerY-42*scale,total+16*scale,84*scale};r.lineStep=54*scale;
    for(std::size_t i=0;i<digits;++i)r.columns.push_back({(width-total)/2+i*(column+gap),centerY-38*scale,column,76*scale});
    return r;
}
inline SurvivalSelectionLayout survivalSelectionLayout(float width,float height,bool thumbnail,bool ldmSupported) {
    (void)ldmSupported; // Unsupported maps retain a disabled NO LDM control.
    SurvivalSelectionLayout r;
    r.scale=std::min({1.f,width/400.f,height/320.f});
    width/=r.scale;height/=r.scale;
    float const margin=14;
    r.safe={margin,margin,width-2*margin,height-2*margin};
    r.header={margin,height-43,width-2*margin,30};r.subtitle={margin,height-62,width-2*margin,13};
    r.carousel={margin,height*.5f-65,width-2*margin,130};
    r.numberFinal={margin,height-98,width-2*margin,32};
    float const panelWidth=std::min(660.f,width-2*margin),left=(width-panelWidth)/2;
    float const top=height-(thumbnail?76.f:110.f),bottom=thumbnail?52.f:58.f;
    r.panel={left,bottom,panelWidth,std::min(thumbnail?220.f:186.f,top-bottom)};
    // Anchor to the header on taller iPad viewports instead of stretching rows.
    r.panel.y=top-r.panel.h;
    float const thumbWidth=(panelWidth-28)*.41f;
    r.thumbnail={left+12,r.panel.top()-12-thumbWidth*9/16,thumbWidth,thumbWidth*9/16};
    float infoLeft=thumbnail?r.thumbnail.right()+12:left+12;
    float infoWidth=left+panelWidth-12-infoLeft;
    r.compact=thumbnail&&infoWidth<300;
    float infoTop=r.panel.top()-10;
    r.title={infoLeft,infoTop-31,infoWidth,28};
    r.creator={infoLeft,infoTop-49,infoWidth,14};
    r.difficulty={infoLeft,infoTop-75,infoWidth*.57f,21};
    r.metadata={infoLeft+infoWidth*.59f,infoTop-75,infoWidth*.41f,21};
    if(!thumbnail){
        r.title.w=infoWidth*.56f;r.creator.w=r.title.w;
        r.difficulty={infoLeft+infoWidth*.60f,infoTop-25,infoWidth*.40f,21};
        r.metadata={r.difficulty.x,infoTop-49,r.difficulty.w,18};
    }
    float controlsLeft=thumbnail&&!r.compact?infoLeft:left+12;
    float controlsWidth=thumbnail&&!r.compact?infoWidth:panelWidth-24;
    float const toggleWidth=std::min(140.f,controlsWidth*.44f),readyWidth=std::min(104.f,controlsWidth*.29f);
    float const y=r.panel.y+19;
    r.ldm={controlsLeft,y+36,toggleWidth,32};r.shake={controlsLeft,y,toggleWidth,32};
    r.ready={controlsLeft+controlsWidth-readyWidth,y+18,readyWidth,34};
    auto const middle=controlsWidth-toggleWidth-readyWidth-16;
    auto const jukeboxWidth=std::min(96.f,middle);
    r.jukebox={controlsLeft+toggleWidth+8+(middle-jukeboxWidth)/2,y+18,jukeboxWidth,34};
    r.readiness={left+12,r.panel.y+3,panelWidth-24,12};
    r.hint={left,37,panelWidth,16};
    for(auto* box:{&r.safe,&r.header,&r.subtitle,&r.carousel,&r.numberFinal,&r.panel,&r.thumbnail,&r.title,&r.creator,&r.difficulty,&r.metadata,&r.ldm,&r.shake,&r.jukebox,&r.ready,&r.readiness,&r.hint}){
        box->x*=r.scale;box->y*=r.scale;box->w*=r.scale;box->h*=r.scale;
    }
    return r;
}
}
