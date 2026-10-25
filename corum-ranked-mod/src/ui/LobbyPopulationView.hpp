#pragma once
#include "RankedVisuals.hpp"
#include "../domain/LobbyPopulation.hpp"
namespace corum::ranked::ui {
class LobbyPopulationView final:public CCNode {
    CCLabelBMFont *m_offline=nullptr,*m_online=nullptr,*m_slash=nullptr;
    CCDrawNode *m_red=nullptr,*m_green=nullptr;
    LobbyPopulation m_value{};bool m_drawn=false;
    bool init()override{
        if(!CCNode::init())return false;
        auto label=[&](std::string value){auto* p=menuLabel(value,.26f,{},Text,FontWeight::SemiBold);p->setAnchorPoint({0,.5f});addChild(p);return p;};
        m_offline=label("--");m_online=label("--");m_slash=label("/");
        m_red=CCDrawNode::create();m_red->drawDot({},2,ccc4f(1,.32f,.41f,1));addChild(m_red);
        m_green=CCDrawNode::create();m_green->drawDot({},2,ccc4f(.35f,.93f,.59f,1));addChild(m_green);
        updatePopulation({});return true;
    }
public:
    static LobbyPopulationView*create(){auto*p=new LobbyPopulationView;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
    void updatePopulation(LobbyPopulation value){
        if(m_drawn&&m_value==value)return;
        m_drawn=true;m_value=value;
        m_offline->setString(value.known?std::to_string(value.offline).c_str():"--");m_online->setString(value.known?std::to_string(value.online).c_str():"--");
        auto width=[](CCLabelBMFont* p){return p->getContentSize().width*p->getScaleX();};
        auto const a=width(m_offline),b=width(m_online),slash=width(m_slash),total=a+b+slash+28;
        float x=-total/2;m_red->setPosition({x+2,0});x+=8;m_offline->setPosition({x,0});x+=a+6;m_slash->setPosition({x,0});x+=slash+6;m_green->setPosition({x+2,0});x+=8;m_online->setPosition({x,0});
        setScale(std::min(1.f,220.f/std::max(1.f,total)));
    }
};
}
