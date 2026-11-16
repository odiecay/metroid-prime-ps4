#include <aurora/math.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
using aurora::Vec2;
static bool g_frameBufferAspectFit = true;
static uint32_t configuredWidth = 852, configuredHeight = 480, textureLimit = 16384;
namespace vi { Vec2<uint32_t> configured_fb_size() { return {configuredWidth, configuredHeight}; } }
static uint32_t max_texture_dimension() { return textureLimit; }
#include "actual_env.inc"
namespace baseline {
#include "baseline_functions.inc"
}
namespace candidate {
#include "candidate_functions.inc"
}
struct Viewport { float left,top,width,height,znear,zfar; };
#include "present_viewport.inc"

#if defined(__ORBIS__)
static bool wide_oracle(int nw,int nh,uint32_t bw,uint32_t bh,int fw,int fh) {
  if(nw<=0 || nh<=0 || !bw || !bh || fw<=0 || fh<=0 || fw>nw || fh>nh) return false;
  using U=__uint128_t;
  U dw=unsigned(nw-fw),dh=unsigned(nh-fh);
  if(dw*512>unsigned(nw)||dh*512>unsigned(nh))return false;
  if(dw*bw>U(unsigned(nw))*2||dh*bh>U(unsigned(nh))*2)return false;
  const U a=U(unsigned(nw))*bh,b=U(unsigned(nh))*bw;
  return (a>b?a-b:b-a)<=U(unsigned(nh))*2;
}
#endif
int main() {
  const bool enabled=env_flag("MP_PS4_NATIVE_EFB",true);
#if defined(__ORBIS__)
  const bool snapping=enabled;
#else
  const bool snapping=false;
#endif
  auto output=[](int nw,int nh,uint32_t bw,uint32_t bh,float scale=0.f,uint32_t cap=16384){
    configuredWidth=bw;configuredHeight=bh;textureLimit=cap;g_frameBufferAspectFit=true;
    return candidate::frame_buffer_size_for_scale(nw,nh,scale);
  };
  assert(output(1920,1080,852,480)==(snapping?Vec2<int>{1920,1080}:Vec2<int>{1917,1080}));
  assert(output(1920,1080,854,480)==(snapping?Vec2<int>{1920,1080}:Vec2<int>{1920,1079}));
  assert(output(1920,1080,640,480)==(Vec2<int>{1440,1080}));
  assert(output(1920,1080,852,480,2.25f)==(Vec2<int>{1917,1080}));
  assert(output(1920,1080,852,480,1.f)==(Vec2<int>{852,480}));
  const auto capped=output(1920,1080,852,480,0,1024);
  assert(capped.x>0&&capped.y>0&&capped.x<=1024&&capped.y<=1024);
  if(snapping) assert(capped==(Vec2<int>{1024,576}));
  assert(output(1920,1080,1,1)==(Vec2<int>{1080,1080}));
  assert(output(1920,1080,2,2)==(Vec2<int>{1080,1080}));
  assert(output(1920,1080,3,3)==(Vec2<int>{1080,1080}));
  assert(output(1920,1080,1,2)==(Vec2<int>{540,1080}));
  auto oldVp=calculate_present_viewport(1920,1080,1917,1080);
  assert(oldVp.width==1917 && oldVp.height==1080 && oldVp.left==1);
  auto newVp=calculate_present_viewport(1920,1080,1920,1080);
  assert(newVp.width==1920 && newVp.height==1080 && newVp.left==0);
  const std::array<Vec2<int>,15> windows{{{1920,1080},{1280,720},{1600,900},{3840,2160},{1920,1200},{1440,1080},{1024,768},{1080,1920},{2560,1080},{5120,1440},{640,480},{1,1},{2,1},{32,32},{8192,8192}}};
  const uint32_t widths[]={0,1,2,3,160,256,320,640,798,800,852,853,854,855,856,1024,1280,1920,3754};
  const uint32_t heights[]={0,1,2,3,224,240,400,448,480,528,576,720,1080};
  const float scales[]={0.f,1.f,1.5f,2.25f,4.f,-1.f,std::numeric_limits<float>::quiet_NaN()};
  const uint32_t limits[]={0,512,1024,16384};
  unsigned cases=0,changes=0,unchangedRequired=0;
  for(auto native:windows)for(auto bw:widths)for(auto bh:heights)for(auto scale:scales)for(auto limit:limits)for(bool fit:{false,true}) {
    configuredWidth=bw;configuredHeight=bh;textureLimit=limit;g_frameBufferAspectFit=fit;
    auto a=baseline::frame_buffer_size_for_scale(native.x,native.y,scale);
    auto b=candidate::frame_buffer_size_for_scale(native.x,native.y,scale);
    ++cases;
    assert(b.x>0&&b.y>0);
    if(limit){assert(b.x<=int(limit)&&b.y<=int(limit));}
    if(!snapping||scale!=0.f||!fit||!bw||!bh){++unchangedRequired;assert(a==b);}
    if(a!=b){
      ++changes;assert(snapping&&scale==0.f&&fit&&bw&&bh);
      auto fitted=baseline::fit_frame_buffer_to_aspect(native.x,native.y,float(bw)/float(bh));
#if defined(__ORBIS__)
      assert(candidate::native_auto_quantization_fit(native.x,native.y,bw,bh,fitted.x,fitted.y));
#endif
      if(limit==0||std::max(native.x,native.y)<=int(limit)) assert(b==native);
    }
  }
  unsigned overflowCases=0;
#if defined(__ORBIS__)
  std::mt19937 random(0x505334u);
  for(unsigned i=0;i<200000;++i){
    int nw=int(random()&INT32_MAX),nh=int(random()&INT32_MAX);
    uint32_t bw=random(),bh=random();
    int fw=nw-std::min<int>(nw,int(random()%12)),fh=nh-std::min<int>(nh,int(random()%12));
    if(i%4==0){bw=UINT32_MAX-(i%1024);bh=UINT32_MAX-((i+31)%1024);nw=INT32_MAX-(i%100);nh=INT32_MAX-((i+31)%100);fw=nw-1;fh=nh-1;}
    assert(candidate::native_auto_quantization_fit(nw,nh,bw,bh,fw,fh)==wide_oracle(nw,nh,bw,bh,fw,fh));++overflowCases;
  }
#endif
  std::array<Vec2<int>,4> oldSequence{{{1920,1080},{1440,1080},{1920,1079},{1917,1080}}};
  std::array<Vec2<int>,4> newSequence{{{1920,1080},output(1920,1080,640,480),output(1920,1080,854,480),output(1920,1080,852,480)}};
  unsigned oldResizes=0,newResizes=0;
  for(unsigned i=1;i<4;++i){oldResizes+=oldSequence[i]!=oldSequence[i-1];newResizes+=newSequence[i]!=newSequence[i-1];}
  assert(oldResizes==3&&newResizes==(snapping?2:3));
  std::printf("mode=%s enabled=%d sizing_cases=%u changed=%u preserved=%u integer_boundary_cases=%u captured_target_changes=%u->%u native_coverage=1917->%d old_present_width=%.0f new_present_width=%.0f\n",
#if defined(__ORBIS__)
  "orbis",
#else
  "desktop",
#endif
  int(enabled),cases,changes,unchangedRequired,overflowCases,oldResizes,newResizes,snapping?1920:1917,oldVp.width,newVp.width);
}
