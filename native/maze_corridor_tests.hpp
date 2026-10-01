#pragma once
#include "extra.hpp"

template<class Require> void mazeCorridorTests(Require require) {
    constexpr float tau=6.283185307f,speed=1.4625f;
    const Vec center{192,224};
    const std::vector<Enemy> enemies{{center,{},{56,56},5000,true,true,true}};
    struct Birth {unsigned frame; float angle;};
    std::vector<Birth> births;
    // The measured primary stream advances 0.2716 radians every two frames.
    // At the end of a spiral turn it jumps 0.6643. Nineteen- and twenty-fan
    // turns alternate, so the openings cannot be followed at a constant pace.
    float angle=1.570796327f-18*.2716f-.6643f*.5f;
    int inTurn=0,turn=0;
    for(unsigned frame=0;frame<600;frame+=2) {
        births.push_back({frame,angle});
        if(++inTurn==(turn%2 ? 20:19)) {angle+=.6643f;inTurn=0;++turn;}
        else angle+=.2716f;
    }
    auto bulletsAt=[&](unsigned frame,bool ages=true) {
        std::vector<Bullet> out;
        for(const auto& birth:births) if(birth.frame<frame && frame-birth.frame<180) {
            int age=int(frame-birth.frame);
            for(int fan=0;fan<4;++fan) {
                float a=birth.angle+(fan-1.5f)*.06545f;
                Vec v{speed*std::cos(a),speed*std::sin(a)};
                out.push_back({{center.x+v.x*age,center.y+v.y*age},v,2,ages ? age:-1});
            }
        }
        return out;
    };
    MazeCorridor observed,inferred;
    observed.observe(center,bulletsAt(160)); inferred.observe(center,bulletsAt(160,false));
    bool openings=observed.gates.size()>=3 && observed.gates.size()==inferred.gates.size();
    for(size_t i=0;i<observed.gates.size();++i) {
        openings=openings && observed.gates[i].width>.45f && observed.gates[i].width<.5f;
        if(i<inferred.gates.size()) openings=openings && std::abs(std::remainder(observed.gates[i].angle-inferred.gates[i].angle,tau))<.001f;
    }
    require(openings,"Maze detects real fan discontinuities with and without recorded bullet ages");
    auto route=observed.route(center,{center.x+90*std::cos(.9f),center.y+90*std::sin(.9f)});
    require(route && route->timed && route->direction<0,"Maze schedules the next opening in the counterclockwise spiral");
    if(route) {
        bool bounded=true;
        for(int f=1;f<navigation::horizon;++f)
            bounded=bounded && std::hypot(route->waypoints[f].x-route->waypoints[f-1].x,route->waypoints[f].y-route->waypoints[f-1].y)<4.1f;
        require(bounded,"Maze crossing deadlines produce a continuous reachable arc");
    }
    std::vector<Bullet> clipped=bulletsAt(150);
    // One surviving bullet per birth cohort cannot establish the fan edges.
    std::vector<Bullet> sparse;
    for(size_t i=0;i<clipped.size();i+=4) sparse.push_back(clipped[i]);
    observed.observe(center,sparse);
    require(observed.gates.empty(),"Clipped single-bullet cohorts do not invent spiral openings");
    ExtraStrategy strategy;
    Vec p{192,314}; Direction previous{}; bool safe=true,inner=true;
    int stops=0,backsteps=0; float travelled=0;
    for(unsigned frame=1;frame<=580;++frame) {
        auto bullets=bulletsAt(frame);
        auto target=strategy.update("ST_ECLDATA7_SUB48_0",frame,p,bullets,{},enemies);
        navigation::Forecast forecast(obstacles(bullets,enemies),{});
        auto move=navigation::choose(p,4,2,1.25f,forecast,*target,{},false,false,previous,.8f,strategy.orbitLane());
        Vec v=velocity(move.direction,move.focus ? 2.f:4.f),next{p.x+v.x,p.y+v.y};
        float turn=-std::remainder(std::atan2(next.y-center.y,next.x-center.x)-std::atan2(p.y-center.y,p.x-center.x),tau);
        if(frame>140) {stops+=move.direction==Direction{};backsteps+=turn<-.002f;travelled+=turn;}
        safe=safe && forecast.clearance(p,next,0,1.25f)>0;
        float r=std::hypot(next.x-center.x,next.y-center.y); inner=inner && r>70 && r<110;
        previous=move.direction;p=next;
    }
    require(safe && inner && travelled>5.5f,"Measured spiral stays collision-free through later unequal opening intervals");
    // At radius 90 the opening's mean tangential speed is about 1.4 pixels per
    // frame, below the game's minimum 2-pixel movement. Brief waits are needed;
    // backtracking through walls is not an acceptable way to fill that time.
    require(stops<160 && backsteps<12,"Measured spiral follows its openings without repeated backtracking through walls");
    auto opening=strategy.update("ST_ECLDATA7_SUB48_0",581,p,bulletsAt(581),{},enemies);
    (void)opening;
    auto before=strategy.orbitLane();
    auto withNewEmitter=bulletsAt(581);
    for(int age=1;age<=9;age+=2) for(int fan=0;fan<4;++fan) {
        float a=2.f-age*.1358f+(fan-1.5f)*.06545f;
        Vec v{speed*std::cos(a),speed*std::sin(a)};
        withNewEmitter.push_back({{center.x+v.x*age,center.y+v.y*age},v,2,age});
    }
    strategy.update("ST_ECLDATA7_SUB48_0",581,p,withNewEmitter,{},enemies);
    auto after=strategy.orbitLane();
    require(before && after && std::hypot(before->waypoints[12].x-after->waypoints[12].x,before->waypoints[12].y-after->waypoints[12].y)<2,
        "A reversed newborn emitter cannot reverse an opening still approaching the player");
    auto pause=strategy.update("ST_ECLDATA7_SUB48_0",582,p,{},{},enemies);
    require(pause && pause->x==center.x && pause->y==center.y+90 && !strategy.orbitLane(),
        "Maze returns below the boss when the last spiral opening has cleared");
    ExtraStrategy starting;
    auto start=starting.update("ST_ECLDATA7_SUB48_0",1,{192,330},bulletsAt(1),{},enemies);
    require(start && start->x==center.x && start->y==center.y+90,
        "Maze waits in its lower opening instead of orbiting before the first corridor exists");
}
