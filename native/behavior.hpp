#pragma once
#include "planner.hpp"
#include <optional>

struct Toggle {
    bool enabled=false, previous=false;
    bool update(bool down) {
        bool changed=down && !previous;
        if(changed) enabled=!enabled;
        previous=down;
        return changed;
    }
};
enum class Objective { Manual, Collect, Attack, Wait, Sweep, Survive, Extra };
struct Intent { Direction direction; Objective objective=Objective::Manual; std::optional<Vec> target{}; };
inline const char* objectiveName(Objective objective) {
    switch(objective) {
    case Objective::Extra:return "extra-pattern";
    case Objective::Survive:return "survive-sweep";
    case Objective::Sweep:return "auto-collect";
    case Objective::Collect:return "collect";
    case Objective::Attack:return "attack";
    case Objective::Wait:return "position";
    default:return "manual";
    }
}
inline Direction toward(Vec player,Vec target,float speed,float deadzone=6) {
    float tolerance=std::max(deadzone,speed*1.1f);
    auto axis=[tolerance](float delta) { return delta>tolerance ? 1 : delta<-tolerance ? -1 : 0; };
    return {axis(target.x-player.x),axis(target.y-player.y)};
}
inline const Enemy* attackTarget(Vec player,const std::vector<Enemy>& enemies) {
    const Enemy* best=nullptr;
    float score=1e9f;
    for(const auto& enemy:enemies) {
        if(!enemy.damageable || enemy.life<=0 || enemy.p.x<0 || enemy.p.x>384 || enemy.p.y<0 || enemy.p.y>404) continue;
        float lane=std::abs(enemy.p.x-player.x);
        float cost=lane-.25f*enemy.p.y+(enemy.boss ? -45.f:0.f)-(lane<28 ? 55.f:0.f)-std::max(0.f,40.f-enemy.life);
        if(cost<score) { score=cost; best=&enemy; }
    }
    return best;
}
inline bool valuable(int type) { return type==3 || type==5; }
inline std::optional<Vec> pickupTarget(Vec player,float speed,const std::vector<Item>& items,const Enemy* enemy,
                                       bool autoplay=false,int power=128) {
    std::optional<Vec> best;
    float score=1e9f;
    for(const auto& item:items) {
        bool rare=valuable(item.type);
        if(item.homing || (!redPower(item.type) && !rare) || item.p.x<0 || item.p.x>384 || item.p.y>436) continue;
        if(!rare && item.p.y<16) continue;
        float distance=std::hypot(item.p.x-player.x,item.p.y-player.y);
        float frames=std::clamp(distance/std::max(speed,1.f),0.f,30.f);
        Vec target{std::clamp(item.p.x+item.v.x*frames,8.f,376.f),std::clamp(item.p.y+item.v.y*frames,24.f,424.f)};
        if(item.v.y>0 && (448-item.p.y)/item.v.y+4 < std::abs(item.p.x-player.x)/speed) continue;
        if(autoplay) {
            // Intercept falling items from below rather than following them up.
            if(!rare && item.type!=4 && item.p.y<280) continue;
            target.y=std::max(330.f,target.y);
            if(!rare && enemy) {
                float lane=power<128 ? 90.f:40.f;
                if(std::abs(target.x-enemy->p.x)>lane || distance>160) continue;
            }
        } else if(enemy && !rare) {
            if(std::abs(target.x-enemy->p.x)>28 || distance>100) continue;
        }
        float cost=distance+(autoplay ? std::abs(target.y-365.f)*.4f:0.f);
        if(item.type==2) cost-=20;
        if(item.type==4) cost-=300;
        if(rare) cost-=item.type==5 ? 2000.f:1500.f;
        if(cost<score) { score=cost; best=target; }
    }
    return best;
}
inline Intent intention(Vec player,float speed,const std::vector<Item>& items,const std::vector<Enemy>& enemies,
                        Direction user,bool collect,bool autoplay,int power=128) {
    // F7 assists collection when the user is not explicitly steering.
    if(!autoplay && (!collect || user!=Direction{})) return {user,Objective::Manual};
    const Enemy* enemy=autoplay ? attackTarget(player,enemies) : nullptr;
    if(auto item=pickupTarget(player,speed,items,enemy,autoplay,power)) return {toward(player,*item,speed),Objective::Collect,*item};
    if(enemy) {
        Vec target{std::clamp(enemy->p.x+enemy->v.x*4.f,12.f,372.f),
                   std::clamp(enemy->p.y+100.f,365.f,416.f)};
        return {toward(player,target,speed,14),Objective::Attack,target};
    }
    if(autoplay) {
        Vec target{std::clamp(player.x,64.f,320.f),365};
        return {toward(player,target,speed,16),Objective::Wait,target};
    }
    return {user,Objective::Manual};
}
inline std::vector<Bullet> obstacles(const std::vector<Bullet>& bullets,const std::vector<Enemy>& enemies) {
    auto result=bullets;
    for(const auto& enemy:enemies) if(enemy.collidable && enemy.life>0) {
        // Conservative enclosing circle for body contact, including rectangular enemies.
        float radius=std::hypot(enemy.size.x,enemy.size.y)*.5f+4;
        result.push_back({enemy.p,enemy.v,radius});
    }
    return result;
}

// Test short paths with a possible turn after three frames. Bomb only when
// none avoids contact, with either speed. This is a prediction, not a proof.
inline bool escapeAvailable(Vec p,float radius,const std::vector<Bullet>& threats,float fast,float slow,const std::vector<Laser>& lasers={}) {
    for(float speed:{fast,slow}) for(int ax=-1;ax<=1;++ax) for(int ay=-1;ay<=1;++ay)
    for(int bx=-1;bx<=1;++bx) for(int by=-1;by<=1;++by) {
        Vec at=p; bool safe=true;
        for(int frame=0;frame<6 && safe;++frame) {
            Vec v=velocity(frame<3 ? Direction{ax,ay}:Direction{bx,by},speed);
            Vec next{std::clamp(at.x+v.x,8.f,376.f),std::clamp(at.y+v.y,16.f,432.f)};
            for(const auto& b:threats) {
                Vec r{b.p.x+b.v.x*frame-at.x,b.p.y+b.v.y*frame-at.y};
                Vec u{b.v.x-next.x+at.x,b.v.y-next.y+at.y};
                float q=u.x*u.x+u.y*u.y;
                float t=q>0 ? std::clamp(-(r.x*u.x+r.y*u.y)/q,0.f,1.f):0;
                if(std::hypot(r.x+u.x*t,r.y+u.y*t)<=radius+b.radius+.35f) { safe=false; break; }
            }
            if(safe && laserClearance(at,next,radius+.35f,lasers,float(frame))<=0) safe=false;
            at=next;
        }
        if(safe) return true;
    }
    return false;
}
struct BombControl {
    double until=-1, ready=0;
    bool update(double now,bool eligible,bool trapped) {
        if(!eligible) { until=-1; return false; }
        if(now<until) return true;
        if(trapped && now>=ready) { until=now+.065; ready=now+.8; return true; }
        return false;
    }
};
// Ascend only through a conservatively clear corridor. Re-evaluate each tick
// so newly spawned threats immediately cancel the sweep.
inline bool collectionCorridor(Vec p,const std::vector<Bullet>& threats,const std::vector<Laser>& lasers={}) {
    // A warning beam can become active during the long ascent. Do not sweep
    // through its current or predicted corridor, even if it is harmless now.
    for(const auto& laser:lasers) if(laser.phase!=2) {
        for(float t=0;t<=80;t+=4) {
            auto [a,b]=laserSegment(laser,t);
            float reach=std::max(std::abs(laser.start),std::abs(laser.end))+std::abs(laser.speed)*t;
            float margin=28+laser.width*.25f+2*(std::hypot(laser.drift.x,laser.drift.y)+std::abs(laser.speed)+std::abs(laser.turn)*reach);
            if(segmentDistance(p,{p.x,112},a,b)<margin) return false;
        }
    }
    for(const auto& b:threats) {
        float margin=b.radius+28+std::hypot(b.v.x,b.v.y)*80;
        float y=std::clamp(b.p.y,std::min(p.y,112.f),std::max(p.y,112.f));
        if(std::hypot(b.p.x-p.x,b.p.y-y)<margin) return false;
    }
    return true;
}
struct SweepControl {
    bool ascending=false;
    double ready=0;
    bool update(double now,Vec p,int power,const std::vector<Item>& items,
                const std::vector<Enemy>& enemies,const std::vector<Bullet>& threats,const std::vector<Laser>& lasers={}) {
        int count=0,powerValue=0;
        bool rareHigh=false, rareLow=false;
        for(const auto& i:items) if(!i.homing && i.type!=6 && i.p.y<436) {
            ++count;
            if(redPower(i.type)) powerValue+=i.type==4 ? 128:i.type==2 ? 8:1;
            if(valuable(i.type)) { rareHigh=rareHigh || i.p.y<260; rareLow=rareLow || i.p.y>=260; }
        }
        if(p.y<128) { ascending=false; ready=now+3; return false; }
        if(!count || rareLow || !collectionCorridor(p,threats,lasers)) { ascending=false; return false; }
        bool fighting=attackTarget(p,enemies)!=nullptr;
        // Leave the bottom only for a valuable batch or a high rare drop.
        // Finish a committed ascent unless danger appears or an urgent drop is below.
        bool worthwhile=rareHigh || (!fighting && (count>=6 || (power<128 && powerValue>=12)));
        if(!ascending && now>=ready && worthwhile) ascending=true;
        return ascending;
    }
};
inline bool preferFocus(Vec p,const std::vector<Bullet>& threats,Intent intent,const std::vector<Laser>& lasers={}) {
    for(const auto& laser:lasers) if(laser.phase!=2) {
        auto [a,b]=laserSegment(laser,6);
        if(pointSegmentDistance(p,a,b)<70+laser.width*.25f) return true;
    }
    for(const auto& b:threats)
        if(std::hypot(b.p.x-p.x,b.p.y-p.y)<70+b.radius+std::hypot(b.v.x,b.v.y)*8) return true;
    return intent.objective==Objective::Attack && intent.direction.x==0;
}

