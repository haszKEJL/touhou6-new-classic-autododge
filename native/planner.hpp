#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <utility>

struct Vec { float x{}, y{}; };
struct Bullet { Vec p, v; float radius; int age=-1; };
struct Item { Vec p, v; int type; bool homing=false; };
struct Enemy { Vec p, v, size; int life; bool boss, damageable, collidable; };
inline bool redPower(int type) { return type == 0 || type == 2 || type == 4; }
struct Direction { int x{}, y{}; bool operator==(const Direction&) const = default; };
#include "laser.hpp"
inline Vec velocity(Direction d, float speed) {
    float length = std::hypot(float(d.x), float(d.y));
    return length ? Vec{d.x*speed/length, d.y*speed/length} : Vec{};
}
// Velocities use native game units: pixels per simulation frame.
inline float clearance(Vec p, Vec v, float playerRadius, const std::vector<Bullet>& bullets,const std::vector<Laser>& lasers={}) {
    constexpr int horizon = 12;
    if ((v.x<0 && p.x+v.x<8) || (v.x>0 && p.x+v.x>376) ||
        (v.y<0 && p.y+v.y<16) || (v.y>0 && p.y+v.y>432))
        return -10000;
    float result = 10000;
    for (const auto& b : bullets) {
        Vec a=p;
        for(int frame=0;frame<horizon;++frame) {
            Vec next{std::clamp(a.x+v.x,8.f,376.f),std::clamp(a.y+v.y,16.f,432.f)};
            Vec r{b.p.x+b.v.x*frame-a.x,b.p.y+b.v.y*frame-a.y};
            Vec u{b.v.x-(next.x-a.x),b.v.y-(next.y-a.y)};
            float q=u.x*u.x+u.y*u.y;
            float t=q>0 ? std::clamp(-(r.x*u.x+r.y*u.y)/q,0.f,1.f) : 0;
            result=std::min(result,std::hypot(r.x+u.x*t,r.y+u.y*t)-b.radius-playerRadius-2.f);
            a=next;
        }
    }
    Vec at=p;
    for(int frame=0;frame<horizon && !lasers.empty();++frame) {
        Vec next{std::clamp(at.x+v.x,8.f,376.f),std::clamp(at.y+v.y,16.f,432.f)};
        result=std::min(result,laserClearance(at,next,playerRadius+2.f,lasers,float(frame)));
        at=next;
    }
    return result;
}
inline Direction plan(Vec p, float speed, float radius, const std::vector<Bullet>& bullets, Direction user,const std::vector<Laser>& lasers={}) {
    const float baseline = clearance(p, velocity(user, speed), radius, bullets,lasers);
    if (baseline > 4) return user;
    Direction best = user;
    float score = baseline;
    // Keep the user's movement on ties; stationary is a valid dodge.
    for (int x=-1; x<=1; ++x) for (int y=-1; y<=1; ++y) {
        Direction d{x,y};
        float candidate = clearance(p, velocity(d, speed), radius, bullets,lasers);
        if (candidate > score + .25f) { score = candidate; best = d; }
    }
    return best;
}
