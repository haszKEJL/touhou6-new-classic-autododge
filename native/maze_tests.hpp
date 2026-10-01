#pragma once
#include "extra.hpp"

template<class Require> void mazeTests(Require require) {
    constexpr float tau=6.283185307f,quarter=1.570796327f;
    const Vec center{192,220};
    const std::vector<Enemy> boss{{center,{},{56,56},5000,true,true,true}};
    auto polar=[&](float angle,float radius=90.f) {
        return Vec{center.x+radius*std::cos(angle),center.y+radius*std::sin(angle)};
    };
    auto angleOf=[&](Vec p) { return std::atan2(p.y-center.y,p.x-center.x); };
    auto ring=[&](float gap,bool pinholes=false) {
        std::vector<Bullet> bullets;
        // A fresh inner emission keeps the phase active while the sampled ring
        // temporarily has no bullets at the orbit radius.
        bullets.push_back({center,{0,1},2});
        for(int i=0;i<120;++i) {
            float theta=i*tau/120;
            if(std::abs(std::remainder(theta-gap,tau))<.85f) continue;
            if(pinholes && (std::abs(std::remainder(theta-gap-1.35f,tau))<.16f ||
                            std::abs(std::remainder(theta-gap+1.35f,tau))<.16f)) continue;
            bullets.push_back({polar(theta),{},2});
        }
        return bullets;
    };

    ExtraStrategy sampling;
    sampling.update("ST_ECLDATA7_SUB48_0",1,polar(quarter),ring(quarter),{},boss);
    bool goalsAdvance=true;
    for(unsigned frame=2;frame<=18;++frame) {
        Vec player=polar(quarter-.025f*frame);
        // Bullets still exist, but none intersects the radius sampled by Maze.
        std::vector<Bullet> outside{{center,{0,1},2},{polar(.2f,145.f),{},2}};
        auto target=sampling.update("ST_ECLDATA7_SUB48_0",frame,player,outside,{},boss);
        float advance=-std::remainder(angleOf(*target)-angleOf(player),tau);
        goalsAdvance=goalsAdvance && advance>.15f && advance<.65f && sampling.phase==0;
    }
    require(goalsAdvance,"Maze keeps its target ahead during missing orbit-ring samples");

    ExtraStrategy corridor;
    bool mainGap=true;
    for(unsigned frame=1;frame<=160;++frame) {
        float gap=quarter-.018f*frame;
        auto target=corridor.update("ST_ECLDATA7_SUB48_0",frame,polar(gap+.06f),ring(gap,true),{},boss);
        mainGap=mainGap && std::abs(std::remainder(angleOf(*target)-gap,tau))<.75f;
    }
    require(mainGap,"Maze follows the large rotating opening rather than incidental side holes");

    ExtraStrategy moving;
    Vec player=polar(quarter);
    Direction previous{};
    float gap=quarter,travelled=0;
    int stops=0,backward=0;
    bool safe=true,inner=true,targetsAdvance=true;
    for(unsigned frame=1;frame<=360;++frame) {
        // Changing angular speed prevents this from testing a fixed key script.
        gap-=.024f+.003f*std::sin(frame*.075f);
        bool missing=frame%53>=24 && frame%53<29;
        auto bullets=missing ? std::vector<Bullet>{{center,{0,1},2}}:ring(gap);
        auto target=moving.update("ST_ECLDATA7_SUB48_0",frame,player,bullets,{},boss);
        if(missing) targetsAdvance=targetsAdvance && -std::remainder(angleOf(*target)-angleOf(player),tau)>.15f;
        auto threats=obstacles(bullets,boss);
        navigation::Forecast forecast(threats,{});
        auto move=navigation::choose(player,4,2,1.25,forecast,*target,{},true,false,previous,.8f,moving.orbitLane());
        Vec v=velocity(move.direction,move.focus ? 2.f:4.f);
        Vec next{player.x+v.x,player.y+v.y};
        float turn=-std::remainder(angleOf(next)-angleOf(player),tau);
        travelled+=turn;
        stops+=move.direction==Direction{};
        backward+=turn<-.002f;
        safe=safe && forecast.clearance(player,next,0,1.25)>0;
        float distance=std::hypot(next.x-center.x,next.y-center.y);
        inner=inner && distance>70 && distance<110;
        player=next; previous=move.direction;
    }
    require(safe && inner && travelled>5.8f,"Maze remains safe in the inner corridor across changing rotation speed");
    // Discrete 2/4-pixel movement may pause at the front of the moving gap.
    // Allow up to one quarter of frames to wait and a few corrective backsteps.
    require(stops<=90 && backward<=18,"Wide Maze corridor permits steady orbit with few stops or backward steps");
    require(targetsAdvance && moving.phase==0,"Short missing-ring intervals preserve forward orbit without reversing phase");

    Vec exposed=polar(quarter);
    std::vector<Bullet> obstruction{{{201,310},{},4}};
    navigation::Forecast forecast(obstruction,{});
    bool tangentSafe=true;
    Vec tangent=exposed;
    for(int frame=0;frame<4;++frame) {
        Vec next{tangent.x+4,tangent.y};
        tangentSafe=tangentSafe && forecast.clearance(tangent,next,frame,2.05f)>0;
        tangent=next;
    }
    auto escape=navigation::choose(exposed,4,2,1.25,forecast,polar(quarter-.4f),{},true,false,{},.8f,
                                  navigation::OrbitLane{center,90,-1},true);
    bool pathSafe=escape.safeFrames>0;
    for(int frame=0;frame<escape.safeFrames;++frame)
        pathSafe=pathSafe && forecast.clearance(escape.path.points[frame],escape.path.points[frame+1],frame,2.05f)>0;
    require(!tangentSafe && pathSafe && escape.direction!=Direction{1,0},
        "Maze forward preference yields a safe dodge when the direct tangent is blocked");
}
