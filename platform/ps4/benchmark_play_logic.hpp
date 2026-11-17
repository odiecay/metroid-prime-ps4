// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace prime::benchmark_play {
struct Point { float x{},y{},z{}; };
struct Route {
    Point start{},end{}; float dx{},dy{},length{},openness{}; bool jumpSafe{};
    // Bounded connected path, validated during placement. Legacy two-point
    // fixtures retain count=0; production coverage uses the complete polyline.
    std::array<Point,8> waypoints{}; unsigned count{};
};
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
// Exact V2 comparator workload retained independently of V3 coverage.
template<class Floor,class Clear,class Score>
bool choose_legacy_route(Point origin,Floor&& floor,Clear&& clear,Score&& score,Route& best) {
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
// Re-validate the original visit route before each rehearsal/phase reset.
// The search origin must not follow the moving player through the room.
// Floor and full-body corridor/access checks still run before any teleport.
template<class Floor,class Clear,class Score>
bool revalidate_legacy_route(Point current,Route& route,Floor&& floor,Clear&& clear,Score&& score) {
    if(!finite(current)||!finite(route.start)||!finite(route.end)||!(route.length>0)||
       !std::isfinite(route.dx)||!std::isfinite(route.dy)||
       std::fabs(std::hypot(route.dx,route.dy)-1.f)>.001f)return false;
    Point start,end,back,front;
    if(!floor(route.start,start)||!finite(start)||!floor(route.end,end)||!finite(end)||
       !floor({start.x-route.dx,start.y-route.dy,start.z},back)||
       !floor({start.x+(route.length+2)*route.dx,start.y+(route.length+2)*route.dy,start.z},front)||
       !segment(back,front,floor,clear,.35f)||!segment(current,start,floor,clear))return false;
    const float openness=score(start,route.dx,route.dy);
    if(!std::isfinite(openness)||openness<=8.f)return false;
    route.start=start;route.end=end;route.openness=openness;return true;
}
// Ground support follows the actual collision primitive footprint. Surface
// supplies the game's own floor/material validation; a rejected corner cannot
// be hidden by taking a higher ray from another part of the body.
template<class Surface>
bool supported_floor(Point desired,Point minimum,Point maximum,float stepUp,Surface&& surface,Point& out) {
    if(!finite(desired)||!finite(minimum)||!finite(maximum)||!std::isfinite(stepUp)||stepUp<0||
       minimum.x>maximum.x||minimum.y>maximum.y||minimum.z>maximum.z)return false;
    Point centre;if(!surface(desired,centre)||!finite(centre))return false;
    float support=centre.z;
    for(float x:{minimum.x,maximum.x})for(float y:{minimum.y,maximum.y}) {
        Point corner;
        if(!surface({desired.x+x,desired.y+y,desired.z},corner)||!finite(corner)||
           std::fabs(corner.z-centre.z)>stepUp)return false;
        support=std::max(support,corner.z);
    }
    out={centre.x,centre.y,support-minimum.z+.15f};return finite(out);
}
// Only a short gravity-direction descent is allowed for an elevated authored
// spawn. Every quarter metre uses the actual full-character collision check.
template<class Clear>
bool safe_descent(Point from,Point to,Clear&& clear) {
    if(!finite(from)||!finite(to)||flat_distance(from,to)>.001f||to.z>from.z||from.z-to.z>8.f)return false;
    const unsigned count=std::max(1u,unsigned(std::ceil((from.z-to.z)/.25f)));
    for(unsigned i=0;i<=count;++i)
        if(!clear({from.x,from.y,from.z+(to.z-from.z)*float(i)/count}))return false;
    return true;
}
inline bool inside_route(const Route&,Point);
// The traversal grid finds only connected, whole-body-clear ground. Its wider
// edges separately validate the .65m guard used by the input driver, including
// its endpoint margins. Placement alone performs this bounded search.
template<class Floor,class Clear>
bool corridor(Point a,Point b,Floor&& floor,Clear&& clear) {
    const float distance=flat_distance(a,b);
    if(!(distance>.001f)||!std::isfinite(distance))return false;
    const float dx=(b.x-a.x)/distance,dy=(b.y-a.y)/distance;
    Point back,front;
    if(!floor({a.x-.65f*dx,a.y-.65f*dy,a.z},back)||
       !floor({b.x+.65f*dx,b.y+.65f*dy,b.z},front))return false;
    return segment(back,front,floor,clear,.65f);
}
template<class Floor,class Clear,class Score>
bool choose_route(Point origin,Floor&& floor,Clear&& clear,Score&& score,Route& best) {
    if(!finite(origin))return false;
    constexpr int Radius=8,Side=2*Radius+1,Cells=Side*Side;
    constexpr std::array<int,8> X{1,1,0,-1,-1,-1,0,1},Y{0,1,1,1,0,-1,-1,-1};
    struct Cell {Point p{};unsigned tested{},wide{};bool reached{};};
    std::array<Cell,Cells> cells{};std::array<unsigned,Cells> queue{};
    const auto neighbour=[&](unsigned n,unsigned dir)->int {
        const int x=int(n%Side)+X[dir],y=int(n/Side)+Y[dir];
        return x>=0&&x<Side&&y>=0&&y<Side?y*Side+x:-1;
    };
    constexpr unsigned centre=Radius*Side+Radius;
    Point seed;if(!floor(origin,seed)||!finite(seed)||!clear(seed))return false;
    cells[centre].p=seed;cells[centre].reached=true;queue[0]=centre;
    unsigned tail=1;
    for(unsigned head=0;head<tail;++head) {
        const unsigned n=queue[head];const Point a=cells[n].p;
        for(unsigned dir=0;dir<8;++dir) {
            const int next=neighbour(n,dir);if(next<0||cells[next].reached)continue;
            Point b;
            if(!floor({origin.x+2.f*(next%Side-Radius),origin.y+2.f*(next/Side-Radius),a.z},b)||
               !finite(b)||!segment(a,b,floor,clear))continue;
            cells[next].p=b;cells[next].reached=true;queue[tail++]=unsigned(next);
        }
    }
    const auto wide=[&](unsigned n,unsigned dir) {
        Cell& a=cells[n];const unsigned bit=1u<<dir;
        if(a.tested&bit)return bool(a.wide&bit);
        a.tested|=bit;const int next=neighbour(n,dir);
        if(next<0||!cells[next].reached)return false;
        const bool valid=corridor(a.p,cells[next].p,floor,clear);
        const unsigned reverse=(dir+4)%8;cells[next].tested|=1u<<reverse;
        if(valid){a.wide|=bit;cells[next].wide|=1u<<reverse;}
        return valid;
    };
    bool found=false;float bestQuality=-1e30f;
    // Prefer a broad path. A constrained room can still use a genuine branched
    // walk: revisit a junction after taking distinct validated legs, never claim
    // a two-point oscillation as a turning route.
    for(unsigned retrace=0;retrace<2&&!found;++retrace)
    for(unsigned q=0;q<tail;++q)for(unsigned heading=0;heading<8;++heading) {
        const unsigned start=queue[q];if(!wide(start,heading))continue;
        Route candidate;candidate.waypoints[0]=cells[start].p;candidate.count=1;
        std::array<unsigned,8> used{};used[0]=start;
        unsigned current=start,previousDirection=heading,turns=0;float straight=0;
        for(unsigned leg=0;leg<7;++leg) {
            int chosen=-1;unsigned chosenDirection=0;float value=-1e30f;
            for(unsigned dir=0;dir<8;++dir) {
                if(leg==0&&dir!=heading)continue;
                const int next=neighbour(current,dir);if(next<0||!wide(current,dir))continue;
                bool visited=false;for(unsigned i=0;i<candidate.count;++i)visited|=used[i]==unsigned(next);
                if(visited&&!retrace)continue;
                bool reusedEdge=false;
                for(unsigned i=1;i<candidate.count;++i)
                    reusedEdge|=(used[i-1]==current&&used[i]==unsigned(next))||
                                (used[i]==current&&used[i-1]==unsigned(next));
                const float dot=float(X[dir]*X[previousDirection]+Y[dir]*Y[previousDirection])/
                    (std::hypot(float(X[dir]),float(Y[dir]))*std::hypot(float(X[previousDirection]),float(Y[previousDirection])));
                if(leg&&dot<-.01f&&!retrace)continue;
                const bool turn=leg&&dot<.99f;
                // Prefer a real turn after a useful straight leg; do not create
                // a zigzag just to inflate the travelled distance.
                const float quality=(turn?(straight>=3.9f?8.f:-4.f):0.f)+
                    .05f*flat_distance(cells[start].p,cells[next].p)+
                    (retrace?(!reusedEdge?12.f:-10.f)+(!visited?4.f:0.f):0.f);
                if(quality>value){value=quality;chosen=next;chosenDirection=dir;}
            }
            if(chosen<0)break;
            const float distance=flat_distance(cells[current].p,cells[chosen].p);
            if(leg&&chosenDirection!=previousDirection){
                turns+=X[chosenDirection]*Y[previousDirection]!=Y[chosenDirection]*X[previousDirection];straight=0;
            }
            straight+=distance;candidate.length+=distance;
            used[candidate.count]=unsigned(chosen);candidate.waypoints[candidate.count++]=cells[chosen].p;
            current=unsigned(chosen);previousDirection=chosenDirection;
        }
        unsigned uniqueVertices=0,uniqueEdges=0;
        for(unsigned i=0;i<candidate.count;++i) {
            bool seen=false;for(unsigned j=0;j<i;++j)seen|=used[i]==used[j];
            uniqueVertices+=!seen;
            if(i){seen=false;for(unsigned j=1;j<i;++j)
                seen|=(used[i-1]==used[j-1]&&used[i]==used[j])||(used[i-1]==used[j]&&used[i]==used[j-1]);
                uniqueEdges+=!seen;}
        }
        if(candidate.count<3||candidate.length<12.f||!turns||uniqueVertices<3||uniqueEdges<3)continue;
        candidate.start=candidate.waypoints[0];candidate.end=candidate.waypoints[candidate.count-1];
        const Point next=candidate.waypoints[1];const float first=flat_distance(candidate.start,next);
        candidate.dx=(next.x-candidate.start.x)/first;candidate.dy=(next.y-candidate.start.y)/first;
        candidate.openness=score(candidate.start,candidate.dx,candidate.dy);
        if(!std::isfinite(candidate.openness)||candidate.openness<0)continue;
        // Visibility ranks valid paths; the collision-validated path itself is
        // the safety condition. A short sightline around a corner is legitimate.
        const float quality=candidate.length+2.f*turns+.05f*candidate.openness-.05f*flat_distance(origin,candidate.start);
        if(quality>bestQuality){best=candidate;bestQuality=quality;found=true;}
    }
    return found;
}
// Recheck the exact original connected path before each phase; the search seed
// never follows the moving player. Commit only after every check succeeds.
template<class Floor,class Clear,class Score>
bool revalidate_route(Point current,Route& route,Floor&& floor,Clear&& clear,Score&& score) {
    if(!finite(current)||route.count<3||route.count>route.waypoints.size()||
       !finite(route.start)||!finite(route.end)||!std::isfinite(route.length)||route.length<12.f||
       !std::isfinite(route.dx)||!std::isfinite(route.dy)||std::fabs(std::hypot(route.dx,route.dy)-1.f)>.001f)return false;
    Route checked=route;float length=0;
    for(unsigned i=0;i<route.count;++i) {
        if(!finite(route.waypoints[i])||!floor(route.waypoints[i],checked.waypoints[i])||
           !finite(checked.waypoints[i]))return false;
        if(i) {
            if(!corridor(checked.waypoints[i-1],checked.waypoints[i],floor,clear))return false;
            length+=flat_distance(checked.waypoints[i-1],checked.waypoints[i]);
        }
    }
    // The current player can be on any previously validated leg, including a
    // jump. Access follows that leg back to the seed rather than cutting a turn.
    if(!inside_route(route,current))return false;
    bool access=false;
    for(unsigned i=1;i<checked.count&&!access;++i) {
        const Point a=checked.waypoints[i-1],b=checked.waypoints[i];
        const float distance=flat_distance(a,b);
        const float t=std::clamp(((current.x-a.x)*(b.x-a.x)+(current.y-a.y)*(b.y-a.y))/(distance*distance),0.f,1.f);
        const Point centre{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
        Point grounded;
        if(flat_distance(current,centre)<=.92f&&floor({current.x,current.y,centre.z},grounded)&&
           finite(grounded)&&segment(grounded,centre,floor,clear))access=true;
    }
    if(!access)return false;
    checked.start=checked.waypoints[0];checked.end=checked.waypoints[checked.count-1];
    const auto next=checked.waypoints[1];const float first=flat_distance(checked.start,next);
    if(!(first>.001f)||length<12.f)return false;
    checked.dx=(next.x-checked.start.x)/first;checked.dy=(next.y-checked.start.y)/first;
    checked.length=length;checked.openness=score(checked.start,checked.dx,checked.dy);
    if(!std::isfinite(checked.openness)||checked.openness<0)return false;
    route=checked;return true;
}
inline bool inside_route(const Route& r,Point p) {
    if(r.count) {
        if(r.count<2||r.count>r.waypoints.size()||!finite(p))return false;
        bool contained=false;
        for(unsigned i=1;i<r.count;++i) {
            const auto a=r.waypoints[i-1],b=r.waypoints[i];
            const float length=flat_distance(a,b);
            if(!finite(a)||!finite(b)||!(length>.001f))return false;
            const float dx=(b.x-a.x)/length,dy=(b.y-a.y)/length;
            const float x=p.x-a.x,y=p.y-a.y;
            const float along=x*dx+y*dy,side=-x*dy+y*dx;
            const float z=a.z+(b.z-a.z)*std::clamp(along/length,0.f,1.f);
            if(along>=-.65f&&along<=length+.65f&&std::fabs(side)<=.65f&&
               p.z>=z-.65f&&p.z<=z+4.5f)contained=true;
        }
        return contained;
    }
    if(!(r.length>0)||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
    const float x=p.x-r.start.x,y=p.y-r.start.y;
    const float along=x*r.dx+y*r.dy,side=-x*r.dy+y*r.dx;
    const float z=r.start.z+(r.end.z-r.start.z)*std::clamp(along/r.length,0.f,1.f);
    return along>=-.65f&&along<=5.6f&&std::fabs(side)<=.65f&&p.z>=z-.65f&&p.z<=z+4.5f;
}
// Closed-loop patrol: steer back to the validated centre line and anticipate
// momentum at each turnaround. No collision queries or guard changes per frame.
inline Point patrol(const Route& r,Point p,Point velocity,bool& toEnd) {
    if(!finite(p)||!finite(velocity)||!(r.length>0))return {};
    const float x=p.x-r.start.x,y=p.y-r.start.y;
    const float along=x*r.dx+y*r.dy,side=-x*r.dy+y*r.dx;
    const float va=velocity.x*r.dx+velocity.y*r.dy;
    const float vs=-velocity.x*r.dy+velocity.y*r.dx;
    const float predicted=along+.18f*va;
    if(predicted>=3.f)toEnd=false;else if(predicted<=1.f)toEnd=true;
    const float a=std::clamp(((toEnd?3.5f:.5f)-along)*1.6f-.22f*va,-1.f,1.f);
    const float b=std::clamp(-3.f*side-.35f*vs,-1.f,1.f);
    const float scale=std::max(1.f,std::hypot(a,b));
    return {(a*r.dx-b*r.dy)/scale,(a*r.dy+b*r.dx)/scale,0};
}
struct RouteCursor { unsigned segment{}; bool forward=true; };
inline Point path_heading(const Route& r,const RouteCursor& cursor) {
    if(r.count<2||r.count>r.waypoints.size()||cursor.segment>=r.count-1)return {};
    const auto a=r.waypoints[cursor.segment],b=r.waypoints[cursor.segment+1];
    const float length=flat_distance(a,b),sign=cursor.forward?1.f:-1.f;
    if(!finite(a)||!finite(b)||!(length>.001f))return {};
    return {sign*(b.x-a.x)/length,sign*(b.y-a.y)/length,0};
}
// Follow every validated leg, braking before a corner instead of cutting
// directly toward a distant waypoint. Only bounded arithmetic runs here;
// floor, full-body collision and corridor searches belong to placement.
inline Point path_patrol(const Route& r,Point p,Point velocity,RouteCursor& cursor) {
    if(!finite(p)||!finite(velocity)||r.count<2||r.count>r.waypoints.size())return {};
    if(cursor.segment>=r.count-1)cursor={};
    auto a=r.waypoints[cursor.segment],b=r.waypoints[cursor.segment+1];
    float length=flat_distance(a,b);
    if(!finite(a)||!finite(b)||!(length>.001f))return {};
    float dx=(b.x-a.x)/length,dy=(b.y-a.y)/length;
    float along=(p.x-a.x)*dx+(p.y-a.y)*dy;
    float va=velocity.x*dx+velocity.y*dy;
    const float predicted=along+.18f*va;
    const bool atEnd=cursor.forward&&along>=length-.30f&&predicted>=length-.15f;
    const bool atStart=!cursor.forward&&along<=.30f&&predicted<=.15f;
    if(atEnd) {
        if(cursor.segment+2<r.count)++cursor.segment;
        else if(flat_distance(r.waypoints[0],r.waypoints[r.count-1])<.01f)cursor.segment=0;
        else cursor.forward=false;
    } else if(atStart) {
        if(cursor.segment)--cursor.segment;
        else cursor.forward=true;
    }
    a=r.waypoints[cursor.segment];b=r.waypoints[cursor.segment+1];
    length=flat_distance(a,b);
    if(!finite(a)||!finite(b)||!(length>.001f))return {};
    dx=(b.x-a.x)/length;dy=(b.y-a.y)/length;
    along=(p.x-a.x)*dx+(p.y-a.y)*dy;
    const float side=-(p.x-a.x)*dy+(p.y-a.y)*dx;
    va=velocity.x*dx+velocity.y*dy;
    const float vs=-velocity.x*dy+velocity.y*dx;
    const float forward=std::clamp(((cursor.forward?length:0.f)-along)*1.4f-.40f*va,-1.f,1.f);
    const float lateral=std::clamp(-3.5f*side-.50f*vs,-1.f,1.f);
    const float scale=std::max(1.f,std::hypot(forward,lateral));
    return {(forward*dx-lateral*dy)/scale,(forward*dy+lateral*dx)/scale,0};
}
// PADClamp subtracts 15 on each axis before octagonal clamping. Compensate
// that deadzone before rounding, otherwise a shallow correction is erased.
inline int pad_axis(float value) {
    if(!std::isfinite(value)||std::fabs(value)<.001f)return 0;
    return int(std::copysign(std::round(15.f+112.f*std::min(1.f,std::fabs(value))),value));
}
struct Stick { int x{},y{}; };
inline Stick stick_for_world(Point desired,Point forward,float scale=1.f) {
    const float length=std::hypot(forward.x,forward.y);
    if(!finite(desired)||!finite(forward)||!(length>1e-5f)||!std::isfinite(scale))return {};
    const float fx=forward.x/length,fy=forward.y/length;
    return {pad_axis((desired.x*fy-desired.y*fx)*scale),
            pad_axis((desired.x*fx+desired.y*fy)*scale)};
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
