#pragma once
#include "navigation.hpp"
#include <map>

// Maze's four-bullet fans draw a spiral. A corridor is the discontinuity
// between successive fans at the end of a turn, not the largest empty sector
// of an arbitrary circle. Its radial velocity gives the crossing deadline.
struct MazeCorridor {
    struct Gate { float angle=0,radius=0,speed=0,width=0; };
    std::vector<Gate> gates;
    void observe(Vec center,const std::vector<Bullet>& bullets) {
        gates.clear();
        struct Cohort { Vec sum{}; float distance=0,speed=0; int count=0; std::vector<float> angles; };
        std::map<int,Cohort> cohorts;
        for(const auto& b:bullets) {
            if(b.radius>=2.5f) continue;
            Vec r=subtract(b.p,center); float distance=std::hypot(r.x,r.y),speed=std::hypot(b.v.x,b.v.y);
            if(distance<1 || speed<.5f || dot(r,b.v)<distance*speed*.995f) continue;
            int age=b.age>=0 ? b.age:int(std::lround(distance/speed));
            auto& c=cohorts[age]; c.sum.x+=r.x/distance; c.sum.y+=r.y/distance;
            c.distance+=distance; c.speed+=speed; ++c.count; c.angles.push_back(std::atan2(r.y,r.x));
        }
        std::vector<float> rates;
        for(auto next=cohorts.begin();next!=cohorts.end();++next) {
            auto old=std::next(next); if(old==cohorts.end()) break;
            if(old->first-next->first>3 || next->second.count<3 || old->second.count<3) continue;
            float a=std::atan2(next->second.sum.y,next->second.sum.x),b=std::atan2(old->second.sum.y,old->second.sum.x);
            rates.push_back(std::abs(std::remainder(a-b,6.283185307f))/float(old->first-next->first));
        }
        if(rates.size()<6) return;
        std::sort(rates.begin(),rates.end()); float rate=rates[rates.size()/2];
        if(rate<.01f || rate>.25f) return;
        for(auto next=cohorts.begin();next!=cohorts.end();++next) {
            auto old=std::next(next); if(old==cohorts.end()) break;
            const auto& n=next->second; const auto& o=old->second; int dt=old->first-next->first;
            if(dt>3 || n.count<3 || o.count<3) continue;
            float a=std::atan2(n.sum.y,n.sum.x),b=std::atan2(o.sum.y,o.sum.x);
            float delta=std::remainder(a-b,6.283185307f);
            if(std::abs(delta)<rate*dt+.18f || std::abs(delta)>1.3f) continue;
            float spreadN=0,spreadO=0;
            for(float angle:n.angles) spreadN=std::max(spreadN,std::abs(std::remainder(angle-a,6.283185307f)));
            for(float angle:o.angles) spreadO=std::max(spreadO,std::abs(std::remainder(angle-b,6.283185307f)));
            float width=std::abs(delta)-spreadN-spreadO;
            if(width<.25f || spreadN>.2f || spreadO>.2f) continue;
            gates.push_back({std::remainder(b+delta*.5f,6.283185307f),
                (n.distance/n.count+o.distance/o.count)*.5f,(n.speed/n.count+o.speed/o.count)*.5f,width});
        }
        std::sort(gates.begin(),gates.end(),[](const Gate& a,const Gate& b){return a.radius>b.radius;});
    }
    std::optional<navigation::OrbitLane> route(Vec center,Vec player,float radius=90) const {
        navigation::OrbitLane lane{center,radius,0};
        float at=std::atan2(player.y-center.y,player.x-center.x);
        float playerRadius=std::hypot(player.x-center.x,player.y-center.y);
        // A crossed opening remains relevant until its fans clear the body.
        float crossingRadius=std::max(radius,std::min(playerRadius,radius+16));
        struct Waypoint {float time,angle;}; std::vector<Waypoint> points;
        points.push_back({0,at});
        for(const auto& gate:gates) {
            float time=(crossingRadius+8-gate.radius)/gate.speed;
            if(time<0) continue;
            float angle=points.back().angle+std::remainder(gate.angle-points.back().angle,6.283185307f);
            points.push_back({std::max(1.f,time-8.f/gate.speed),angle});
        }
        if(points.size()<2) return std::nullopt;
        lane.timed=true;
        if(points.size()>2) lane.direction=points[2].angle>points[1].angle ? 1.f:-1.f;
        for(int f=0;f<navigation::horizon;++f) {
            float t=float(f+1),angle=points.back().angle;
            for(size_t i=1;i<points.size();++i) if(t<=points[i].time) {
                float fraction=std::clamp((t-points[i-1].time)/std::max(1.f,points[i].time-points[i-1].time),0.f,1.f);
                angle=points[i-1].angle+(points[i].angle-points[i-1].angle)*fraction; break;
            }
            lane.waypoints[f]={center.x+radius*std::cos(angle),center.y+radius*std::sin(angle)};
        }
        return lane;
    }
};
