// SPDX-License-Identifier: MIT
#include "benchmark_play_logic.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <array>
#include "actual_pad_clamp.hpp"
using namespace prime::benchmark_play;
static void polyline_steering_checks() {
    unsigned cases=0;float worst=0;
    for(unsigned heading=0;heading<8;++heading)
    for(float lag:{.08f,.16f,.3f})for(float drift:{-.2f,.2f})
    for(float speed:{5.5f,8.4f})for(float scale:{1.f,.55f})for(float initial:{0.f,.55f,-.55f})
    for(float leg:{2.f,6.f})for(unsigned shapeKind=0;shapeKind<3;++shapeKind) {
        const float angle=heading*.7853981634f,dx=std::cos(angle),dy=std::sin(angle);
        Route r;r.count=shapeKind==2?6:5;r.openness=30;
        const std::array<Point,6> shape=shapeKind==2?
            std::array<Point,6>{{{leg,leg,0},{0,0,0},{-leg,0,0},{-leg,-leg,0},{0,0,0},{leg,leg,0}}}:
            shapeKind==1?std::array<Point,6>{{{0,0,0},{leg,0,0},{leg,leg,0},{0,leg,0},{0,0,0},{}}}:
            std::array<Point,6>{{{0,0,0},{leg,0,0},{leg,leg,0},{2*leg,leg,0},{2*leg,2*leg,0},{}}};
        for(unsigned i=0;i<r.count;++i)r.waypoints[i]={95+shape[i].x*dx-shape[i].y*dy,-48+shape[i].x*dy+shape[i].y*dx,5};
        for(unsigned i=1;i<r.count;++i)r.length+=flat_distance(r.waypoints[i-1],r.waypoints[i]);
        r.start=r.waypoints[0];r.end=r.waypoints[r.count-1];
        const float first=flat_distance(r.start,r.waypoints[1]);
        r.dx=(r.waypoints[1].x-r.start.x)/first;r.dy=(r.waypoints[1].y-r.start.y)/first;
        Point p{r.start.x-r.dy*initial,r.start.y+r.dx*initial,r.start.z},velocity{};
        RouteCursor cursor;std::array<Point,4> delay{};unsigned turns=0;float distance=0;
        for(unsigned tick=0;tick<1800;++tick) {
            if(!inside_route(r,p)){std::fprintf(stderr,"polyline escape h%u lag%.2f drift%.2f speed%.1f scale%.2f initial%.2f tick%u pos%.3f/%.3f leg%u\n",heading,lag,drift,speed,scale,initial,tick,p.x,p.y,cursor.segment);std::abort();}
            const auto previous=cursor.segment;
            const auto goal=path_patrol(r,p,velocity,cursor);turns+=previous!=cursor.segment;
            const auto headingNow=path_heading(r,cursor);
            const float aim=std::atan2(headingNow.y,headingNow.x)+std::sin(float(tick)*.03f)*.55f;
            const Point forward{std::cos(aim),std::sin(aim),0};
            const auto stick=stick_for_world(goal,forward,scale);
            s8 x=s8(stick.x),y=s8(stick.y);ClampStick(&x,&y,72,40,15);
            const float localX=float(x)/72,localY=float(y)/72;
            const float gateLength=std::max(1.f,std::hypot(localX,localY));
            Point command{(localX*forward.y+localY*forward.x)/gateLength,
                          (-localX*forward.x+localY*forward.y)/gateLength,0};
            const auto old=delay[tick%delay.size()];delay[tick%delay.size()]=command;
            velocity.x+=(old.x*speed-velocity.x)/(60*lag);velocity.y+=(old.y*speed-velocity.y)/(60*lag);
            const Point before=p;p.x+=(velocity.x-dy*drift)/60;p.y+=(velocity.y+dx*drift)/60;
            distance+=flat_distance(before,p);
            float nearest=1e9f;
            for(unsigned i=1;i<r.count;++i){const auto a=r.waypoints[i-1],b=r.waypoints[i];const float length=flat_distance(a,b);const float along=std::clamp(((p.x-a.x)*(b.x-a.x)+(p.y-a.y)*(b.y-a.y))/(length*length),0.f,1.f);nearest=std::min(nearest,flat_distance(p,{a.x+(b.x-a.x)*along,a.y+(b.y-a.y)*along,p.z}));}
            worst=std::max(worst,nearest);
        }
        assert(inside_route(r,p));assert(turns>=4);assert(distance>30);++cases;
    }
    Route r;r.count=4;r.waypoints={Point{0,0,0},Point{6,0,0},Point{6,6,0},Point{12,6,0}};
    r.start=r.waypoints[0];r.end=r.waypoints[3];r.length=18;r.dx=1;
    assert(inside_route(r,{6,3,0}));assert(!inside_route(r,{3,3,0}));
    assert(!inside_route(r,{6,3,-1}));assert(inside_route(r,{6,3,4}));
    RouteCursor cursor{2,true};const auto reverse=path_patrol(r,{11.9f,6,0},{1,0,0},cursor);
    assert(!cursor.forward&&reverse.x<0);assert(path_heading(r,cursor).x<0);
    auto bad=r;bad.count=9;assert(!inside_route(bad,{}));assert(path_patrol(bad,{}, {},cursor).x==0);
    bad=r;bad.waypoints[1].x=std::numeric_limits<float>::quiet_NaN();assert(!inside_route(bad,{}));
    bad=r;bad.waypoints[3].x=std::numeric_limits<float>::quiet_NaN();assert(!inside_route(bad,{1,0,0}));
    std::printf("polyline %u actual-PAD delayed/drift cases; real2m/6m legs, left/right turns, revisited junctions and reversals, max centreline distance %.4fm, unchanged .65m guard\n",cases,worst);
}
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
static void route_lifecycle_checks() {
    const auto floor=[](Point q,Point& out) {
        out={q.x,q.y,.15f};
        return q.x>=-1.1f&&q.x<=6.1f&&std::fabs(q.y)<=.5f;
    };
    const auto clear=[](Point){return true;};
    const auto score=[](Point,float dx,float){return dx>.99f?30.f:0.f;};
    Route route;assert(choose_legacy_route({0,0,.15f},floor,clear,score,route));
    const auto original=route;
    // A legal endpoint within the unchanged guard can be a bad new search
    // anchor. This reproduces the lifecycle failure without inventing a room.
    const Point walked{2,.1f,.15f};assert(inside_route(route,walked));
    Route searched;assert(!choose_legacy_route(walked,floor,clear,score,searched));
    for(unsigned reset=0;reset<32;++reset) {
        assert(revalidate_legacy_route(walked,route,floor,clear,score));
        assert(route.start.x==original.start.x&&route.start.y==original.start.y&&route.start.z==original.start.z);
        assert(route.end.x==original.end.x&&route.end.y==original.end.y&&route.dx==original.dx&&route.dy==original.dy);
    }
    const auto blocked=[](Point q){return q.x<1.f||q.x>1.5f;};
    assert(!revalidate_legacy_route(walked,route,floor,blocked,score));
    const auto cliff=[](Point q,Point& out){out={q.x,q.y,.15f};return q.x<3.f;};
    assert(!revalidate_legacy_route(walked,route,cliff,clear,score));
    assert(!revalidate_legacy_route({2,2,.15f},route,floor,clear,score));
    const auto obscured=[](Point,float,float){return 2.f;};
    assert(!revalidate_legacy_route(walked,route,floor,clear,obscured));
    const auto invalid=[](Point,float,float){return std::numeric_limits<float>::quiet_NaN();};
    assert(!revalidate_legacy_route(walked,route,floor,clear,invalid));
    auto bad=route;bad.dx=std::numeric_limits<float>::infinity();
    assert(!revalidate_legacy_route(walked,bad,floor,clear,score));
    // Failed checks never replace a previously valid route with zeroed data.
    assert(route.start.x==original.start.x&&route.length==original.length);
    std::puts("route lifecycle:32 stable phase/rehearsal resets; moving-anchor failure reproduced; new obstacles/cliffs/access/invalid view rejected");
}
static void coverage_route_checks() {
    const auto flat=[](Point q,Point&out){out={q.x,q.y,.15f};return std::fabs(q.x)<=14&&std::fabs(q.y)<=14;};
    const auto clear=[](Point){return true;};
    const auto score=[](Point,float,float){return 20.f;};
    Route route;assert(choose_route({0,0,.15f},flat,clear,score,route));
    assert(route.count>=3&&route.count<=8&&route.length>=12);
    unsigned turns=0;
    for(unsigned i=1;i<route.count;++i) {
        assert(corridor(route.waypoints[i-1],route.waypoints[i],flat,clear));
        if(i>1){const auto a=route.waypoints[i-2],b=route.waypoints[i-1],c=route.waypoints[i];
            turns+=std::fabs((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x))>.01f;}
    }
    assert(turns);const Route original=route;
    const Point walked=route.waypoints[route.count/2];
    for(unsigned i=0;i<32;++i){assert(revalidate_route(walked,route,flat,clear,score));
        assert(route.count==original.count&&route.start.x==original.start.x&&route.start.y==original.start.y);
        for(unsigned j=0;j<route.count;++j)assert(route.waypoints[j].x==original.waypoints[j].x&&route.waypoints[j].y==original.waypoints[j].y);}
    const auto blocked=[&](Point p){return flat_distance(p,original.waypoints[2])>.6f;};
    assert(!revalidate_route(walked,route,flat,blocked,score));
    const auto missing=[&](Point q,Point&out){return flat(q,out)&&flat_distance(q,original.waypoints[2])>.6f;};
    assert(!revalidate_route(walked,route,missing,clear,score));
    assert(route.count==original.count&&route.length==original.length);
    assert(!revalidate_route({40,40,.15f},route,flat,clear,score));
    const auto invalid=[](Point,float,float){return std::numeric_limits<float>::quiet_NaN();};
    assert(!choose_route({0,0,.15f},flat,clear,invalid,route));
    const auto narrow=[](Point q,Point&out){out={q.x,q.y,.15f};return std::fabs(q.y)<.6f;};
    assert(!choose_route({0,0,.15f},narrow,clear,score,route));
    const auto tiny=[](Point q,Point&out){out={q.x,q.y,.15f};return std::fabs(q.x)<2&&std::fabs(q.y)<2;};
    assert(!choose_route({0,0,.15f},tiny,clear,score,route));
    assert(!choose_route({std::numeric_limits<float>::infinity(),0,0},flat,clear,score,route));
    // A real connected turn is retained; the planner cannot cross missing ground
    // or claim a tiny straight patrol as completed coverage.
    const auto elbow=[](Point q,Point&out){out={q.x,q.y,.15f};return
        (q.x>=-2&&q.x<=14&&q.y>=-2&&q.y<=2)||
        (q.x>=10&&q.x<=14&&q.y>=-2&&q.y<=14);};
    assert(choose_route({0,0,.15f},elbow,clear,score,route));assert(route.length>=12&&route.count>=3);
    for(unsigned i=1;i<route.count;++i)assert(corridor(route.waypoints[i-1],route.waypoints[i],elbow,clear));
    // A small, real T junction can support a branched patrol even though
    // forbidding every revisit would reject its connected ground. Every metre
    // still follows a validated edge; three unique edges are mandatory.
    const auto junction=[](Point q,Point&out){out={q.x,q.y,.15f};return
        (std::fabs(q.x)<2.7f&&std::fabs(q.y)<.7f)||
        (std::fabs(q.x)<.7f&&q.y>-.7f&&q.y<2.7f);};
    assert(choose_route({0,0,.15f},junction,clear,score,route));
    assert(route.length>=12&&route.count>=3);
    unsigned repeated=0;for(unsigned i=0;i<route.count;++i)for(unsigned j=0;j<i;++j)
        repeated+=flat_distance(route.waypoints[i],route.waypoints[j])<.01f;
    assert(repeated);
    for(unsigned i=1;i<route.count;++i)assert(corridor(route.waypoints[i-1],route.waypoints[i],junction,clear));
    assert(revalidate_route(route.waypoints[1],route,junction,clear,score));
    const auto twoEdges=[](Point q,Point&out){out={q.x,q.y,.15f};return q.x>-.7f&&q.x<4.7f&&std::fabs(q.y)<.7f;};
    assert(!choose_route({0,0,.15f},twoEdges,clear,score,route));
    // Footprint support on an authored sloped floor raises the body above its
    // highest corner, without weakening the subsequent whole-body collision.
    const Point lo{-.5f,-.5f,0},hi{.5f,.5f,2.7f};Point supported;
    const auto ramp=[](Point q,Point&out){out={q.x,q.y,1.067f*q.y};return true;};
    assert(supported_floor({0,0,0},lo,hi,1.f,ramp,supported));
    assert(std::fabs(supported.z-.6835f)<.0001f);
    const auto unsafe=[](Point q,Point&out){out={q.x,q.y,0};return q.x<.4f;};
    assert(!supported_floor({0,0,0},lo,hi,1.f,unsafe,supported));
    const auto step=[](Point q,Point&out){out={q.x,q.y,q.x>.4f?1.1f:0.f};return true;};
    assert(!supported_floor({0,0,0},lo,hi,1.f,step,supported));
    const auto bad=[](Point q,Point&out){out={q.x,q.y,std::numeric_limits<float>::quiet_NaN()};return true;};
    assert(!supported_floor({0,0,0},lo,hi,1.f,bad,supported));
    unsigned descentChecks=0;const auto descent=[&](Point){++descentChecks;return true;};
    assert(safe_descent({0,0,4},{0,0,.15f},descent));assert(descentChecks>=17);
    const auto ceiling=[](Point q){return q.z<1||q.z>2;};
    assert(!safe_descent({0,0,4},{0,0,.15f},ceiling));
    assert(!safe_descent({0,0,4},{1,0,.15f},clear));
    assert(!safe_descent({0,0,0},{0,0,1},clear));
    assert(!safe_descent({0,0,10},{0,0,0},clear));
    std::puts("coverage:connected>=12m turning paths, .65m full-body corridors,32 stable resets, footprint/ramp support, bounded clear descent and rejected hazards/cliffs/obstacles passed");
}
int main(){
    coverage_route_checks();
    polyline_steering_checks();
    route_lifecycle_checks();
    steering_checks();
    const auto floor=[](Point desired,Point& p){p={desired.x,desired.y,.15f};return true;};
    const auto clear=[](Point){return true;};
    const auto view=[](Point,float dx,float){return dx>.9f?30.f:0.f;};
    Route route;assert(choose_legacy_route({0,0,.15f},floor,clear,view,route));
    assert(route.length==4&&route.dx>.9f&&route.openness==30);
    assert(inside_route(route,route.start));
    assert(inside_route(route,{2,0,.15f}));assert(inside_route(route,{2,0,3.f}));
    assert(!inside_route(route,{2,1.5f,.15f}));assert(!inside_route(route,{2,0,-2}));
    assert(!inside_route(route,{7,0,.15f}));assert(!inside_route({},{}));
    assert(!inside_route(route,{std::numeric_limits<float>::quiet_NaN(),0,0}));
    assert(!segment({std::numeric_limits<float>::infinity(),0,0},{0,0,0},floor,clear));
    assert(!choose_legacy_route({std::numeric_limits<float>::quiet_NaN(),0,0},floor,clear,view,route));
    const auto cliff=[](Point desired,Point& p){p={desired.x,desired.y,.15f};return desired.x<2.f;};
    assert(!segment({0,0,.15f},{4,0,.15f},cliff,clear));
    const auto wall=[](Point p){return p.x<2.f||p.x>3.f;};
    assert(!segment({0,0,.15f},{4,0,.15f},floor,wall));
    const auto narrow=[](Point p){return std::fabs(p.y)<.2f;};
    assert(!segment({0,0,.15f},{4,0,.15f},floor,narrow,.35f));
    const auto smallFloor=[](Point desired,Point& p){p={desired.x,desired.y,.15f};return std::fabs(p.x)<1&&std::fabs(p.y)<1;};
    assert(!choose_legacy_route({0,0,.15f},smallFloor,clear,view,route));
    const auto blockedView=[](Point,float,float){return 2.f;};
    assert(!choose_legacy_route({0,0,.15f},floor,clear,blockedView,route));
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
