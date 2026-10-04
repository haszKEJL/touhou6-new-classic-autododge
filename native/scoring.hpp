#pragma once
#include "planner.hpp"
#include <array>
#include <optional>

namespace scoring {
inline bool available(const Item& item) {
    return !item.homing && item.type>=0 && item.type<=6 && item.p.x>=0 && item.p.x<=384 && item.p.y>=-32 && item.p.y<440;
}
inline float expires(const Item& item) {
    return item.v.y>0 ? (448-item.p.y)/item.v.y:10000.f;
}
inline float catchTime(Vec player,float speed,const Item& item) {
    Vec d=subtract(item.p,player);
    constexpr float reach=7.5f; // The game's pickup box has an 8-unit half-size.
    float a=dot(item.v,item.v)-speed*speed,b=2*(dot(d,item.v)-speed*reach),c=dot(d,d)-reach*reach;
    float time=0;
    if(c>0) {
        if(std::abs(a)<.0001f) time=b<0 ? -c/b:10000.f;
        else {
            float discriminant=b*b-4*a*c;
            if(discriminant<0) return 10000;
            float first=(-b-std::sqrt(discriminant))/(2*a),second=(-b+std::sqrt(discriminant))/(2*a);
            time=std::min(first>=0 ? first:10000.f,second>=0 ? second:10000.f);
        }
    }
    Vec destination{item.p.x+item.v.x*time,item.p.y+item.v.y*time};
    Vec edge{std::clamp(destination.x,8.f,376.f),std::clamp(destination.y,16.f,432.f)};
    return std::hypot(destination.x-edge.x,destination.y-edge.y)<=reach ? time:10000.f;
}
inline std::optional<Vec> pickup(Vec player,float speed,const std::vector<Item>& items,int power) {
    std::optional<Vec> best;
    float score=std::numeric_limits<float>::infinity();
    for(const auto& item:items) {
        if(!available(item)) continue;
        float distance=std::hypot(item.p.x-player.x,item.p.y-player.y);
        float intercept=catchTime(player,std::max(1.f,speed),item);
        if(intercept>expires(item)) continue;
        float frames=std::clamp(intercept,0.f,45.f);
        Vec target{std::clamp(item.p.x+item.v.x*frames,8.f,376.f),std::clamp(item.p.y+item.v.y*frames,24.f,432.f)};
        float cost=distance;
        if(item.type==1) cost-=120+40*std::clamp((448-item.p.y)/320.f,0.f,1.f);
        if(redPower(item.type)) cost-=power<128 ? (item.type==4 ? 650.f:item.type==2 ? 240.f:140.f):70.f;
        if(item.type==3) cost-=1500;
        if(item.type==5) cost-=2000;
        if(item.type==6) cost-=15;
        cost-=140*std::clamp(1-expires(item)/30.f,0.f,1.f);
        int neighbors=0;
        for(const auto& other:items) if(available(other) && std::hypot(other.p.x-item.p.x,other.p.y-item.p.y)<48) ++neighbors;
        cost-=25*std::min(4,std::max(0,neighbors-1));
        if(cost<score) {score=cost;best=target;}
    }
    return best;
}

// A nominal diagonal ascent is checked at every frame, including relative
// bullet motion and warning-beam activation. It is rechecked during play;
// the shared movement planner still checks the actual route it sends.
inline bool pathClear(Vec player,Vec goal,float speed,float radius,
                      const std::vector<Bullet>& threats,const std::vector<Laser>& lasers,int hold=0) {
    float distance=std::hypot(goal.x-player.x,goal.y-player.y);
    int travel=int(std::ceil(distance/std::max(speed,1.f)));
    if(travel>120) return false;
    Vec at=player;
    for(int frame=0;frame<travel+hold;++frame) {
        float fraction=travel ? std::min(1.f,float(frame+1)/travel):1.f;
        Vec next{player.x+(goal.x-player.x)*fraction,player.y+(goal.y-player.y)*fraction};
        for(const auto& b:threats) {
            Vec r{b.p.x+b.v.x*frame-at.x,b.p.y+b.v.y*frame-at.y};
            Vec u{b.v.x-next.x+at.x,b.v.y-next.y+at.y};
            float q=dot(u,u),t=q>0 ? std::clamp(-dot(r,u)/q,0.f,1.f):0;
            if(std::hypot(r.x+u.x*t,r.y+u.y*t)<=b.radius+radius+8) return false;
        }
        if(laserClearance(at,next,radius+8,lasers,float(frame))<=0) return false;
        at=next;
    }
    return true;
}
struct Sweep {
    enum class Phase { Idle, Ascend, Gather };
    Phase phase=Phase::Idle;
    Vec goal{};
    double ready=0,gatheredAt=0;
    void cancel() {phase=Phase::Idle;}
    bool update(double now,Vec p,float speed,float radius,const std::vector<Item>& items,
                const std::vector<Enemy>& enemies,const std::vector<Bullet>& threats,const std::vector<Laser>& lasers) {
        int count=0,points=0,powerValue=0;
        bool homingPoints=false,urgent=false,rareHigh=false;
        float pointX=0;
        float ascent=std::hypot(phase==Phase::Ascend ? p.x-goal.x:0.f,p.y-112)/std::max(speed,1.f);
        for(const auto& item:items) {
            if(item.type==1 && item.homing) homingPoints=true;
            if(!available(item)) continue;
            bool rare=item.type==3 || item.type==5;
            float lifetime=expires(item);
            bool reachable=catchTime(p,std::max(speed,1.f),item)<=lifetime;
            urgent=urgent || (reachable && ((rare && item.p.y>=260) || (item.p.y>360 && lifetime<ascent+12)));
            if(lifetime<=ascent+2) continue;
            ++count;
            if(item.type==1) {++points;pointX+=item.p.x;}
            if(redPower(item.type)) powerValue+=item.type==4 ? 128:item.type==2 ? 8:1;
            rareHigh=rareHigh || (rare && item.p.y<260);
        }
        if(phase==Phase::Gather) {
            if((!homingPoints && now-gatheredAt>.15) || now-gatheredAt>3 ||
               !pathClear(p,goal,speed,radius,threats,lasers,24)) {
                cancel();ready=now+.8;return false;
            }
            return true;
        }
        if(p.y<128 && (count || homingPoints) && now>=ready && pathClear(p,{p.x,std::min(p.y,112.f)},speed,radius,threats,lasers,24)) {
            phase=Phase::Gather;goal={p.x,std::min(p.y,112.f)};gatheredAt=now;return true;
        }
        if(!count || urgent) {cancel();return false;}
        if(phase==Phase::Ascend) {
            if(!pathClear(p,goal,speed,radius,threats,lasers,12)) {cancel();ready=now+.4;return false;}
            return true;
        }
        bool worthwhile=rareHigh || points>0 || count>=3 || powerValue>=8;
        if(now<ready || !worthwhile) return false;
        const Enemy* enemy=nullptr;
        for(const auto& e:enemies) if(e.damageable && e.life>0 && (!enemy || std::abs(e.p.x-p.x)<std::abs(enemy->p.x-p.x))) enemy=&e;
        std::array<float,5> columns{{p.x,points ? pointX/points:p.x,p.x-64,p.x+64,enemy ? enemy->p.x:p.x}};
        float best=std::numeric_limits<float>::infinity();
        for(float column:columns) {
            Vec candidate{std::clamp(column,24.f,360.f),112};
            float cost=std::hypot(candidate.x-p.x,candidate.y-p.y);
            bool useful=false;
            for(const auto& item:items) if(available(item) && expires(item)>cost/std::max(speed,1.f)+2) {useful=true;break;}
            if(!useful) continue;
            if(enemy) cost+=.2f*std::abs(candidate.x-enemy->p.x);
            if(cost<best && pathClear(p,candidate,speed,radius,threats,lasers,12)) {best=cost;goal=candidate;}
        }
        if(!std::isfinite(best)) return false;
        phase=Phase::Ascend;return true;
    }
};
} // namespace scoring
