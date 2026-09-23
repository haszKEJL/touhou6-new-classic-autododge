#pragma once
#include <array>
#include <optional>
#include <cstdint>
// Included after Vec/Direction: laser coordinates use the same playfield units.
struct Laser {
    Vec origin{}, drift{};
    float angle{}, turn{}, start{}, end{}, maxLength{}, speed{}, width{};
    int phase{}, timer{}, startTime{}, duration{};
    unsigned slot{};
};
class LaserTracker {
    std::array<std::optional<Laser>,64> previous{};
    uint32_t previousFrame=0;
public:
    void update(std::vector<Laser>& lasers,uint32_t frame) {
        uint32_t elapsed=frame-previousFrame;
        std::array<std::optional<Laser>,64> current{};
        for(auto& laser:lasers) {
            laser.turn=0; laser.drift={};
            const auto& old=previous[laser.slot];
            if(old && laser.phase>=old->phase && (laser.phase!=old->phase || laser.timer>=old->timer)) {
                if(elapsed==0 && std::abs(std::remainder(laser.angle-old->angle,6.283185307f))<=.15f &&
                   std::hypot(laser.origin.x-old->origin.x,laser.origin.y-old->origin.y)<=20) {
                    laser.drift=old->drift; laser.turn=old->turn;
                } else if(elapsed>0 && elapsed<=8) {
                    float turn=std::remainder(laser.angle-old->angle,6.283185307f)/float(elapsed);
                    Vec drift{(laser.origin.x-old->origin.x)/float(elapsed),(laser.origin.y-old->origin.y)/float(elapsed)};
                    // Reused slots / teleports are not continuous beam motion.
                    if(std::abs(turn)<=.15f && std::hypot(drift.x,drift.y)<=20) { laser.turn=turn; laser.drift=drift; }
                }
            }
            // Multiple polls can straddle the manager's update within one
            // simulation frame. Keep the first baseline until time advances.
            current[laser.slot]=(elapsed==0 && old) ? old:std::optional<Laser>(laser);
        }
        previous=current; previousFrame=frame;
    }
};
inline float dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y; }
inline Vec subtract(Vec a,Vec b) { return {a.x-b.x,a.y-b.y}; }
inline float pointSegmentDistance(Vec p,Vec a,Vec b) {
    Vec d=subtract(b,a), r=subtract(p,a);
    float q=dot(d,d), t=q>0 ? std::clamp(dot(r,d)/q,0.f,1.f):0;
    return std::hypot(r.x-d.x*t,r.y-d.y*t);
}
inline float cross(Vec a,Vec b) { return a.x*b.y-a.y*b.x; }
inline float segmentDistance(Vec a,Vec b,Vec c,Vec d) {
    Vec u=subtract(b,a),v=subtract(d,c),r=subtract(c,a);
    float denominator=cross(u,v);
    if(std::abs(denominator)>1e-6f) {
        float t=cross(r,v)/denominator, s=cross(r,u)/denominator;
        if(t>=0 && t<=1 && s>=0 && s<=1) return 0;
    }
    return std::min({pointSegmentDistance(a,c,d),pointSegmentDistance(b,c,d),
                     pointSegmentDistance(c,a,b),pointSegmentDistance(d,a,b)});
}
inline std::pair<Vec,Vec> laserSegment(const Laser& laser,float frame) {
    float end=laser.end+laser.speed*frame;
    float start=std::max({0.f,laser.start,end-laser.maxLength});
    end=std::max(start,end);
    float angle=laser.angle+laser.turn*frame;
    Vec axis{std::cos(angle),std::sin(angle)};
    Vec origin{laser.origin.x+laser.drift.x*frame,laser.origin.y+laser.drift.y*frame};
    return {{origin.x+axis.x*start,origin.y+axis.y*start},
            {origin.x+axis.x*end,origin.y+axis.y*end}};
}
inline std::pair<float,float> laserWindow(const Laser& laser) {
    if(laser.phase==2) return {0,0}; // NC calls collision only in phase 1.
    float from=laser.phase==0 ? float(std::max(0,laser.startTime-laser.timer)):0.f;
    float until=laser.phase==0 ? from+float(laser.duration)+1.f : float(laser.duration-laser.timer)+1.f;
    return {from,std::max(from,until)};
}
// Swept player segment against the entire beam. A capsule conservatively
// encloses the game's rotated rectangle (its half-width is width / 4).
// Quarter-frame subdivisions bound translation, growth and observed rotation.
inline float laserClearance(Vec p,Vec next,float radius,const std::vector<Laser>& lasers,float frame) {
    float result=10000;
    for(const auto& laser:lasers) {
        auto [from,until]=laserWindow(laser);
        float begin=std::max(frame,from), end=std::min(frame+1.f,until);
        for(float t=begin;t<end;t+=.25f) {
            float stop=std::min(t+.25f,end), middle=(t+stop)*.5f;
            auto interpolate=[&](float at) { return Vec{p.x+(next.x-p.x)*(at-frame),p.y+(next.y-p.y)*(at-frame)}; };
            auto [a,b]=laserSegment(laser,middle);
            float reach=std::max(std::abs(laser.start),std::abs(laser.end))+std::abs(laser.speed)*stop;
            float motion=(std::hypot(laser.drift.x,laser.drift.y)+std::abs(laser.speed)+std::abs(laser.turn)*reach)*(stop-t)*.5f;
            result=std::min(result,segmentDistance(interpolate(t),interpolate(stop),a,b)-laser.width*.25f-radius-motion);
        }
    }
    return result;
}
