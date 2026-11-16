#include "frame_staging.hpp"
#include "gfx/recording.hpp"
#include "gfx/resource_cache.hpp"
#include "gfx/hash.hpp"
#include "gx/gx.hpp"
#include "gx/shader_info.hpp"
#include "webgpu/gpu.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <type_traits>
#include <vector>
#undef assert
#define assert(c) do {if(!(c)){std::fprintf(stderr,"uniform assertion: %s line%d\n",#c,__LINE__);std::abort();}}while(false)
using namespace aurora;
namespace aurora::gx {gfx::Range baseline_build_uniform(const ShaderInfo&) noexcept;}
static uint32_t seed=0x5052494d;
static uint32_t random32(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static float number(){return std::bit_cast<float>((random32()&0x807fffff)|0x3f000000);}
template<class T>static void fill(T& value){
 if constexpr(requires {value.x();value.y();value.z();value.w();})value=T{number(),number(),number(),number()};
 else if constexpr(requires {value.m0;value.m1;value.m2;value.m3;}){fill(value.m0);fill(value.m1);fill(value.m2);fill(value.m3);}
 else if constexpr(requires {value.m0;value.m1;value.m2;}){fill(value.m0);fill(value.m1);fill(value.m2);}
 else if constexpr(requires {value.m0;value.m1;}){fill(value.m0);fill(value.m1);}
 else if constexpr(requires {value.pos;value.nrm;}){fill(value.pos);fill(value.nrm);}
 else if constexpr(requires {value.pos;value.dir;value.color;value.cosAtt;value.distAtt;}){fill(value.pos);fill(value.dir);fill(value.color);fill(value.cosAtt);fill(value.distAtt);}
 else if constexpr(requires {value.begin();value.end();}){for(auto& element:value)fill(element);}
 else if constexpr(requires {value.x;value.y;})value=T{number(),number()};
 else static_assert(!sizeof(T),"unsupported uniform input type");
}

static void state(unsigned n){
 auto& s=gx::g_gxState;
 fill(s.pnMtx);fill(s.texMtxs);fill(s.ptTexMtxs);fill(s.proj);
 if((n&7)<2){const uint32_t bits=(n&7)?0x7fc54321:0x80000000;s.pnMtx[n%10].pos.m0.x()=std::bit_cast<float>(bits);}
 s.renderViewport={0.f,0.f,1917.f+float(n%7),1080.f,0.f,1.f};s.logicalViewport={0.f,0.f,640.f,480.f,0.f,1.f};
 s.lineWidth=n%255;s.pointSize=(n*3)%255;s.lineHalfAspect=n&1;
 s.lineTexOffset=GXTexOffset(n%6);s.pointTexOffset=GXTexOffset((n+1)%6);
 for(auto& c:s.colorChannelState){c.lightMask=random32()&255;fill(c.ambColor);fill(c.matColor);}
 fill(s.colorRegs);fill(s.kcolors);fill(s.lights);
 for(auto& light:s.lights)light.color=Vec4<float>{float(random32()%101)/100,float(random32()%101)/100,float(random32()%101)/100,1.f};
 fill(s.pbrProbe);fill(s.pbrEmissive);fill(s.pbrBacklight);fill(s.pbrLayer);fill(s.pbrLayerHeight);
 fill(s.pbrParam);fill(s.pbrUp);fill(s.pbrCubeParams);fill(s.pbrAmbient);fill(s.pbrVolumeRows);fill(s.pbrTone);
 fill(s.pbrBacklightLights);fill(s.pbrLightSkip);fill(s.pbrLightScale);fill(s.pbrBakedLightModulation);
 fill(s.pbrLightHdr);fill(s.pbrShield);fill(s.pbrLightmapRect);fill(s.pbrLightmapAxes);fill(s.pbrRoomLights);
 fill(s.pbrModelDiffuse);fill(s.pbrModelIncan);fill(s.pbrModelCoverage);fill(s.pbrThermal);fill(s.hudDyin);
 fill(s.clipPlaneEq);fill(s.shellUniform);fill(s.volFogParams);fill(s.volFogTone);fill(s.shadowUniform);fill(s.colorMatrix);
 s.pbrBrdfLut=n&1;fill(s.fog.color);s.fog.a=float(n);s.fog.b=-float(n);s.fog.c=.125f*n;s.fog.type=n%2?GX_FOG_LIN:GX_FOG_EXP;
 s.blendMode=GXBlendMode(n%4);s.blendFacSrc=GXBlendFactor(n%8);s.blendFacDst=GXBlendFactor((n+3)%8);
 s.alphaCompare.ref0=n&255;s.alphaCompare.ref1=(n*7)&255;
 for(unsigned i=0;i<gx::MaxTexCoord;++i){
  s.texCoordScales[i].scaleS=random32()&4095;s.texCoordScales[i].scaleT=random32()&4095;
  s.texCoordScales[i].lineOffset=random32()&1;s.texCoordScales[i].pointOffset=random32()&1;
  s.tcgs[i].mtx=n%3==0?GX_IDENTITY:GXTexMtx(GX_TEXMTX0+3*((n+i)%10));
 }
 for(auto& matrix:s.indTexMtxs){fill(matrix.mtx);matrix.scaleExp=int(n%21)-10;}
}
static gx::ShaderConfig config(unsigned n){
 gx::ShaderConfig c{};c.lineMode=n%4;c.pbr=(n>>2)&1;c.sceneHdr=(n>>3)&1;c.pbrThermal=(n>>4)&1;
 c.hudSample=(n>>5)%4;c.clipPlane=(n>>7)&1;c.shell=(n>>8)%3;c.volFog=(n>>10)%4;
 c.shadow=(n>>12)&1;c.colorMatrix=(n>>11)&1;c.fogType=n&1?GX_FOG_LIN:GX_FOG_NONE;
 c.alphaCompare.ref0=gx::AlphaRefUniform;c.pbrLightmapAttr=GX_VA_TEX0;c.tevStageCount=4;c.numIndStages=4;
 for(unsigned i=0;i<4;++i){auto& t=c.tevStages[i];
  t.colorPass={GX_CC_CPREV,GX_CC_TEXC,GX_CC_RASC,GX_CC_KONST};t.alphaPass={GX_CA_APREV,GX_CA_TEXA,GX_CA_RASA,GX_CA_KONST};
  t.colorOp.outReg=GXTevRegID(i%4);t.alphaOp.outReg=GXTevRegID(i%4);t.texCoordId=GXTexCoordID(i);t.texMapId=GXTexMapID(i);
  t.channelId=GXChannelID(i%2?GX_COLOR1A1:GX_COLOR0A0);t.kcSel=GXTevKColorSel(GX_TEV_KCSEL_K0+i);
  t.kaSel=GXTevKAlphaSel(GX_TEV_KASEL_K0_R+i*4);t.indTexStage=GXIndTexStageID(i);t.indTexMtxId=GXIndTexMtxID(i%3);
  c.indStages[i].texMapId=GXTexMapID(i+4);c.indStages[i].texCoordId=GXTexCoordID(i+4);
 }
 for(auto& channel:c.colorChannels){channel.lightingEnabled=n&1;channel.matSrc=GX_SRC_REG;channel.ambSrc=GX_SRC_REG;}
 for(unsigned i=0;i<gx::MaxTexCoord;++i)c.tcgs[i].postMtx=n&2?GXPTTexMtx(GX_PTTEXMTX0+(i%8)*3):GX_PTIDENTITY;
 return c;
}
static void clear(gfx::detail::FramePacket& frame){
 frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
 frame.spillVerts={};frame.spillIndices={};frame.spillStorage={};frame.spillUniforms={};
}
static uint64_t cpu(){timespec t{};assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0);return uint64_t(t.tv_sec)*1000000000ull+t.tv_nsec;}
static void reset(gfx::detail::FramePacket& frame){gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);clear(frame);gfx::detail::begin_recording(frame,0);}
int main(){
 webgpu::g_graphicsConfig.surfaceConfiguration.format=wgpu::TextureFormat::RGBA8Unorm;
 webgpu::g_graphicsConfig.depthFormat=wgpu::TextureFormat::Depth24Plus;webgpu::g_graphicsConfig.msaaSamples=1;
 webgpu::g_frameBuffer.size={1917,1080,1};webgpu::g_frameBuffer.format=wgpu::TextureFormat::RGBA8Unorm;
 webgpu::g_depthBuffer.size={1917,1080,1};webgpu::g_depthBuffer.format=wgpu::TextureFormat::Depth24Plus;
 gfx::detail::resources().limits.minUniformBufferOffsetAlignment=256;gfx::detail::resources().limits.minStorageBufferOffsetAlignment=256;
 gfx::detail::testing::suppress_render_worker(true);
 gfx::detail::testing::StagingPool staging;gfx::detail::FramePacket frame;staging.attach(frame);gfx::detail::begin_recording(frame,0);
 if(!getenv("UNIFORM_BENCH_ONLY")){
  uint64_t total=0;unsigned cases=0,maxSize=0;
  for(unsigned epoch=0;epoch<128;++epoch){
   if(epoch)reset(frame);gx::g_gxState={};
   for(unsigned n=0;n<64;++n){
    const unsigned id=epoch*64+n;state(id);const auto info=gx::build_shader_info(config(id));
    const auto expected=gx::baseline_build_uniform(info),actual=gx::build_uniform(info);
    assert(expected.size==actual.size&&actual.size<=gx::MaxUniformSize);
    assert(expected.offset+expected.size<=frame.uniforms.size()&&actual.offset+actual.size<=frame.uniforms.size());
    assert(!std::memcmp(frame.uniforms.data()+expected.offset,frame.uniforms.data()+actual.offset,actual.size));
    total+=actual.size;maxSize=std::max(maxSize,actual.size);++cases;
   }
  }
  std::printf("UNIFORM_BYTES cases=%u frame_resets=127 bytes=%llu max_uniform=%u exact_bits=1\n",cases,(unsigned long long)total,maxSize);
 }
 const bool candidate=getenv("UNIFORM_CANDIDATE")&&std::strcmp(getenv("UNIFORM_CANDIDATE"),"0");
 const auto pack=candidate?gx::build_uniform:gx::baseline_build_uniform;
 for(unsigned scene=0;scene<3;++scene){
  gx::g_gxState={};state(4);auto c=config(scene?16383:1);
  if(!scene){c={};c.tevStageCount=1;c.tevStages[0].colorPass.d=GX_CC_RASC;c.tevStages[0].alphaPass.d=GX_CA_RASA;c.tevStages[0].channelId=GX_COLOR0A0;
    c.colorChannels[0].lightingEnabled=true;c.colorChannels[GX_ALPHA0].lightingEnabled=true;}
  const auto info=gx::build_shader_info(c);std::array<uint64_t,20> samples{};uint64_t check=0;
  for(unsigned trial=0;trial<24;++trial){reset(frame);const auto start=cpu();
   for(unsigned draw=0;draw<900;++draw){
    const float value=float(draw),negative=-value;
    gx::g_gxState.pnMtx[draw%10].pos.m0.x()=value;gx::g_gxState.texMtxs[draw%10].m0.x()=negative;
    gx::g_gxState.colorChannelState[0].matColor.x()=float(draw%256)/255;pack(info);
   }
   const auto duration=cpu()-start;if(trial>=4)samples[trial-4]=duration;
   if(trial==23)check=xxh3_hash_s(frame.uniforms.data(),frame.uniforms.size());
  }
  std::sort(samples.begin(),samples.end());
  std::printf("UNIFORM_PACK scene=%u candidate=%u ubos=900 cpu_median_ms=%.6f min_ms=%.6f max_ms=%.6f bytes=%zu hash=%016llx\n",scene,candidate,samples[10]/1e6,samples.front()/1e6,samples.back()/1e6,frame.uniforms.size(),(unsigned long long)check);
 }
 gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);gfx::detail::shutdown_recording();gx::g_gxState={};
}
