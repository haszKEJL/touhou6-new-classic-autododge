#pragma once
#include "extra.hpp"
template<class Require> void extraTests(Require require) {
    const std::vector<Enemy> boss{{{192,96},{},{56,56},5000,true,true,true}};
    Vec p{192,365};
    for(const auto& profile:extraProfiles) {
        ExtraStrategy strategy;
        auto goal=strategy.update(profile.key,100,p,{},{},boss);
        require(goal && strategy.card==profile.card,"Each of thirteen script keys selects its own strategy");
        require(goal->x>=16 && goal->x<=368 && goal->y>=20 && goal->y<=420,"Every card waypoint stays inside playfield");
    }
    require(!extraProfile("ST_ECLDATA6_SUB36_0"),"Other stages cannot accidentally select Extra strategies");
    require(!extraProfile("ST_ECLDATA7_SUB99_0"),"Unknown card uses general planner rather than guessed script");
    ExtraStrategy laev;
    auto goal=laev.update("ST_ECLDATA7_SUB36_0",100,p,{},{},boss);
    require(goal && goal->x==192 && goal->y>96,"Laevateinn opens below boss instead of entering first swing");
    goal=laev.update("ST_ECLDATA7_SUB36_0",110,{36,200},{},{},boss);
    require(goal->y>96,"No premature upper escape before the opening swing");
    Laser sword; sword.origin={192,96}; sword.angle=1; sword.end=sword.maxLength=600;
    sword.width=80; sword.turn=.0392699f; sword.phase=1; sword.duration=500;
    goal=laev.update("ST_ECLDATA7_SUB36_0",120,{36,24},{},{sword},boss);
    require(goal->y==24 && goal->x==348,"After opening swing use upper route to the right");
    goal=laev.update("ST_ECLDATA7_SUB36_0",170,{82,24},{},{},boss);
    require(goal->x==348 && goal->y==24,"Do not descend just because a beam disappears");
    goal=laev.update("ST_ECLDATA7_SUB36_0",171,{82,150},{},{sword},boss);
    require(goal->x==348,"A child beam does not reset the strategic route");
    goal=laev.update("ST_ECLDATA7_SUB44_0",172,p,{},{},boss);
    require(goal->y==335 && laev.phase==0,"Changing cards resets previous route state");
    ExtraStrategy four;
    auto clones=boss; clones.push_back({{64,110},{},{32,32},400,false,true,true});
    goal=four.update("ST_ECLDATA7_SUB39_0",1,p,{},{},clones);
    require(goal->x==64,"Four of a Kind targets left damageable clone before central boss");
    ExtraStrategy maze;
    goal=maze.update("ST_ECLDATA7_SUB48_0",1,{192,216},{},{},boss);
    require(goal->x==192 && goal->y==330,"Maze waits below while boss enters from the top");
    ExtraStrategy clock;
    clock.update("ST_ECLDATA7_SUB57_0",1,p,{},{},boss);
    goal=clock.update("ST_ECLDATA7_SUB57_0",150,{192,240},{},{},boss);
    require(goal->x==192 && goal->y>240,"Clock retreats vertically before horizontal transition");
    sword.drift={2,0};
    goal=clock.update("ST_ECLDATA7_SUB57_0",151,{192,260},{},{sword},boss);
    require(goal->x==310,"Clock shifts right when laser formation moves right");
    ExtraStrategy none;
    goal=none.update("ST_ECLDATA7_SUB60_0",1,p,{},{},boss);
    require(goal->x==64 && goal->y==384,"Survival card starts from lower left");
    goal=none.update("ST_ECLDATA7_SUB60_0",2,{64,384},{},{},boss);
    require(goal->x==320 && goal->y==384,"Survival route moves across bottom first");
    goal=none.update("ST_ECLDATA7_SUB60_0",3,{320,384},{},{},boss);
    require(goal->x==320 && goal->y==72,"Survival route continues up right side");
    std::vector<Bullet> close{{{192,360},{0,1},3}};
    for(auto key:{"ST_ECLDATA7_SUB22_0","ST_ECLDATA7_SUB24_0","ST_ECLDATA7_SUB68_0"}) {
        ExtraStrategy strategy;
        goal=strategy.update(key,1,p,close,{},boss);
        require(goal->y>p.y,"Pressure triggers retreat in Selene, Stone and QED");
    }
    // Deterministic closed-loop regression: a wide warning activates after 18
    // frames. Replan each step and verify the selected speed and path remain safe.
    Vec current{192,350}; bool safe=true;
    Laser warning; warning.origin={192,0}; warning.angle=1.570796327f;
    warning.end=warning.maxLength=448; warning.width=80; warning.startTime=18; warning.duration=300;
    Direction last{};
    for(int frame=0;frame<30;++frame) {
        Laser now=warning;
        now.phase=frame<18 ? 0:1; now.timer=frame<18 ? frame:frame-18;
        navigation::Forecast forecast({},{now});
        auto move=navigation::choose(current,4,2,1.25,forecast,{192,350},{},true,false,last);
        Vec v=velocity(move.direction,move.focus ? 2.f:4.f);
        Vec next{current.x+v.x,current.y+v.y};
        if(laserClearance(current,next,1.25,{now},0)<=0) safe=false;
        current=next; last=move.direction;
    }
    require(safe,"Closed-loop route avoids activating beam without oscillating back into it");
    auto recovery=navigation::choose({192,350},4,2,1.25,navigation::Forecast({{{196,350},{},4}},{}),{192,350},{},true,false);
    require(recovery.safeFrames==0 && recovery.direction.x<0,"Overlapping threat does not freeze invulnerable player in place");
    const std::vector<Enemy> centered{{{192,220},{},{56,56},5000,true,true,true}};
    ExtraStrategy circling;
    current={192,310}; last={};
    float travelled=0; float before=std::atan2(current.y-220,current.x-192);
    bool stayedInside=true;
    for(int frame=1;frame<=400;++frame) {
        std::vector<Bullet> ring;
        float gap=1.570796327f-frame*.02f;
        for(int i=0;i<120;++i) {
            float theta=i*6.283185307f/120;
            if(std::abs(std::remainder(theta-gap,6.283185307f))>.7f)
                ring.push_back({{192+90*std::cos(theta),220+90*std::sin(theta)},{},2});
        }
        auto destination=circling.update("ST_ECLDATA7_SUB48_0",frame,current,ring,{},centered);
        auto move=navigation::choose(current,4,2,1.25,navigation::Forecast(ring,{}),*destination,{},true,false,last,.8f,circling.orbitLane());
        Vec v=velocity(move.direction,move.focus ? 2.f:4.f);
        current={current.x+v.x,current.y+v.y}; last=move.direction;
        stayedInside=stayedInside && std::hypot(current.x-192,current.y-220)<125;
        float angle=std::atan2(current.y-220,current.x-192);
        travelled-=std::remainder(angle-before,6.283185307f); before=angle;
    }
    require(travelled>6 && stayedInside,"Maze follows a rotating gap for a full INNER orbit");
    ExtraStrategy fighting; current={40,24}; last={};
    for(int frame=1;frame<=140;++frame) {
        auto destination=fighting.update("ST_ECLDATA7_SUB50_0",frame,current,{},{},boss);
        auto move=navigation::choose(current,4,2,1.25,navigation::Forecast({{{192,96},{},30}},{}),*destination,{},true,false,last);
        Vec v=velocity(move.direction,move.focus ? 2.f:4.f);
        current={current.x+v.x,current.y+v.y}; last=move.direction;
    }
    require(current.y>330 && std::abs(current.x-192)<35,"Starbow closed-loop escapes upper corner into firing lane");
    ExtraStrategy unknown;
    goal=unknown.update("ST_ECLDATA7_SUB48_0",1,{40,24},{},{},{});
    require(goal->y==365 && !unknown.haveAngle,"Missing boss cannot start orbit around guessed upper pivot");
    ExtraStrategy returnRoute;
    Laser returningSword=sword; returningSword.angle=-1.4f; returningSword.turn=-.0392699f;
    returnRoute.update("ST_ECLDATA7_SUB36_0",1,{36,24},{},{returningSword},boss);
    returnRoute.phase=3;
    returnRoute.update("ST_ECLDATA7_SUB36_0",2,{348,24},{},{returningSword},boss);
    current={348,24}; last={}; bool bodySafe=true;
    for(int frame=60;frame<200;++frame) {
        auto destination=returnRoute.update("ST_ECLDATA7_SUB36_0",frame,current,{},{},boss);
        navigation::Forecast obstacles({{{192,96},{},30}},{});
        auto move=navigation::choose(current,4,2,1.25,obstacles,*destination,{},true,false,last);
        Vec v=velocity(move.direction,move.focus ? 2.f:4.f);
        Vec next{current.x+v.x,current.y+v.y};
        bodySafe=bodySafe && obstacles.clearance(current,next,0,1.25)>0;
        current=next; last=move.direction;
    }
    require(bodySafe && current.y>165 && std::abs(current.x-192)<30,"Laevateinn closed-loop returns below boss without body collision");
    ExtraStrategy jitter;
    for(int frame=1;frame<=100;++frame)
        jitter.update("ST_ECLDATA7_SUB48_0",frame,{192.f+(frame%2 ? 4.f:-4.f),340},{},{},centered);
    require(jitter.phase==0 && jitter.orbit<.2f,"Maze oscillation does not count as completed revolutions");
    ExtraStrategy wide;
    goal=wide.update("ST_ECLDATA7_SUB48_0",1,{192,390},{},{},centered);
    require(std::hypot(goal->x-192,goal->y-220)<91,"Maze returns to inner ring after displacement");
    Laser parent; parent.origin={192,96}; parent.angle=-1; parent.turn=-.0392699f;
    parent.end=parent.maxLength=600; parent.width=24; parent.phase=0; parent.startTime=60; parent.timer=20; parent.duration=80;
    Laser child=parent; child.angle=-.96f; child.width=12; child.phase=1; child.timer=1; child.duration=6; child.turn=0;
    auto futureRay=laserSegment(parent,12);
    Vec exposed{parent.origin.x+(futureRay.second.x-parent.origin.x)*.2f,parent.origin.y+(futureRay.second.y-parent.origin.y)*.2f};
    require(navigation::Forecast({},{parent,child},true).clearance(exposed,exposed,12,1.25)<0,
        "Observed short rays are forecast while the parent sword still warns");
    require(navigation::Forecast({},{parent},true).clearance(exposed,exposed,12,1.25)>0,
        "A warning without observed damaging children remains non-colliding");
    // Geometry measured in th6_01 around the first fatal returning swing.
    ExtraStrategy recordedSwing;
    auto movingBoss=boss; movingBoss[0].p={104.882f,59.4351f};
    Laser observed=parent; observed.origin=movingBoss[0].p;
    observed.angle=-1.06029f; observed.turn=-.0392699f; observed.timer=23;
    recordedSwing.update("ST_ECLDATA7_SUB36_0",1,{36,24},{},{},movingBoss);
    recordedSwing.phase=3;
    goal=recordedSwing.update("ST_ECLDATA7_SUB36_0",2,{348,24},{},{observed},movingBoss);
    require(goal->y>movingBoss[0].p.y && recordedSwing.phase==4,
        "Recorded returning sword triggers descent while still warning");
    ExtraStrategy travel;
    travel.update("ST_ECLDATA7_SUB36_0",1,{36,24},{},{},boss);
    Laser opening=parent; opening.turn=.0392699f; opening.angle=.5f;
    travel.update("ST_ECLDATA7_SUB36_0",2,{36,24},{},{opening},boss);
    goal=travel.update("ST_ECLDATA7_SUB36_0",100,{100,24},{},{},boss);
    require(goal->x==348 && goal->y==24,"Gap after opening sword is used to cross right, not descend");
    ExtraStrategy reversal;
    std::vector<Bullet> emitted{{{192,240},{0,1},2}};
    reversal.update("ST_ECLDATA7_SUB48_0",1,{192,310},emitted,{},centered);
    for(unsigned frame=2;frame<30;++frame) reversal.update("ST_ECLDATA7_SUB48_0",frame,{192,310},{},{},centered);
    require(reversal.phase==0,"Maze does not reverse in the pause before new emission");
    reversal.update("ST_ECLDATA7_SUB48_0",30,{192,310},emitted,{},centered);
    require(reversal.phase==1,"Maze reverses when emission resumes after a genuine pause");
    reversal.update("ST_ECLDATA7_SUB48_0",30,{192,310},emitted,{},centered);
    require(reversal.phase==1,"Repeated memory polls cannot reverse Maze twice");
}
