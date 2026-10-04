#pragma once
#include "behavior.hpp"

template<class Require> void scoringTests(Require require) {
    Vec p{192,350};
    std::vector<Item> blue{{{280,240},{0,1},1}};
    auto normal=intention(p,4,blue,{},{},false,true);
    auto scoring=intention(p,4,blue,{},{},false,true,128,true);
    require(normal.objective==Objective::Wait && scoring.objective==Objective::Collect && scoring.target->y<330,
        "Score priority collects blue points above the normal lower collection lane");
    require(intention(p,4,blue,{}, {1,0},true,false,128,true).objective==Objective::Manual,
        "Score priority never overrides manual F7 steering");
    require(!scoring::pickup(p,4,{{{192,240},{0,4},1,true},{{192,230},{0,4},6,true}},128),
        "Already homing point and cancel-star items need no chase");
    auto rare=scoring::pickup(p,4,{{{192,340},{0,1},1},{{64,200},{0,1},5}},128);
    require(rare && rare->x==64,"Score priority preserves extra-life pickup priority");
    auto power=scoring::pickup(p,4,{{{192,340},{0,1},1},{{216,320},{0,1},2}},32);
    require(power && power->x==216,"Score priority still builds power from large power drops");
    require(!scoring::pickup(p,4,{{{192,420},{0,2},1}},128),
        "Do not chase a falling point that will leave before it can be caught");
    require(scoring::pickup({192,400},4,{{{192,420},{0,2},1}},128).has_value(),
        "Rescue a reachable falling point near the bottom");
    require(!scoring::pickup({192,400},4,{{{192,420},{0,3},1}},128),
        "Respect the pickup box and lower playfield edge when a point falls too fast");
    SweepControl sweep;
    std::vector<Item> batch{{{160,180},{0,1},1},{{240,160},{0,1},0}};
    std::vector<Enemy> enemies{{{320,160},{},{24,24},20,false,true,true}};
    require(!sweep.update(0,p,0,batch,enemies,{}),"Normal autoplay keeps its small-batch combat behavior");
    require(sweep.update(.1,p,0,batch,enemies,{}, {},true),
        "New Classic score collection crosses POC without requiring full power or an empty enemy list");
    require(sweep.goal.y==112,"Score collection aims above the New Classic point-value threshold");
    std::vector<Item> coming{{{160,180},{0,-8},1,true},{{240,160},{0,-8},0,true}};
    require(sweep.update(1,{192,112},0,coming,enemies,{}, {},true),
        "Crossing POC begins a high collection hold instead of an immediate retreat");
    require(sweep.update(1.5,{192,112},0,coming,enemies,{}, {},true),
        "Hold above POC while attracted point items still need to arrive");
    require(!sweep.update(1.7,{192,112},0,{},enemies,{}, {},true),
        "Resume combat when the point batch has finished arriving");
    SweepControl danger;
    danger.update(0,{192,112},128,coming,{}, {},{},true);
    require(!danger.update(.1,{192,112},128,coming,{},{{{205,112},{-1,0},4}}, {},true),
        "New danger cancels the high score hold before contact");
    SweepControl urgent;
    require(!urgent.update(0,{192,400},128,{{{192,420},{0,2},1},{{160,100},{0,1},1}}, {},{}, {},true),
        "Rescue reachable bottom drops before committing to top collection");
    SweepControl flank;
    require(flank.update(0,p,128,{{{80,160},{0,1},1}}, {},{{{192,250},{},20}}, {},true) && flank.goal.x!=192,
        "Score collection finds a clear ascent column around a blocking enemy body");
    Laser across;across.origin={0,240};across.angle=0;across.end=across.maxLength=384;
    across.width=16;across.phase=1;across.duration=300;
    SweepControl blocked;
    require(!blocked.update(0,p,128,batch,{}, {},{across},true),
        "A laser covering the ascent cancels the score sweep");
    across.phase=0;across.startTime=18;
    require(!blocked.update(.1,p,128,batch,{}, {},{across},true),
        "Score ascent accounts for a warning laser becoming active during travel");
    SweepControl stars;
    require(!stars.update(0,p,128,{{{180,200},{0,-5},6,true},{{200,200},{0,-5},6,true}}, {},{}, {},true),
        "Homing cancel stars alone never trigger an unnecessary ascent");
    require(!stars.update(.1,p,128,{{{0,430},{0,3},1}}, {},{}, {},true),
        "A point expiring before POC arrival cannot justify an empty score ascent");
    require(sweep.update(3,p,128,batch,{}, {},{},true),"Score collection can begin a later batch after cooldown");
    sweep.cancel();
    require(!sweep.ascending && sweep.score.phase==scoring::Sweep::Phase::Idle,
        "Pause, disable and card strategies cancel both score ascent and collection hold");
}
