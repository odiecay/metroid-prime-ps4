// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>

namespace prime::benchmark_play {
struct Point { float x{},y{},z{}; };
struct Route { Point start{},end{}; float dx{},dy{},length{},openness{}; bool jumpSafe{}; };
inline bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline float flat_distance(Point a,Point b) { return std::hypot(a.x-b.x,a.y-b.y); }
// This bounded search is run during placement, never during measured frames.
// Floor validates the real floor and hazards; Clear checks the whole character.
template<class Floor,class Clear>
bool segment(Point a,Point b,Floor&& floor,Clear&& clear,float width=0.f) {
    const float distance=flat_distance(a,b);
    if(!finite(a)||!finite(b)||!std::isfinite(distance)||distance>128)return false;
    const unsigned count=std::max(1u,unsigned(std::ceil(distance/.5f)));
    const float dx=(b.x-a.x)/std::max(distance,.001f),dy=(b.y-a.y)/std::max(distance,.001f);
    Point previous=a;
    for(unsigned i=0;i<=count;++i) {
        const float t=float(i)/count;Point desired{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
        Point p;
        if(!floor(desired,p)||!finite(p)||std::fabs(p.z-previous.z)>.5f||!clear(p))return false;
        previous=p;
        if(width>0)for(float side:{-width,width}) {
            Point edge{p.x-dy*side,p.y+dx*side,p.z},out;
            if(!floor(edge,out)||!finite(out)||std::fabs(out.z-p.z)>.35f||!clear(out))return false;
        }
    }
    return true;
}
template<class Floor,class Clear,class Score>
bool choose_route(Point origin,Floor&& floor,Clear&& clear,Score&& score,Route& best) {
    if(!finite(origin))return false;
    bool found=false;float bestScore=8.f;
    for(unsigned candidate=0;candidate<9;++candidate) {
        const float angle=(candidate?candidate-1:0)*.7853981634f;
        Point desired{origin.x+(candidate?4.f*std::cos(angle):0),origin.y+(candidate?4.f*std::sin(angle):0),origin.z},a;
        if(!floor(desired,a)||!segment(origin,a,floor,clear))continue;
        for(unsigned heading=0;heading<8;++heading) {
            const float yaw=heading*.7853981634f,dx=std::cos(yaw),dy=std::sin(yaw);
            Point before{a.x-dx,a.y-dy,a.z},after{a.x+6*dx,a.y+6*dy,a.z},back,front,end;
            if(!floor(before,back)||!floor(after,front)||!segment(back,front,floor,clear,.35f)||
               !floor({a.x+4*dx,a.y+4*dy,a.z},end))continue;
            const float openness=score(a,dx,dy);
            if(!std::isfinite(openness)||openness<=bestScore)continue;
            best={a,end,dx,dy,4.f,openness,false};found=true;bestScore=openness;
        }
    }
    return found;
}
inline bool inside_route(const Route& r,Point p) {
    if(!(r.length>0)||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
    const float x=p.x-r.start.x,y=p.y-r.start.y;
    const float along=x*r.dx+y*r.dy,side=-x*r.dy+y*r.dx;
    const float z=r.start.z+(r.end.z-r.start.z)*std::clamp(along/r.length,0.f,1.f);
    return along>=-.65f&&along<=5.6f&&std::fabs(side)<=.65f&&p.z>=z-.65f&&p.z<=z+4.5f;
}
struct Input { bool fire{},missile{},jump{},morph{},lock{},move{}; };
inline Input input(unsigned phase,unsigned tick,bool morphed,bool jumpSafe,bool enemyTarget) {
    Input out;out.move=true;
    if(phase==1) {
        out.move=!jumpSafe||tick>=120;
        out.jump=jumpSafe&&tick<6;
        out.fire=tick<120||tick%24<6;
    } else if(phase==2) {out.missile=tick%90<5;out.lock=enemyTarget;}
    else if(phase==3) {
        out.morph=!morphed&&tick<6;out.move=morphed;
        out.fire=morphed&&tick%60<3;
    }
    return out;
}
}
