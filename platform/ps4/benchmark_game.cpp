// SPDX-License-Identifier: MIT
#include "benchmark_tour.hpp"
#include "benchmark_play_logic.hpp"
#include "port_debug.h"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/Cameras/CCameraManager.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerGun.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Weapons/CGameProjectile.hpp"
#include "MetroidPrime/Weapons/CBomb.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "Collision/CCollisionPrimitive.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Collision/CMaterialFilter.hpp"
#include <aurora/frame_trace.hpp>
#include <dolphin/pad.h>
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace prime {
namespace {
using namespace benchmark_play;
struct Adapter {
    unsigned visit{},view{},unmorphTicks{};bool prepared{},measuring{},controls{},placing{},routeValid{},toEnd=true;
    Route route;RouteCursor cursor;uint32_t routeWorld{},routeArea{};std::bitset<65536> seen;
} adapter;
Point point(const CVector3f& p){return {p.GetX(),p.GetY(),p.GetZ()};}
CVector3f vector(Point p){return CVector3f(p.x,p.y,p.z);}
uint64_t nanos(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
const CMaterialFilter Solid=CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid),CMaterialList());

bool setup(CStateManager& mgr,Route& route,bool reuse) {
    CPlayer& player=*mgr.Player();const CVector3f origin=player.GetTranslation();
    const CAABox& bounds=mgr.GetWorld()->GetAreaAlways(mgr.GetWorld()->GetCurrentAreaId()).GetAABB();
    // A centre-only ray leaves the forward half of the real body inside an
    // authored ramp. Find support under the actual primitive footprint first,
    // then let Clear reject any remaining body/wall/dynamic collision.
    const auto body=player.GetCollisionPrimitive()->CalculateAABox(CTransform4f::Translate(player.GetPrimitiveOffset()));
    const auto surface=[&](Point desired,float length,Point& out){
        const CVector3f top(desired.x,desired.y,desired.z+2.f);
        const auto hit=CGameCollision::RayStaticIntersection(mgr,top,CVector3f::Down(),length,Solid);
        if(!hit.IsValid()||!CGameCollision::IsFloor(hit.GetMaterial(),hit.GetPlane().GetNormal())||
           hit.GetMaterial().HasMaterial(kMT_Lava)||hit.GetMaterial().HasMaterial(kMT_Phazon))return false;
        out=point(hit.GetPoint());return finite(out);
    };
    const auto supported=[&](Point desired,Point& out){
        const auto ray=[&](Point p,Point& hit){return surface(p,5.f,hit);};
        if(!supported_floor(desired,point(body.GetMinPoint()),point(body.GetMaxPoint()),
                            player.GetStepUpHeight(),ray,out))return false;
        const auto& lo=bounds.GetMinPoint();const auto& hi=bounds.GetMaxPoint();
        return out.x>lo.GetX()+.6f&&out.x<hi.GetX()-.6f&&out.y>lo.GetY()+.6f&&out.y<hi.GetY()-.6f&&
               out.z>lo.GetZ()&&out.z<hi.GetZ()-2.f;
    };
    const bool coverage=benchmark_coverage_enabled();
    const auto floor=[&](Point desired,Point& out){
        if(coverage)return supported(desired,out);
        const auto hit=CGameCollision::RayStaticIntersection(mgr,CVector3f(desired.x,desired.y,desired.z+2.f),CVector3f::Down(),5.f,Solid);
        if(!hit.IsValid()||!(hit.GetPlane().GetNormal().GetZ()>=.85f)||
           hit.GetMaterial().HasMaterial(kMT_Lava)||hit.GetMaterial().HasMaterial(kMT_Phazon))return false;
        out=point(hit.GetPoint()+CVector3f(0,0,.15f));
        const auto& lo=bounds.GetMinPoint();const auto& hi=bounds.GetMaxPoint();
        return out.x>lo.GetX()+.6f&&out.x<hi.GetX()-.6f&&out.y>lo.GetY()+.6f&&out.y<hi.GetY()-.6f&&
               out.z>lo.GetZ()&&out.z<hi.GetZ()-2.f;
    };
    const auto clear=[&](Point pos){
        const auto* primitive=player.GetCollisionPrimitive();
        const auto transform=CTransform4f::Translate(vector(pos)+player.GetPrimitiveOffset());
        TEntityList near;mgr.BuildNearList(near,primitive->CalculateAABox(transform),Solid,&player);
        return !CGameCollision::DetectCollisionBoolean(mgr,*primitive,transform,player.GetMaterialFilter(),near);
    };
    const auto score=[&](Point pos,float dx,float dy){
        const auto eye=vector(pos)+CVector3f(0,0,player.GetEyeHeight());
        float total=0;
        for(float horizontal:{-.35f,0.f,.35f})for(float vertical:{-.18f,0.f,.18f}) {
            CVector3f dir(dx*std::cos(horizontal)-dy*std::sin(horizontal),dx*std::sin(horizontal)+dy*std::cos(horizontal),vertical);
            dir.Normalize();TEntityList near;mgr.BuildNearList(near,eye,dir,40.f,Solid,&player);
            TUniqueId hitId=kInvalidUniqueId;
            const auto hit=CGameCollision::RayWorldIntersection(mgr,hitId,eye,dir,40.f,Solid,near);
            const float distance=hit.IsValid()?hit.GetTime():40.f;
            if(distance<3.f)return 0.f;total+=distance;
        }
        return total/9.f;
    };
    if(!coverage) {
        if(reuse) {
            if(!revalidate_legacy_route(point(origin),route,floor,clear,score))return false;
        } else {
            route={};if(!choose_legacy_route(point(origin),floor,clear,score,route))return false;
        }
    } else if(reuse) {
        if(!revalidate_route(point(origin),route,floor,clear,score))return false;
    } else {
        route={};Point seed=point(origin),projected;
        if(!floor(seed,projected)) {
            // An authored warp spawn can stand above its static floor (the
            // Landing Site ship spawn is3.66m above it). Project only downward
            // to the first real surface, then validate the complete body descent.
            const auto hit=CGameCollision::RayStaticIntersection(mgr,origin+CVector3f(0,0,2),CVector3f::Down(),12.f,Solid);
            if(!hit.IsValid()||!CGameCollision::IsFloor(hit.GetMaterial(),hit.GetPlane().GetNormal())||
               hit.GetMaterial().HasMaterial(kMT_Lava)||hit.GetMaterial().HasMaterial(kMT_Phazon))return false;
            projected=point(hit.GetPoint()+CVector3f(0,0,.15f));
            Point supported;if(!floor(projected,supported)||!safe_descent(seed,supported,clear))return false;
            seed=supported;
        } else seed=projected;
        if(!choose_route(seed,floor,clear,score,route))return false;
    }
    route.jumpSafe=true;
    for(float height:{1.f,2.f,3.f})
        if(!clear({route.start.x,route.start.y,route.start.z+height}))route.jumpSafe=false;
    const CVector3f pos=vector(route.start),look(route.dx,route.dy,0);
    player.Teleport(CTransform4f::LookAt(pos,pos+look,CVector3f::Up()),mgr,true);
    player.SetVelocityWR(CVector3f::Zero());
    return true;
}
BenchmarkPlayObservation observe(CStateManager& mgr,bool seed) {
    const auto* player=mgr.GetPlayer();BenchmarkPlayObservation sample;
    if(!player)return sample;
    const auto pos=point(player->GetTranslation());sample.valid=true;sample.x=pos.x;sample.y=pos.y;sample.z=pos.z;
    const auto forward=player->GetTransform().GetForward();sample.yaw=std::atan2(-forward.GetX(),forward.GetY());
    sample.airborne=player->GetPlayerMovementState()!=NPlayer::kMS_OnGround;
    sample.morphed=player->GetMorphballTransitionState()==CPlayer::kMS_Morphed;
    const auto& objects=mgr.GetObjectListById(kOL_Actor);
    for(int index=objects.GetFirstObjectIndex();index!=-1;index=objects.GetNextObjectIndex(index)) {
        const auto* entity=objects[index];if(!entity)continue;
        const CWeapon* weapon=nullptr;
        if(const CGameProjectile* projectile=TCastToConstPtr<CGameProjectile>(entity))weapon=projectile;
        else if(const CBomb* bomb=TCastToConstPtr<CBomb>(entity))weapon=bomb;
        if(!weapon||weapon->GetOwnerId()!=player->GetUniqueId())continue;
        const auto uid=weapon->GetUniqueId().Value();
        if(adapter.seen.test(uid))continue;adapter.seen.set(uid);
        if(seed)continue;
        if(weapon->GetType()==kWT_Missile)++sample.missiles;
        else if(weapon->GetType()==kWT_Bomb)++sample.bombs;
        else if(weapon->GetType()>=kWT_Power&&weapon->GetType()<=kWT_Plasma){
            ++sample.power;sample.charged+=weapon->HasAttrib(CWeapon::kPA_Charged);
        }
    }
    if(player->GetOrbitState()==CPlayer::kOS_OrbitObject) {
        const CPatterned* enemy=TCastToConstPtr<CPatterned>(mgr.GetObjectById(player->GetOrbitTargetId()));
        sample.lockedEnemy=enemy!=nullptr;
    }
    return sample;
}
void neutral(){PADStatus pad{};pad.err=PAD_ERR_NONE;PADSetVirtualStatus(0,&pad);}
void drive_legacy(CStateManager& mgr,const BenchmarkPlayControl& control) {
    auto& player=*mgr.Player();const Point pos=point(player.GetTranslation());
    if(!inside_route(adapter.route,pos)){
        BenchmarkPlayObservation failure;failure.valid=true;failure.x=pos.x;failure.y=pos.y;failure.z=pos.z;
        const auto forward=player.GetTransform().GetForward();failure.yaw=std::atan2(-forward.GetX(),forward.GetY());
        failure.guardStops=1;benchmark_play_observe(failure);
        neutral();benchmark_play_fail("route_guard");return;
    }
    const bool morphed=player.GetMorphballTransitionState()==CPlayer::kMS_Morphed;
    const CPatterned* target=TCastToConstPtr<CPatterned>(mgr.GetObjectById(player.GetOrbitNextTargetId()));
    const auto action=input(control.view,control.inputTick,morphed,adapter.route.jumpSafe,target!=nullptr);
    PADStatus pad{};pad.err=PAD_ERR_NONE;
    if(control.view!=3) {
        const auto forward=player.GetTransform().GetForward();
        const float desired=std::atan2(-adapter.route.dx,adapter.route.dy)+((control.inputTick/120)%2?.18f:-.18f);
        const float error=std::remainder(desired-std::atan2(-forward.GetX(),forward.GetY()),6.283185307f);
        pad.substickX=error>.04f?-64:error<-.04f?64:0;
    }
    if(action.fire)pad.button|=PAD_BUTTON_A;
    if(action.missile)pad.button|=PAD_BUTTON_Y;
    if(action.jump)pad.button|=PAD_BUTTON_B;
    if(action.morph)pad.button|=PAD_BUTTON_X;
    if(action.lock){pad.button|=PAD_TRIGGER_L;pad.triggerLeft=255;}
    if(action.move) {
        const Point goal=patrol(adapter.route,pos,point(player.GetVelocityWR()),adapter.toEnd);
        // Direct aim strafes in the body frame even with retail movement physics.
        // Morph Ball instead uses its camera-derived, interpolated control frame
        // (CMorphBall::ComputeMarioMovement), never the rolling body's transform.
        const auto forward=morphed?player.PortControlDirFlat():player.GetTransform().GetForward();
        const auto sticks=stick_for_world(goal,point(forward),morphed?.55f:1.f);
        pad.stickX=static_cast<s8>(sticks.x);pad.stickY=static_cast<s8>(sticks.y);
    }
    PADSetVirtualStatus(0,&pad);
}
void drive(CStateManager& mgr,const BenchmarkPlayControl& control) {
    if(!benchmark_coverage_enabled()){drive_legacy(mgr,control);return;}
    auto& player=*mgr.Player();const Point pos=point(player.GetTranslation());
    if(!inside_route(adapter.route,pos)){
        BenchmarkPlayObservation failure;failure.valid=true;failure.x=pos.x;failure.y=pos.y;failure.z=pos.z;
        const auto forward=player.GetTransform().GetForward();failure.yaw=std::atan2(-forward.GetX(),forward.GetY());
        failure.guardStops=1;benchmark_play_observe(failure);
        neutral();benchmark_play_fail("route_guard");return;
    }
    const bool morphed=player.GetMorphballTransitionState()==CPlayer::kMS_Morphed;
    const CPatterned* target=TCastToConstPtr<CPatterned>(mgr.GetObjectById(player.GetOrbitNextTargetId()));
    const auto action=input(control.view,control.inputTick,morphed,adapter.route.jumpSafe,target!=nullptr);
    if(control.view==2&&control.rehearsing&&control.inputTick==0) {
        const auto& inventory=*mgr.GetPlayerState();const auto* gun=player.GetPlayerGun();
        std::fprintf(stderr,"[benchmark-play] missile preparation visit=%u amount=%d capacity=%d usable=%d secondary=%u charge=%u firing=%u\n",
            control.visit,inventory.GetItemAmount(CPlayerState::kIT_Missiles),
            inventory.GetItemCapacity(CPlayerState::kIT_Missiles),inventory.PortUsableAmount(CPlayerState::kIT_Missiles),
            gun?gun->GetSecondaryItemId():0,gun?unsigned(gun->GetChargeState()):0,gun?unsigned(gun->GetFiring()):0);
        if(!inventory.HasPowerUp(CPlayerState::kIT_Missiles)||inventory.PortUsableAmount(CPlayerState::kIT_Missiles)<=0) {
            neutral();benchmark_play_fail("missing_missile_inventory");return;
        }
    }
    const Point goal=adapter.route.count?
        path_patrol(adapter.route,pos,point(player.GetVelocityWR()),adapter.cursor):
        patrol(adapter.route,pos,point(player.GetVelocityWR()),adapter.toEnd);
    PADStatus pad{};pad.err=PAD_ERR_NONE;
    if(control.view!=3) {
        const auto forward=player.GetTransform().GetForward();
        const auto heading=adapter.route.count?path_heading(adapter.route,adapter.cursor):Point{adapter.route.dx,adapter.route.dy,0};
        const float desired=std::atan2(-heading.x,heading.y)+((control.inputTick/120)%2?.18f:-.18f);
        const float error=std::remainder(desired-std::atan2(-forward.GetX(),forward.GetY()),6.283185307f);
        pad.substickX=error>.04f?-64:error<-.04f?64:0;
    }
    if(action.fire)pad.button|=PAD_BUTTON_A;
    if(action.missile)pad.button|=PAD_BUTTON_Y;
    if(action.jump)pad.button|=PAD_BUTTON_B;
    if(action.morph)pad.button|=PAD_BUTTON_X;
    if(action.lock){pad.button|=PAD_TRIGGER_L;pad.triggerLeft=255;}
    if(action.move) {
        // Direct aim strafes in the body frame even with retail movement physics.
        // Morph Ball instead uses its camera-derived, interpolated control frame
        // (CMorphBall::ComputeMarioMovement), never the rolling body's transform.
        const auto forward=morphed?player.PortControlDirFlat():player.GetTransform().GetForward();
        const auto sticks=stick_for_world(goal,point(forward),morphed?.55f:1.f);
        pad.stickX=static_cast<s8>(sticks.x);pad.stickY=static_cast<s8>(sticks.y);
    }
    PADSetVirtualStatus(0,&pad);
}
}
void benchmark_game_tick(CStateManager& mgr) {
    const auto* world=mgr.GetWorld();const int index=world?world->GetCurrentAreaId().Value():-1;
    const bool hasArea=world&&index>=0&&index<world->GetNumAreas();
    const auto* area=hasArea?&world->GetAreaAlways(TAreaId(index)):nullptr;
    const auto* player=mgr.GetPlayer();const bool play=benchmark_play_enabled();
    if(play&&!adapter.controls){
        // Fixed diagnostic input semantics, saved only in the isolated play-v2 user folder.
        PortDebug::SetTwinStick(true);PortDebug::SetFlickStick(false);
        // Both direct-aim presets strafe; preserve the original movement/jump
        // force laws for this benchmark instead of switching to Remastered.
        PortDebug::SetRemasteredMovement(false);adapter.controls=true;
    }
    bool ready=area&&area->IsPostConstructed()&&!mgr.GetWantsToQuit()&&mgr.GetGameState()==CStateManager::kGS_Running&&
        player&&!mgr.GetCameraManager()->IsInCinematicCamera()&&player->GetCameraState()!=CPlayer::kCS_Spawned;
    if(!play)ready=ready&&!player->GetDisableInput()&&mgr.GetCameraManager()->IsInFPCamera();
    auto before=benchmark_play_control();
    if(play&&(before.measuring||before.rehearsing)) {
        aurora::trace::Scope observer("benchmark gameplay observation",aurora::trace::Main);
        benchmark_play_observe(observe(mgr,false));
    }
    prime_ps4_benchmark_tick(world?uint32_t(world->IGetWorldAssetId()):0,area?uint32_t(area->GetAreaAssetId()):0,
        mgr.GetUpdateFrameIndex(),aurora::trace::frame_id(),ready);
    if(!play)return;
    auto control=benchmark_play_control();
    if(!control.active){PADClearVirtualStatus(0);return;}
    const bool newPlacement=control.placing&&!adapter.placing;
    adapter.placing=control.placing;
    if(control.placing&&ready) {
        neutral();
        const auto worldKey=uint32_t(world->IGetWorldAssetId()),areaKey=uint32_t(area->GetAreaAssetId());
        if(adapter.routeWorld!=worldKey||adapter.routeArea!=areaKey){
            adapter.routeValid=false;adapter.routeWorld=worldKey;adapter.routeArea=areaKey;
        }
        if(newPlacement||adapter.visit!=control.visit||adapter.view!=control.view){
            // Rehearsal resets and later actions keep the room's same route.
            // A new visit alone invalidates the cache, even if its room repeats.
            if(adapter.visit!=control.visit)adapter.routeValid=false;
            adapter.visit=control.visit;adapter.view=control.view;adapter.prepared=false;adapter.unmorphTicks=0;
        }
        if(player->GetMorphballTransitionState()!=CPlayer::kMS_Unmorphed) {
            PADStatus pad{};pad.err=PAD_ERR_NONE;
            if(adapter.unmorphTicks++%120<6)pad.button=PAD_BUTTON_X;
            PADSetVirtualStatus(0,&pad);return;
        }
        const auto start=nanos();const bool valid=setup(mgr,adapter.route,adapter.routeValid);const auto elapsed=nanos()-start;
        adapter.prepared=valid;adapter.routeValid=valid;adapter.toEnd=true;adapter.cursor={};adapter.seen.reset();adapter.measuring=false;
        benchmark_play_placement(valid,adapter.route.start.x,adapter.route.start.y,adapter.route.start.z,
            adapter.route.length,adapter.route.openness,adapter.route.jumpSafe,elapsed,
            std::atan2(-adapter.route.dx,adapter.route.dy));
        if(!benchmark_play_enabled())PADClearVirtualStatus(0);
        return;
    }
    if((control.measuring||control.rehearsing)&&ready&&adapter.prepared) {
        if(!adapter.measuring){adapter.measuring=true;adapter.seen.reset();observe(mgr,true);}
        drive(mgr,control);
        if(!benchmark_play_enabled())PADClearVirtualStatus(0);
    } else {neutral();adapter.measuring=false;}
}
}
