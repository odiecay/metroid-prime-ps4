// SPDX-License-Identifier: MIT
#include "benchmark_play_logic.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <array>
#include "actual_pad_clamp.hpp"
#include "port_mouse.h"
using namespace prime::benchmark_play;
struct LatchedPad { int x{},y{},aim{}; };
// Planar source-order model: real PADClamp and PortMouse::AxisForce, with the
// retail 200kg actor, 35000 acceleration, 16.5 cap and 1/tick normal friction.
// It does not simulate room collisions or stand in for console acceptance.
struct PlanarSourceOrder {
    Point position,velocity;float yaw{},right{},forward=1;
    void step(LatchedPad pad,bool inverted) {
        s8 aim=s8(pad.aim),zero=0;ClampStick(&aim,&zero,59,31,15);
        // UpdateMouseAim changes the body BEFORE the staged local movement.
        yaw-=float(aim)/59*(600*.0035f)/60*(inverted?-1:1);
        const Point f{-std::sin(yaw),std::cos(yaw),0},r{f.y,-f.x,0};
        s8 x=s8(pad.x),y=s8(pad.y);ClampStick(&x,&y,72,40,15);
        float sx=std::clamp(float(x)/72/.8f,-1.f,1.f),sy=std::clamp(float(y)/72/.8f,-1.f,1.f);
        const float rawX=std::fabs(sx*.8f),rawY=std::fabs(sy*.8f),raw=std::hypot(rawX,rawY);
        if(raw>1e-5f){const float a=std::max(rawX,rawY)/raw,b=std::min(rawX,rawY)/raw;
            const float gate=(40.f/72)/(40.f/72*a+32.f/72*b);sx/=gate;sy/=gate;}
        const auto input=PortMouse::ClampPlanar({sx,sy},1.f);sx=input.right;sy=input.forward;
        const float magnitude=std::hypot(sx,sy);
        if(magnitude>1e-5f){right=sx/magnitude;forward=sy/magnitude;}
        float vx=velocity.x*r.x+velocity.y*r.y,vy=velocity.x*f.x+velocity.y*f.y;
        float across=vx*forward-vy*right,along=vx*right+vy*forward;
        const auto damp=[](float v){return v>0?std::max(0.f,v-1):std::min(0.f,v+1);};
        across=damp(across);along=damp(along);
        vx=across*forward+along*right;vy=std::clamp(along*forward-across*right,-16.5f,16.5f);
        velocity={vx*r.x+vy*f.x,vx*r.y+vy*f.y,0};
        float force=0;
        if(magnitude>1e-5f){const float speed=right*vx+forward*vy;
            force=PortMouse::AxisForce(magnitude,speed,16.5f,1,200,1.f/60,35000);
            if(speed>16.5f&&force<0)force=0;}
        // PredictLinearMotion: position uses post-friction velocity; force
        // updates momentum for the following simulation tick.
        position.x+=velocity.x/60;position.y+=velocity.y/60;
        velocity.x+=force*(right*r.x+forward*f.x)/12000;
        velocity.y+=force*(right*r.y+forward*f.y)/12000;
    }
};
static void coverage_stop_turn_checks() {
    unsigned cases=0;float worst=0;
    const std::array<Point,8> biotech{{{0,0,0},{-2,2,0},{-2,4,0},{-4,4,0},
                                      {-4,2,0},{-4,0,0},{-2,-2,0},{0,-2,0}}};
    for(unsigned heading=0;heading<8;++heading)for(unsigned delay:{1u,4u})
    for(bool inverted:{false,true})for(unsigned phase:{0u,1u,2u}) {
        const float angle=heading*.7853981634f,dx=std::cos(angle),dy=std::sin(angle);
        Route route;route.count=8;route.length=15.6569f;
        for(unsigned i=0;i<8;++i)route.waypoints[i]={39.95435f+biotech[i].x*dx-biotech[i].y*dy,
            -203.056f+biotech[i].x*dy+biotech[i].y*dx,0};
        route.start=route.waypoints[0];route.end=route.waypoints[7];
        if(heading==0)assert(!inside_route(route,{38.84713f,-205.83061f,0}));
        const float first=flat_distance(route.start,route.waypoints[1]);
        route.dx=(route.waypoints[1].x-route.start.x)/first;route.dy=(route.waypoints[1].y-route.start.y)/first;
        CoverageCursor cursor;CoverageActionState actionState;
        PlanarSourceOrder actor{route.start,{},std::atan2(-route.dx,route.dy)};
        std::array<LatchedPad,4> latched{};unsigned turns=0,translations=0,aims=0,fireEdges=0,missileEdges=0;
        bool firing=false,missile=false;float distance=0,aimDegrees=0;
        for(unsigned tick=0;tick<2400;++tick) {
            assert(inside_route(route,actor.position));
            // The first jump is stationary; its real grounded observations,
            // not an assumed 120-tick landing time, open the movement gate.
            const bool grounded=phase!=1||tick<7||tick>=71;
            const bool orbiting=phase==2&&tick>=3&&tick<48;
            const auto action=coverage_input(phase,tick,false,true,phase==2,grounded,
                grounded?actor.velocity:Point{0,0,3},orbiting,actionState);
            fireEdges+=action.fire&&!firing;missileEdges+=action.missile&&!missile;
            firing=action.fire;missile=action.missile;
            const auto before=cursor.route;
            const Point forward{-std::sin(actor.yaw),std::cos(actor.yaw),0};
            const auto command=coverage_command(route,actor.position,actor.velocity,forward,grounded,
                action.move&&!orbiting,true,inverted,cursor);
            assert(!command.aim||std::hypot(command.movement.x,command.movement.y)==0);
            assert(!command.aim||std::hypot(actor.velocity.x,actor.velocity.y)<.15f);
            assert(!command.aim||grounded);
            turns+=before.segment!=cursor.route.segment||before.forward!=cursor.route.forward;
            const auto stick=stick_for_world(command.movement,forward);
            const LatchedPad next{stick.x,stick.y,command.aim};
            translations+=next.x!=0||next.y!=0;aims+=next.aim!=0;
            const auto pending=latched[tick%delay];latched[tick%delay]=next;
            if(delay==1&&pending.aim)assert(std::hypot(actor.velocity.x,actor.velocity.y)<.15f);
            const Point previous=actor.position;const float previousYaw=actor.yaw;actor.step(pending,inverted);
            distance+=flat_distance(previous,actor.position);
            aimDegrees+=std::fabs(std::remainder(actor.yaw-previousYaw,6.283185307f))*57.295779513f;
            if(tick==359||tick==719) {
                // Timing remains12 wall seconds. Even360 real simulation
                // updates leave room for the observation criteria; projectile
                // production itself is still verified by native actor counters.
                assert(distance>=2&&aimDegrees>=5&&turns>=1);
                if(phase==1)assert(fireEdges>=2);
                if(phase==2)assert(missileEdges>=2);
            }
            float nearest=1e6f;
            for(unsigned i=1;i<route.count;++i){const auto a=route.waypoints[i-1],b=route.waypoints[i];
                const float length=flat_distance(a,b),t=std::clamp(((actor.position.x-a.x)*(b.x-a.x)+
                    (actor.position.y-a.y)*(b.y-a.y))/(length*length),0.f,1.f);
                nearest=std::min(nearest,flat_distance(actor.position,{a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),0}));}
            worst=std::max(worst,nearest);
        }
        if(distance<=20||turns<6)std::fprintf(stderr,"coverage shortcase heading%u delay%u inverted%d phase%u travel%.3f turns%u segment%u pos%.3f/%.3f align%d brake%d yaw%.3f\n",heading,delay,inverted,phase,distance,turns,cursor.route.segment,actor.position.x,actor.position.y,cursor.aligning,cursor.braking,actor.yaw);
        assert(inside_route(route,actor.position));assert(distance>20);assert(turns>=6);
        assert(translations>100&&aims>100);++cases;
    }
    CoverageActionState state;
    for(unsigned tick=0;tick<500;++tick){const auto a=coverage_input(1,tick,false,true,false,
        false,{0,0,-2},false,state);assert(!a.move);assert(a.jump==(tick<6));}
    assert(!coverage_input(1,500,false,true,false,true,{0,0,0},false,state).move);
    assert(coverage_input(1,501,false,true,false,true,{0,0,0},false,state).move);
    state={};assert(!coverage_input(1,200,false,true,false,true,{2,0,0},false,state).move);
    for(unsigned tick=0;tick<120;++tick){auto a=coverage_input(2,tick,false,true,true,true,{},true,state);
        assert(!a.move);assert(a.lock==(tick<45));}
    assert(coverage_input(2,120,false,true,true,true,{},false,state).move);
    // Frozen V3 reverses its heading before residual velocity is stopped.
    // The correction first submits neutral braking without switching/turning.
    Route end;end.count=2;end.waypoints={Point{0,0,0},Point{2,0,0}};
    RouteCursor original;path_patrol(end,{1.8f,0,0},{3,0,0},original);
    assert(!original.forward);
    CoverageCursor stopped;stopped.aligning=false;
    auto braking=coverage_command(end,{1.8f,0,0},{3,0,0},{1,0,0},true,true,true,false,stopped);
    assert(stopped.braking&&stopped.route.forward&&braking.aim==0&&braking.movement.x==0);
    auto turning=coverage_command(end,{1.9f,0,0},{0,0,0},{1,0,0},true,true,true,false,stopped);
    assert(!stopped.route.forward&&turning.aim!=0&&turning.movement.x==0&&turning.movement.y==0);
    // Ball momentum continues to use the unchanged active polyline steering.
    Route ball;ball.count=3;ball.waypoints={Point{0,0,0},Point{4,0,0},Point{4,4,0}};
    CoverageCursor ballCursor;RouteCursor old;
    for(unsigned i=0;i<100;++i){Point p{float(i%4),0,0},v{1,0,0};
        auto expected=path_patrol(ball,p,v,old);
        auto actual=coverage_command(ball,p,v,{0,1,0},false,true,false,false,ballCursor);
        assert(actual.aim==0&&actual.movement.x==expected.x&&actual.movement.y==expected.y);}
    std::printf("coverage stop/turn/go %u latched LOCAL-pad/retail-force cases; inversion,1/4-poll latency,actual ground/orbit gates,real corners/reversals; max centreline %.4fm; no physics/collision acceptance claim\n",cases,worst);
}
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
    coverage_stop_turn_checks();
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
