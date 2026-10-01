#pragma once
#include "planner.hpp"
#include <array>
#include <optional>

namespace prediction {
inline constexpr int maxFrames=36;
struct Path {
    std::array<Vec,maxFrames+1> points{};
    int count=0, safeFrames=0;
    bool focus=false, searched=false;
    std::optional<Vec> target;
};
inline Path project(Vec p,Direction direction,float speed,int frames=12) {
    Path path; path.points[0]=p; path.count=1;
    Vec v=velocity(direction,speed);
    for(int frame=0;frame<std::clamp(frames,0,maxFrames);++frame) {
        p={std::clamp(p.x+v.x,8.f,376.f),std::clamp(p.y+v.y,16.f,432.f)};
        path.points[path.count++]=p;
    }
    return path;
}
// New Classic centers its 384x448 playfield in a 480-unit high canvas.
// Fit the 4:3 minimum canvas to handle window resizing and letterboxing.
struct Mapping {
    float scale=1,left=0,top=0;
    Vec point(Vec p) const { return {left+p.x*scale,top+p.y*scale}; }
};
inline Mapping mapping(float width,float height) {
    float scale=std::min(width/640.f,height/480.f);
    return {scale,(width-384.f*scale)*.5f,(height-480.f*scale)*.5f+16.f*scale};
}
}
