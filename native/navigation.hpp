#pragma once
#include "behavior.hpp"
#include "prediction.hpp"
#include <cstdint>

// Full routes are searched again every simulation tick. Safety outranks the
// preferred firing lane or pickup; the first action alone is sent to the game.
namespace navigation {
inline constexpr int horizon=36, stepFrames=4, beamWidth=10;
struct BeamSlice { Vec a,b; float begin,end,radius; };
struct Frame { std::vector<Bullet> bullets; std::vector<BeamSlice> beams; };
struct Forecast {
    std::array<Frame,horizon> frames;
    Forecast(const std::vector<Bullet>& bullets,const std::vector<Laser>& lasers,bool predictTrails=false) {
        std::vector<Laser> emitters;
        // NC Laevateinn's rotating warning creates damaging short-lived rays
        // BEFORE its own phase becomes active. Extrapolate this observed trail,
        // rather than interpreting the warning interval as empty space.
        for(const auto& parent:lasers) if(predictTrails && parent.phase!=2 && parent.duration>8) {
            for(const auto& child:lasers) if(child.phase==1 && child.duration<=8 && child.width<parent.width &&
                std::hypot(child.origin.x-parent.origin.x,child.origin.y-parent.origin.y)<80 &&
                std::abs(std::remainder(child.angle-parent.angle,6.283185307f))<.4f) {
                Laser emitter=parent; emitter.width=child.width;
                emitters.push_back(emitter); break;
            }
        }
        for(int frame=0;frame<horizon;++frame) {
            auto& out=frames[frame];
            out.bullets.reserve(bullets.size());
            for(const auto& b:bullets) out.bullets.push_back({{b.p.x+b.v.x*frame,b.p.y+b.v.y*frame},b.v,b.radius});
            for(const auto& laser:lasers) {
                auto [from,until]=laserWindow(laser);
                float end=std::min(float(frame+1),until);
                for(float t=std::max(float(frame),from);t<end;t+=.25f) {
                    float stop=std::min(t+.25f,end);
                    auto [a,b]=laserSegment(laser,(t+stop)*.5f);
                    float reach=std::max(std::abs(laser.start),std::abs(laser.end))+std::abs(laser.speed)*stop;
                    float motion=(std::hypot(laser.drift.x,laser.drift.y)+std::abs(laser.speed)+std::abs(laser.turn)*reach)*(stop-t)*.5f;
                    out.beams.push_back({a,b,t-frame,stop-frame,laser.width*.25f+motion});
                }
            }
            for(const auto& emitter:emitters) for(int lag=0;lag<=6;++lag) {
                float at=float(frame-lag);
                if(at<0) continue; // Existing rays account for the old trail.
                if(at>=laserWindow(emitter).second) continue;
                auto [a,b]=laserSegment(emitter,at+.5f);
                float reach=std::max(std::abs(emitter.start),std::abs(emitter.end));
                float motion=(std::hypot(emitter.drift.x,emitter.drift.y)+std::abs(emitter.turn)*reach)*.5f;
                out.beams.push_back({a,b,0,1,emitter.width*.25f+motion});
            }
        }
    }
    float clearance(Vec p,Vec next,int frame,float radius) const {
        float best=24;
        const auto& f=frames[frame];
        for(const auto& b:f.bullets) {
            Vec r=subtract(b.p,p), u{b.v.x-next.x+p.x,b.v.y-next.y+p.y};
            float reach=b.radius+radius+best;
            if(std::min(r.x,r.x+u.x)>reach || std::max(r.x,r.x+u.x)<-reach ||
               std::min(r.y,r.y+u.y)>reach || std::max(r.y,r.y+u.y)<-reach) continue;
            float q=dot(u,u),t=q>0 ? std::clamp(-dot(r,u)/q,0.f,1.f):0;
            best=std::min(best,std::hypot(r.x+u.x*t,r.y+u.y*t)-b.radius-radius);
            if(best<=0) return best;
        }
        for(const auto& b:f.beams) {
            auto at=[&](float t) { return Vec{p.x+(next.x-p.x)*t,p.y+(next.y-p.y)*t}; };
            best=std::min(best,segmentDistance(at(b.begin),at(b.end),b.a,b.b)-b.radius-radius);
            if(best<=0) return best;
        }
        return best;
    }
};
struct Decision { Direction direction{}; bool focus=false; int safeFrames=0; float margin=0; prediction::Path path{}; };
struct OrbitLane {
    Vec center{}; float radius=90, direction=0;
    bool timed=false;
    std::array<Vec,horizon> waypoints{};
};
struct Step { int8_t x=0,y=0; bool focus=false; };
struct Node {
    Vec p{};
    Direction first{},last{};
    bool firstFocus=false,lastFocus=false;
    float cost=0,margin=24,futurePenalty=0;
    int survived=0;
    std::array<Step,horizon/stepFrames> steps{};
};
inline Decision choose(Vec p,float fast,float slow,float radius,const Forecast& forecast,Vec target,
                       Direction wanted,bool preferSlow,bool forcedSlow,Direction previous={},float turnCost=.22f,
                       std::optional<OrbitLane> orbitLane=std::nullopt,bool capturePath=false) {
    std::vector<Node> beam(1); beam[0].p=p; beam[0].last=previous;
    Node fallback=beam.front();
    auto decision=[&](const Node& node,int safeFrames) {
        prediction::Path path;
        if(capturePath) {
            Vec at=p; path.points[0]=at; path.count=1;
            for(int frame=0;frame<std::max(1,safeFrames);++frame) {
                const auto step=safeFrames ? node.steps[frame/stepFrames]:Step{int8_t(node.first.x),int8_t(node.first.y),node.firstFocus};
                Vec v=velocity({step.x,step.y},step.focus ? slow:fast);
                at={std::clamp(at.x+v.x,8.f,376.f),std::clamp(at.y+v.y,16.f,432.f)};
                path.points[path.count++]=at;
            }
        }
        path.safeFrames=safeFrames; path.focus=node.firstFocus; path.searched=true; path.target=target;
        return Decision{node.first,node.firstFocus,safeFrames,node.margin,path};
    };
    for(int start=0;start<horizon;start+=stepFrames) {
        std::vector<Node> candidates;
        candidates.reserve(beam.size()*18);
        for(const auto& parent:beam) for(int focus=forcedSlow ? 1:0;focus<=1;++focus)
        for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) {
            Node node=parent; Direction d{x,y};
            if(capturePath) node.steps[start/stepFrames]={int8_t(x),int8_t(y),bool(focus)};
            if(start==0) { node.first=d; node.firstFocus=bool(focus); }
            Vec v=velocity(d,focus ? slow:fast);
            bool safe=true;
            for(int f=start;f<start+stepFrames;++f) {
                Vec next{std::clamp(node.p.x+v.x,8.f,376.f),std::clamp(node.p.y+v.y,16.f,432.f)};
                float room=forecast.clearance(node.p,next,f,radius+.8f);
                if(room<=0) { safe=false; break; }
                node.margin=std::min(node.margin,room); ++node.survived;
                node.cost+=2.f/(room+1.f);
                Vec waypoint=orbitLane && orbitLane->timed ? orbitLane->waypoints[f]:target;
                node.cost+=(orbitLane && orbitLane->timed ? .025f:.003f)*std::hypot(next.x-waypoint.x,next.y-waypoint.y);
                if(orbitLane) {
                    Vec before=subtract(node.p,orbitLane->center),after=subtract(next,orbitLane->center);
                    node.cost+=.035f*std::abs(std::hypot(after.x,after.y)-orbitLane->radius);
                    if(orbitLane->direction && !orbitLane->timed) {
                        float turn=std::remainder(std::atan2(after.y,after.x)-std::atan2(before.y,before.x),6.283185307f);
                        // Soft preference for forward motion; collision checks
                        // still reject every unsafe segment before scoring it.
                        node.cost+=.08f*std::max(0.f,1.2f-orbitLane->direction*turn*orbitLane->radius);
                    }
                }
                float edge=std::min({next.x-8,376-next.x,next.y-16,432-next.y});
                node.cost+=.12f*std::max(0.f,14-edge);
                node.p=next;
            }
            if(node.survived>fallback.survived || (node.survived==fallback.survived && node.cost<fallback.cost)) fallback=node;
            if(!safe) continue;
            if(d!=parent.last) node.cost+=turnCost;
            if(bool(focus)!=parent.lastFocus) node.cost+=.1f;
            if(start==0) {
                if(d!=wanted) node.cost+=.12f;
                if(bool(focus)!=preferSlow) node.cost+=.15f;
            }
            node.last=d; node.lastFocus=bool(focus);
            node.futurePenalty=0;
            for(int future=node.survived;future<horizon;future+=4) {
                Vec continued=node.p;
                if(orbitLane && orbitLane->timed) continued=orbitLane->waypoints[future];
                else if(orbitLane && orbitLane->direction) {
                    Vec relative=subtract(node.p,orbitLane->center); float distance=std::hypot(relative.x,relative.y);
                    float angle=std::atan2(relative.y,relative.x)+orbitLane->direction*(node.lastFocus ? slow:fast)*(future-node.survived)/std::max(distance,32.f);
                    continued={orbitLane->center.x+distance*std::cos(angle),orbitLane->center.y+distance*std::sin(angle)};
                }
                float room=forecast.clearance(continued,continued,future,radius+.8f);
                node.futurePenalty+=std::max(0.f,12-room)*4.f/float(future-node.survived+8);
            }
            candidates.push_back(node);
        }
        if(candidates.empty()) break;
        auto rank=[&](const Node& n) {
            Vec end=orbitLane && orbitLane->timed ? orbitLane->waypoints[std::min(horizon-1,n.survived-1)]:target;
            return n.cost+n.futurePenalty+.04f*std::hypot(n.p.x-end.x,n.p.y-end.y);
        };
        std::stable_sort(candidates.begin(),candidates.end(),[&](const Node& a,const Node& b){return rank(a)<rank(b);});
        beam.clear();
        // Keep spatial alternatives instead of filling the beam with nearly
        // identical routes. Preserve different first actions during pruning.
        for(const auto& candidate:candidates) {
            bool duplicate=false;
            for(const auto& kept:beam) if(candidate.first==kept.first && candidate.firstFocus==kept.firstFocus &&
                std::hypot(candidate.p.x-kept.p.x,candidate.p.y-kept.p.y)<8) { duplicate=true; break; }
            if(!duplicate) beam.push_back(candidate);
            if(beam.size()==beamWidth) break;
        }
        if(start+stepFrames==horizon) {
            const auto& best=beam.front();
            return decision(best,horizon);
        }
    }
    if(fallback.survived==0) {
        // Already inside the conservative margin (or temporarily invulnerable):
        // do not freeze. Move toward the least obstructed immediate endpoint.
        float best=-std::numeric_limits<float>::infinity();
        for(int focus=forcedSlow ? 1:0;focus<=1;++focus)
        for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) {
            Direction d{x,y}; Vec v=velocity(d,focus ? slow:fast);
            Vec next{std::clamp(p.x+v.x,8.f,376.f),std::clamp(p.y+v.y,16.f,432.f)};
            float room=forecast.clearance(next,next,1,radius);
            float score=room-.005f*std::hypot(next.x-target.x,next.y-target.y);
            if(score>best) { best=score; fallback.first=d; fallback.firstFocus=bool(focus); }
        }
    }
    return decision(fallback,fallback.survived);
}
} // namespace navigation

struct SweepSurvival {
    bool active=false,flankReached=false;
    int side=-1;
    Vec pivot{};
    uint32_t seen=0;
    std::optional<Vec> update(Vec player,const std::vector<Laser>& lasers,uint32_t frame) {
        const Laser* sword=nullptr;
        for(const auto& laser:lasers) if(laser.phase!=2 && laser.end-laser.start>220 && laser.origin.y<280 &&
            (laser.width>=64 || std::abs(laser.turn)>.004f)) {
            if(!sword || laser.width>sword->width) sword=&laser;
        }
        if(sword) {
            if(!active) { side=player.x<sword->origin.x ? -1:1; flankReached=false; }
            active=true; pivot=sword->origin; seen=frame;
        } else if(active && frame-seen>45) active=false;
        if(!active) return std::nullopt;
        float flank=std::clamp(pivot.x+side*145.f,28.f,356.f);
        if(std::abs(player.x-flank)<28 || player.y<pivot.y-35) flankReached=true;
        if(flankReached) return Vec{flank,40};
        return Vec{flank,std::clamp(pivot.y+90.f,150.f,280.f)};
    }
};
