// SPDX-License-Identifier: MIT
#include "benchmark_play_logic.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace prime::benchmark_play;
int main(){
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
