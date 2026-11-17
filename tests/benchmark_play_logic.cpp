// SPDX-License-Identifier: MIT
#include "benchmark_play_logic.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
#include <array>
#include "actual_pad_clamp.hpp"
using namespace prime::benchmark_play;
// Response simulation deliberately tests the controller and actual PADClamp,
// not game collision or physics. Hardware acceptance remains required.
static void steering_checks() {
    unsigned cases=0;float worst=0.f;
    for(unsigned heading=0;heading<8;++heading)
    for(float lag:{.08f,.16f,.3f})for(float drift:{-.2f,.2f})
    for(float speed:{5.5f,8.4f})for(float scale:{1.f,.55f})for(float initial:{0.f,.55f,-.55f}) {
        const float angle=heading*.7853981634f,dx=std::cos(angle),dy=std::sin(angle);
        Route r{{95.27023f,-48.10115f,5.1886f},{95.27023f+4*dx,-48.10115f+4*dy,5.1886f},dx,dy,4,30,false};
        Point p{r.start.x-dy*initial,r.start.y+dx*initial,r.start.z};
        Point velocity{};bool toEnd=true;unsigned turns=0;float distance=0;
        std::array<Point,4> delay{};
        for(unsigned tick=0;tick<1800;++tick) {
            assert(inside_route(r,p));
            const bool previous=toEnd;
            const Point goal=patrol(r,p,velocity,toEnd);turns+=previous!=toEnd;
            // A moving first-person aim or delayed Morph Ball camera frame.
            const float aim=angle+std::sin(float(tick)*.03f)*.55f;
            const Point forward{std::cos(aim),std::sin(aim),0};
            const auto stick=stick_for_world(goal,forward,scale);
            s8 x=s8(stick.x),y=s8(stick.y);ClampStick(&x,&y,72,40,15);
            const float localX=float(x)/72,localY=float(y)/72;
            const float gateLength=std::max(1.f,std::hypot(localX,localY));
            Point command{(localX*forward.y+localY*forward.x)/gateLength,
                          (-localX*forward.x+localY*forward.y)/gateLength,0};
            const Point old=delay[tick%delay.size()];delay[tick%delay.size()]=command;
            velocity.x+=(old.x*speed-velocity.x)/ (60*lag);
            velocity.y+=(old.y*speed-velocity.y)/ (60*lag);
            // Persistent 0.2m/s lateral bias exceeds the observed .662m/3.4s.
            const Point before=p;
            p.x+=(velocity.x-dy*drift)/60;p.y+=(velocity.y+dx*drift)/60;
            distance+=flat_distance(before,p);
            const float side=-(p.x-r.start.x)*dy+(p.y-r.start.y)*dx;
            worst=std::max(worst,std::fabs(side));
        }
        assert(inside_route(r,p));assert(turns>=6);assert(distance>20);++cases;
    }
    bool toEnd=true;
    Route r{{0,0,0},{4,0,0},1,0,4,30,false};
    const auto correction=patrol(r,{2,.6f,0},{0,.2f,0},toEnd);
    assert(correction.y<0);assert(pad_axis(.02f)>15);assert(pad_axis(-.02f)<-15);
    assert(pad_axis(0)==0&&pad_axis(2)==127&&pad_axis(-2)==-127);
    assert(pad_axis(std::numeric_limits<float>::quiet_NaN())==0);
    // The original open-loop route cannot correct the observed persistent drift.
    Point legacy{};unsigned failTick=0;
    for(;failTick<720&&inside_route(r,legacy);++failTick)legacy.y+=.2f/60;
    assert(failTick<210&&!inside_route(r,legacy));
    assert(stick_for_world({1,0,0},{0,0,0}).x==0);
    assert(stick_for_world({1,0,0},{0,1,0}).x==127);
    // A camera/body rotation must not rotate the requested world-space motion.
    assert(stick_for_world({1,0,0},{1,0,0}).y==127);
    std::printf("closed-loop %u lag/drift/rounding cases, max lateral %.4fm; guard .6500m unchanged\n",cases,worst);
}
int main(){
    steering_checks();
    const auto floor=[](Point desired,Point& p){p={desired.x,desired.y,.15f};return true;};
    const auto clear=[](Point){return true;};
    const auto view=[](Point,float dx,float){return dx>.9f?30.f:0.f;};
    Route route;assert(choose_route({0,0,.15f},floor,clear,view,route));
    assert(route.length==4&&route.dx>.9f&&route.openness==30);
    assert(inside_route(route,route.start));
    assert(inside_route(route,{2,0,.15f}));assert(inside_route(route,{2,0,3.f}));
    assert(!inside_route(route,{2,1.5f,.15f}));assert(!inside_route(route,{2,0,-2}));
    assert(!inside_route(route,{7,0,.15f}));assert(!inside_route({},{}));
    assert(!inside_route(route,{std::numeric_limits<float>::quiet_NaN(),0,0}));
    assert(!segment({std::numeric_limits<float>::infinity(),0,0},{0,0,0},floor,clear));
    assert(!choose_route({std::numeric_limits<float>::quiet_NaN(),0,0},floor,clear,view,route));
    const auto cliff=[](Point desired,Point& p){p={desired.x,desired.y,.15f};return desired.x<2.f;};
    assert(!segment({0,0,.15f},{4,0,.15f},cliff,clear));
    const auto wall=[](Point p){return p.x<2.f||p.x>3.f;};
    assert(!segment({0,0,.15f},{4,0,.15f},floor,wall));
    const auto narrow=[](Point p){return std::fabs(p.y)<.2f;};
    assert(!segment({0,0,.15f},{4,0,.15f},floor,narrow,.35f));
    const auto smallFloor=[](Point desired,Point& p){p={desired.x,desired.y,.15f};return std::fabs(p.x)<1&&std::fabs(p.y)<1;};
    assert(!choose_route({0,0,.15f},smallFloor,clear,view,route));
    const auto blockedView=[](Point,float,float){return 2.f;};
    assert(!choose_route({0,0,.15f},floor,clear,blockedView,route));
    for(unsigned phase=0;phase<4;++phase)for(unsigned tick=0;tick<720;++tick){
        const auto controls=input(phase,tick,phase==3&&tick>20,true,true);
        assert(!controls.jump||phase==1&&tick<6);
        assert(!controls.missile||phase==2);
        assert(!controls.morph||phase==3&&tick<6);
        if(phase==3&&tick<20)assert(!controls.move&&!controls.fire);
    }
    assert(!input(1,0,false,false,false).jump);
    std::puts("bounded route, wall/cliff/clearance, invalid poses and 2880 normal-input phase cases passed");
}
