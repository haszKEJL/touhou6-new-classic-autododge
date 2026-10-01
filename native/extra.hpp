#pragma once
#include "navigation.hpp"
#include "maze_corridor.hpp"
#include <string_view>

enum class ExtraCard { None, Selene, Flare, Stone, Cranberry, Laevateinn, Four,
                       Kagome, Maze, Starbow, Catadioptric, Clock, NoneLeft, Qed };
struct ExtraProfile { std::string_view key,name; ExtraCard card; };
inline constexpr std::array<ExtraProfile,13> extraProfiles{{
    {"ST_ECLDATA7_SUB22_0","Silent Selene",ExtraCard::Selene},
    {"ST_ECLDATA7_SUB23_0","Royal Flare",ExtraCard::Flare},
    {"ST_ECLDATA7_SUB24_0","Philosopher's Stone",ExtraCard::Stone},
    {"ST_ECLDATA7_SUB33_0","Cranberry Trap",ExtraCard::Cranberry},
    {"ST_ECLDATA7_SUB36_0","Laevateinn",ExtraCard::Laevateinn},
    {"ST_ECLDATA7_SUB39_0","Four of a Kind",ExtraCard::Four},
    {"ST_ECLDATA7_SUB44_0","Kagome Kagome",ExtraCard::Kagome},
    {"ST_ECLDATA7_SUB48_0","Maze of Love",ExtraCard::Maze},
    {"ST_ECLDATA7_SUB50_0","Starbow Break",ExtraCard::Starbow},
    {"ST_ECLDATA7_SUB54_0","Catadioptric",ExtraCard::Catadioptric},
    {"ST_ECLDATA7_SUB57_0","Clock That Ticks Away the Past",ExtraCard::Clock},
    {"ST_ECLDATA7_SUB60_0","And Then Will There Be None",ExtraCard::NoneLeft},
    {"ST_ECLDATA7_SUB68_0","Ripples of 495 Years",ExtraCard::Qed}
}};
inline const ExtraProfile* extraProfile(std::string_view key) {
    for(const auto& p:extraProfiles) if(p.key==key) return &p;
    return nullptr;
}
// These are strategic waypoints, never unconditional key sequences. Every
// segment still has to pass the shared collision and route search.
struct ExtraStrategy {
    ExtraCard card=ExtraCard::None;
    uint32_t entered=0,lastFrame=0;
    int phase=0,corner=0,laps=0;
    Vec boss{192,96};
    float orbit=0;
    Vec previousPlayer{};
    bool haveAngle=false,retreat=false,clockMoved=false,bossKnown=false;
    bool swordCenterReady=false,swordOpeningDone=false;
    bool mazeGapKnown=false;
    float mazeGap=1.570796327f;
    bool mazeEmitting=false,mazePause=false;
    uint32_t mazeLastEmission=0;
    MazeCorridor mazeCorridor;
    std::optional<navigation::OrbitLane> mazeRoute;
    bool mazeGateMode=false;
    std::optional<navigation::OrbitLane> orbitLane() const {
        if(mazeRoute) return mazeRoute;
        if(mazeGateMode) return std::nullopt;
        if(card==ExtraCard::Maze && bossKnown && boss.y>180) return navigation::OrbitLane{boss,90,mazePause ? 0.f:(phase==0 ? -1.f:1.f)};
        return std::nullopt;
    }
    const ExtraProfile* profile=nullptr;
    void reset() { *this=ExtraStrategy{}; }
    std::optional<Vec> update(std::string_view key,uint32_t frame,Vec p,
        const std::vector<Bullet>& bullets,const std::vector<Laser>& lasers,const std::vector<Enemy>& enemies) {
        const auto* selected=extraProfile(key);
        if(!selected) { reset(); return std::nullopt; }
        if(card!=selected->card || frame<lastFrame) {
            reset(); profile=selected; card=selected->card; entered=frame;
        }
        bool newFrame=frame!=lastFrame;
        lastFrame=frame;
        const Enemy* main=nullptr;
        for(const auto& e:enemies) if(e.boss && e.life>0 && (!main || e.life>main->life)) main=&e;
        if(main) { boss=main->p; bossKnown=true; }
        // Never orbit an invented pivot during a temporarily unavailable boss.
        if(!bossKnown) return Vec{192,365};
        float age=float(frame-entered),nearest=10000;
        int close=0;
        for(const auto& b:bullets) {
            float distance=std::hypot(b.p.x-p.x,b.p.y-p.y)-b.radius;
            nearest=std::min(nearest,distance);
            if(distance<80) ++close;
        }
        bool pressure=close>=4 || nearest<35;
        Vec target{boss.x,365};
        switch(card) {
        case ExtraCard::Selene:
        case ExtraCard::Stone:
            retreat=retreat || pressure || bullets.size()>90;
            target={boss.x,retreat ? std::min(416.f,std::max(p.y+18,boss.y+100)) : boss.y+95};
            break;
        case ExtraCard::Flare:
            // Cross the first sheets before settling below. Local avoidance
            // remains enabled; the guide's stationary finish is shot-dependent.
            if(phase==0 && (p.y<260 || age>150)) phase=1;
            if(phase==1 && age>240) phase=2;
            target={192,phase<2 ? 240.f:420.f};
            break;
        case ExtraCard::Cranberry:
            if(phase==0 && (pressure || bullets.size()>60)) phase=1;
            if(phase==1 && p.y>270) phase=2;
            target={boss.x,phase==0 ? boss.y+85:phase==1 ? 285.f:370.f};
            break;
        case ExtraCard::Laevateinn: {
            const Laser* sword=nullptr;
            for(const auto& l:lasers) if(l.phase!=2 && l.duration>8 && l.end-l.start>220 && (!sword || l.width>sword->width)) sword=&l;
            float swordAngle=sword ? std::remainder(sword->angle,6.283185307f):0;
            if(phase==0 && boss.x>=188 && boss.x<=196 && boss.y>=70) swordCenterReady=true;
            // The opening clockwise swing starts at upper left. Stay BELOW
            // Flandre while it passes overhead, then leave through its wake.
            if(phase==0 && sword && sword->turn>.005f && swordAngle>.25f && swordAngle<2.f) {
                phase=1; swordOpeningDone=true;
            }
            if(phase==0 && swordOpeningDone && swordCenterReady && boss.x<184 && boss.y<100) phase=1;
            if(phase==1 && p.x<60 && p.y<52) phase=2;
            if(phase==2 && boss.x>350) phase=3;
            // Do not chase Flandre left into her returning sword. Wait on the
            // right until the upward swing passes, then descend on its right.
            if(phase==3 && sword && sword->turn<-.005f && swordAngle<-.7f && swordAngle>-3.1f) phase=4;
            if(phase==4 && p.y>boss.y+75) { phase=0; swordCenterReady=false; }
            if(phase==0) target={boss.x,boss.y+100};
            else if(phase==1) target={20,24};
            else if(phase==2 || phase==3) target={348,24};
            else target={std::max(p.x,std::min(348.f,boss.x+90)),boss.y+100};
            break;
        }
        case ExtraCard::Four: {
            const Enemy* left=nullptr;
            for(const auto& e:enemies) if(e.damageable && e.life>0 && e.p.y>0 && e.p.y<260 && (!left || e.p.x<left->p.x)) left=&e;
            target={left ? left->p.x:boss.x,350};
            break;
        }
        case ExtraCard::Kagome:
            target={std::clamp(p.x,156.f,228.f),335};
            break;
        case ExtraCard::Maze: {
            // Flandre enters from the top, then teleports into the center.
            // Do not orbit that temporary entry position.
            if(boss.y<180) { target={192,330}; break; }
            mazeCorridor.observe(boss,bullets);
            if(!mazeCorridor.gates.empty()) mazeGateMode=true;
            for(const auto& b:bullets) if(b.radius<2.5f && b.age>=0) mazeGateMode=true;
            mazeRoute=mazeCorridor.route(boss,p);
            if(mazeRoute) {
                if(mazeRoute->direction) phase=mazeRoute->direction<0 ? 0:1;
                target=mazeRoute->waypoints[12];
                break;
            }
            if(mazeGateMode) {
                // The opening starts below Flandre. When the blue stream ends,
                // return there for the red opening rather than extrapolating
                // the last orbit through the emission pause.
                target={boss.x,boss.y+90};
                break;
            }
            bool emission=false;
            for(const auto& b:bullets) if(std::hypot(b.p.x-boss.x,b.p.y-boss.y)<38) { emission=true; break; }
            if(emission) {
                if(mazePause) { phase=1-phase; mazePause=false; mazeGapKnown=false; }
                mazeEmitting=true; mazeLastEmission=frame;
            } else if(mazeEmitting && frame-mazeLastEmission>18) mazePause=true;
            float angle=std::atan2(p.y-boss.y,p.x-boss.x);
            float sign=phase==0 ? -1.f:1.f;
            if(haveAngle && newFrame) {
                // Measure player motion about the SAME pivot, excluding boss
                // translation and repeated polls of the same simulation tick.
                float before=std::atan2(previousPlayer.y-boss.y,previousPlayer.x-boss.x);
                float delta=std::remainder(angle-before,6.283185307f);
                if(std::abs(delta)<.5f) orbit=std::max(0.f,orbit+sign*delta);
            }
            if(newFrame || !haveAngle) previousPlayer=p;
            haveAngle=true;
            constexpr int sectors=120;
            constexpr float tau=6.283185307f,radius=90;
            std::array<bool,sectors> blocked{};
            int count=0;
            for(const auto& b:bullets) {
                Vec q{b.p.x+b.v.x*12-boss.x,b.p.y+b.v.y*12-boss.y};
                float distance=std::hypot(q.x,q.y);
                if(std::abs(distance-radius)>b.radius+14) continue;
                float theta=std::atan2(q.y,q.x),half=std::asin(std::min(1.f,(b.radius+4)/radius));
                for(int i=0;i<sectors;++i) if(std::abs(std::remainder(i*tau/sectors-theta,tau))<half) blocked[i]=true;
                ++count;
            }
            if(!mazeGapKnown) {mazeGap=angle; mazeGapKnown=true;}
            float halfGap=0;
            if(count>0) {
                int widest=0;
                for(int i=0;i<sectors;++i) if(!blocked[i] && blocked[(i+sectors-1)%sectors]) {
                    int length=0; while(length<sectors && !blocked[(i+length)%sectors]) ++length;
                    widest=std::max(widest,length);
                }
                float best=-1e9f,selectedAngle=mazeGap;
                for(int i=0;i<sectors;++i) if(!blocked[i] && blocked[(i+sectors-1)%sectors]) {
                    int length=0;
                    while(length<sectors && !blocked[(i+length)%sectors]) ++length;
                    // Follow the large C-shaped opening rather than chasing
                    // incidental pinholes between neighboring bullets.
                    if(widest>=12 && length<.65f*widest) continue;
                    float middle=(i+(length-1)*.5f)*tau/sectors;
                    float difference=std::remainder(middle-mazeGap,tau);
                    float fromPlayer=std::remainder(middle-angle,tau);
                    float direction=phase==0 ? -1.f:1.f;
                    float score=length*tau/sectors-2*std::abs(difference)-4*std::max(0.f,-fromPlayer*direction);
                    if(score>best) {best=score; selectedAngle=mazeGap+difference; halfGap=(length-1)*.5f*tau/sectors;}
                }
                // Follow the observed corridor, including its reversal, rather
                // than switching after a guessed number of player revolutions.
                float delta=std::clamp(selectedAngle-mazeGap,-.06f,.06f);
                if(newFrame) mazeGap+=delta;
            }
            // Aim ahead on the arc, constrained by the observed main opening.
            // Missing ring samples must not freeze the goal at an old point.
            float nextAngle=angle+sign*.4f;
            if(halfGap>.18f) {
                float offset=std::remainder(angle-mazeGap,tau);
                nextAngle=mazeGap+std::clamp(offset+sign*.4f,-halfGap+.12f,halfGap-.12f);
            }
            if(mazePause) nextAngle=1.570796327f;
            target={boss.x+radius*std::cos(nextAngle),boss.y+radius*std::sin(nextAngle)};
            break;
        }
        case ExtraCard::Starbow: {
            // Fight below the boss; the guide's unverified corner pixel was
            // neither safe in this port nor an effective firing position.
            target={std::clamp(boss.x,64.f,320.f),365};
            break;
        }
        case ExtraCard::Catadioptric:
            target={std::clamp(boss.x,270.f,328.f),370};
            break;
        case ExtraCard::Clock: {
            if(phase==0 && (pressure || age>120)) phase=1;
            float drift=0;
            for(const auto& l:lasers) if(l.phase!=2) drift+=l.drift.x;
            if(phase==1 && drift>1) { phase=2; clockMoved=true; }
            if(phase==2 && p.x>290) phase=3;
            target=phase==0 ? Vec{boss.x,boss.y+90}:phase==1 ? Vec{p.x,std::min(410.f,p.y+35)}:
                   phase==2 ? Vec{310,std::min(400.f,p.y+12)}:Vec{192,420};
            break;
        }
        case ExtraCard::NoneLeft: {
            constexpr std::array<Vec,4> corners{{{64,384},{320,384},{320,72},{64,72}}};
            if(laps<2) {
                if(std::hypot(p.x-corners[corner].x,p.y-corners[corner].y)<30) {
                    corner=(corner+1)%4; if(corner==0) ++laps;
                }
                target=corners[corner];
            } else target={192,365};
            break;
        }
        case ExtraCard::Qed:
            retreat=retreat || pressure;
            target={192,retreat ? std::min(416.f,p.y+22):285.f};
            break;
        default:break;
        }
        target.x=std::clamp(target.x,16.f,368.f); target.y=std::clamp(target.y,20.f,420.f);
        return target;
    }
};
