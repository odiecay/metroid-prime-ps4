// Host CPU investigation of the real GX process()/draw preparation paths.
// Only device-owned pipeline/bind-group creation is replaced at link time;
// register decode, configs, shader info, UBO packing, indices and staging are real.
#include "frame_staging.hpp"
#include "gfx/recording.hpp"
#include "gfx/resource_cache.hpp"
#include "gfx/hash.hpp"
#include "gx/command_processor.hpp"
#include "gx/gx.hpp"
#include "gx/pipeline.hpp"
#include "gx/shader_info.hpp"
#include "gx/regs.hpp"
#include "webgpu/gpu.hpp"
#include <dolphin/gx/GXAurora.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Keep the renderer's release-layout ABI while retaining fixture assertions.
#undef assert
#define assert(c) do { if (!(c)) { std::fprintf(stderr, "draw-prep assertion failed: %s at %d\n", #c, __LINE__); std::abort(); } } while (false)

using namespace aurora;
struct Timing {uint64_t calls=0,ns=0,bytes=0;};
enum Metric {Config,Info,Uniform,Storage,Vertices,Indices,TextureResolve,MetricCount};
static std::array<Timing,MetricCount> times;
static bool profile=false;
static bool hostHdr=false;
extern "C" bool wrapHdr() noexcept asm("__wrap__ZN6aurora3gfx21scene_hdr_pass_activeEv");
extern "C" bool wrapHdr() noexcept {return hostHdr;}
static uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Measure {
 Metric m;uint64_t start;
 explicit Measure(Metric m,uint64_t bytes=0):m(m),start(profile?now():0){if(profile){++times[m].calls;times[m].bytes+=bytes;}}
 ~Measure(){if(profile)times[m].ns+=now()-start;}
};

extern "C" void realConfig(gx::PipelineConfig&,GXPrimitive,GXVtxFmt) noexcept asm("__real__ZN6aurora2gx24populate_pipeline_configERNS0_14PipelineConfigE11GXPrimitive8GXVtxFmt");
extern "C" void wrapConfig(gx::PipelineConfig&,GXPrimitive,GXVtxFmt) noexcept asm("__wrap__ZN6aurora2gx24populate_pipeline_configERNS0_14PipelineConfigE11GXPrimitive8GXVtxFmt");
extern "C" void wrapConfig(gx::PipelineConfig& c,GXPrimitive p,GXVtxFmt f) noexcept {Measure m(Config);realConfig(c,p,f);}
extern "C" gx::ShaderInfo realInfo(const gx::ShaderConfig&) noexcept asm("__real__ZN6aurora2gx17build_shader_infoERKNS0_12ShaderConfigE");
extern "C" gx::ShaderInfo wrapInfo(const gx::ShaderConfig&) noexcept asm("__wrap__ZN6aurora2gx17build_shader_infoERKNS0_12ShaderConfigE");
extern "C" gx::ShaderInfo wrapInfo(const gx::ShaderConfig& c) noexcept {Measure m(Info);return realInfo(c);}
extern "C" gfx::Range realUniform(const gx::ShaderInfo&) noexcept asm("__real__ZN6aurora2gx13build_uniformERKNS0_10ShaderInfoE");
extern "C" gfx::Range wrapUniform(const gx::ShaderInfo&) noexcept asm("__wrap__ZN6aurora2gx13build_uniformERKNS0_10ShaderInfoE");
extern "C" gfx::Range wrapUniform(const gx::ShaderInfo& c) noexcept {Measure m(Uniform,c.uniformSize);return realUniform(c);}
extern "C" gfx::Range realStorage(const uint8_t*,size_t) asm("__real__ZN6aurora3gfx12push_storageEPKhm");
extern "C" gfx::Range wrapStorage(const uint8_t*,size_t) asm("__wrap__ZN6aurora3gfx12push_storageEPKhm");
extern "C" gfx::Range wrapStorage(const uint8_t* p,size_t n){Measure m(Storage,n);return realStorage(p,n);}
extern "C" gfx::Range realVerts(const uint8_t*,size_t,size_t) asm("__real__ZN6aurora3gfx10push_vertsEPKhmm");
extern "C" gfx::Range wrapVerts(const uint8_t*,size_t,size_t) asm("__wrap__ZN6aurora3gfx10push_vertsEPKhmm");
extern "C" gfx::Range wrapVerts(const uint8_t* p,size_t n,size_t a){Measure m(Vertices,n);return realVerts(p,n,a);}
extern "C" gfx::Range realIndices(const uint8_t*,size_t,size_t) asm("__real__ZN6aurora3gfx12push_indicesEPKhmm");
extern "C" gfx::Range wrapIndices(const uint8_t*,size_t,size_t) asm("__wrap__ZN6aurora3gfx12push_indicesEPKhmm");
extern "C" gfx::Range wrapIndices(const uint8_t* p,size_t n,size_t a){Measure m(Indices,n);return realIndices(p,n,a);}
extern "C" void realResolve(const gx::ShaderInfo&) noexcept asm("__real__ZN6aurora2gx24resolve_sampled_texturesERKNS0_10ShaderInfoE");
extern "C" void wrapResolve(const gx::ShaderInfo&) noexcept asm("__wrap__ZN6aurora2gx24resolve_sampled_texturesERKNS0_10ShaderInfoE");
extern "C" void wrapResolve(const gx::ShaderInfo& c) noexcept {Measure m(TextureResolve);realResolve(c);}
// GPU objects are intentionally excluded; all actual configs are still hashed.
extern "C" gfx::PipelineRef wrapPipeline(const gx::PipelineConfig&,bool) asm("__wrap__ZN6aurora3gfx12pipeline_refERKNS_2gx14PipelineConfigEb");
extern "C" gfx::PipelineRef wrapPipeline(const gx::PipelineConfig& c,bool){return xxh3_hash(c,static_cast<HashType>(gfx::ShaderType::GX));}
extern "C" gfx::PipelineRef wrapPipelineTemplate(const gx::PipelineConfig&) asm("__wrap__ZN6aurora3gfx12pipeline_refINS_2gx14PipelineConfigEEEmRKT_");
extern "C" gfx::PipelineRef wrapPipelineTemplate(const gx::PipelineConfig& c){return wrapPipeline(c,false);}
extern "C" bool wrapTouch(gfx::PipelineRef) asm("__wrap__ZN6aurora3gfx14touch_pipelineEm");
extern "C" bool wrapTouch(gfx::PipelineRef){return true;}
extern "C" gx::GXBindGroups wrapGroups(const gx::ShaderInfo&) noexcept asm("__wrap__ZN6aurora2gx17build_bind_groupsERKNS0_10ShaderInfoE");
extern "C" gx::GXBindGroups wrapGroups(const gx::ShaderInfo&) noexcept{return {};}

#ifdef GX_RASTER_CONFIG_EQUIVALENCE
namespace aurora::gx::fifo {
#include "actual_raster_config.inc"
}
#endif

struct Model {std::vector<u8> positions,normals,uvs;};
static std::array<Model,16> models;
static void be(std::vector<u8>& out,uint64_t v,unsigned n){while(n)out.push_back(u8(v>>(--n*8)));}
static void auroraCommand(std::vector<u8>& out,u16 c){out.push_back(GX_AURORA);be(out,c,2);}
static void array(std::vector<u8>& out,GXAttr a,const std::vector<u8>& data){auroraCommand(out,GX_AURORA_LOAD_ARRAYBASE+a-GX_VA_POS);be(out,reinterpret_cast<uintptr_t>(data.data()),8);be(out,data.size(),4);be(out,0,1);}
static std::vector<u8> workload(bool materials,bool matrices,GXPrimitive primitive,bool raster=false,unsigned draws=1200){
 std::vector<u8> out;
 for(unsigned draw=0;draw<draws;++draw){
  const auto& model=models[draw%models.size()];
  array(out,GX_VA_POS,model.positions);array(out,GX_VA_NRM,model.normals);array(out,GX_VA_TEX0,model.uvs);
  auroraCommand(out,GX_AURORA_PORT_DRAW_SERIAL);be(out,draw+1,4);
  if(matrices){out.push_back(GX_LOAD_XF_REG);be(out,(3u<<16),4);for(unsigned i=0;i<4;++i)be(out,std::bit_cast<u32>(float(draw*4+i)),4);}
  if(raster){out.push_back(GX_LOAD_BP_REG);be(out,(0x41u<<24)|1u|8u|16u|(GX_BL_SRCALPHA<<8)|((draw&1?GX_BL_ONE:GX_BL_INVSRCALPHA)<<5),4);}
  if(materials){out.push_back(GX_LOAD_BP_REG);be(out,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|((draw&1)?GX_CC_RASC:GX_CC_KONST),4);}
  out.push_back(primitive|GX_VTXFMT0);be(out,96,2);
  for(unsigned v=0;v<96;++v){be(out,v,2);be(out,v,2);be(out,v,2);}
 }
 return out;
}
static void state(){
 gx::g_gxState={};auto& s=gx::g_gxState;
 for(auto a:{GX_VA_POS,GX_VA_NRM,GX_VA_TEX0}){
  s.vtxDesc[a]=GX_INDEX16;s.vtxFmts[0].attrs[a]={a==GX_VA_TEX0?GX_TEX_ST:(a==GX_VA_NRM?GX_NRM_XYZ:GX_POS_XYZ),GX_F32,0};
  s.arrays[a].stride=a==GX_VA_TEX0?8:12;
 }
 s.numTevStages=1;s.numChans=1;
 s.tevStages[0].colorPass.d=GX_CC_RASC;s.tevStages[0].alphaPass.d=GX_CA_RASA;
 s.tevStages[0].channelId=GX_COLOR0A0;
 s.colorChannelConfig[0].lightingEnabled=true;s.colorChannelConfig[1].lightingEnabled=true;
 s.colorChannelState[0].matColor={1,1,1,1};s.colorChannelState[0].ambColor={.2f,.2f,.2f,1};
 s.logicalViewport={0,0,1917,1080,0,1};s.logicalScissor={0,0,1917,1080};
 s.renderViewport=s.logicalViewport;s.renderScissor=s.logicalScissor;
}
#ifndef GX_DRAW_PREPARATION_HELPERS_ONLY
int main(){
 webgpu::g_graphicsConfig.surfaceConfiguration.format=wgpu::TextureFormat::RGBA8Unorm;
 webgpu::g_graphicsConfig.depthFormat=wgpu::TextureFormat::Depth24Plus;
 webgpu::g_graphicsConfig.msaaSamples=1;
 webgpu::g_frameBuffer.size={1917,1080,1};webgpu::g_frameBuffer.format=wgpu::TextureFormat::RGBA8Unorm;
 webgpu::g_depthBuffer.size={1917,1080,1};webgpu::g_depthBuffer.format=wgpu::TextureFormat::Depth24Plus;
 gfx::detail::resources().limits.minUniformBufferOffsetAlignment=256;
 gfx::detail::resources().limits.minStorageBufferOffsetAlignment=256;
 gfx::detail::testing::suppress_render_worker(true);
 gx::testing::set_vertex_array_check(false);
 for(unsigned a=0;a<models.size();++a){
  auto fill=[&](std::vector<u8>& v,size_t n){v.resize(n);for(unsigned i=0;i<n;++i)v[i]=u8(a*31+i*17);};
  fill(models[a].positions,24*1024);fill(models[a].normals,24*1024);fill(models[a].uvs,32*1024);
 }
 gfx::detail::testing::StagingPool staging;gfx::detail::FramePacket frame;staging.attach(frame);
#ifdef GX_RASTER_CONFIG_EQUIVALENCE
 state();gfx::detail::begin_recording(frame,0);
 unsigned compared=0;
 for(const auto pixel:{GX_PF_RGB8_Z24,GX_PF_RGBA6_Z24,GX_PF_RGB565_Z16})for(const u32 dst:{0u,0x55u,UINT32_MAX}){
  state();gx::g_gxState.pixelFmt=pixel;gx::g_gxState.dstAlpha=dst;
  gx::PipelineConfig cached;realConfig(cached,GX_TRIANGLES,GX_VTXFMT0);
  for(u32 i=0;i<4096;++i){
   gx::fifo::handle_bp((0x40u<<24)|((i*17u)&31u));
   gx::fifo::handle_bp((0x41u<<24)|((i*4051u)&0xffffu));
   auto patched=cached;gx::fifo::populate_cached_retail_raster_config(patched);
   gx::PipelineConfig expected;realConfig(expected,GX_TRIANGLES,GX_VTXFMT0);
   assert(!std::memcmp(&patched,&expected,sizeof(expected)));++compared;
  }
 }
 std::printf("RASTER_CONFIG_EQUIVALENCE actual_bp_configs=%u complete_bytes_equal=1\n",compared);
 gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
 for(unsigned mode=0;mode<7;++mode){
  state();auto& s=gx::g_gxState;
  switch(mode){case 0:s.pbr=1;break;case 1:s.volFog=true;break;case 2:s.depthPrepass=1;break;
   case 3:s.actorDecal=true;break;case 4:s.shell=1;break;case 5:s.drawIdMode=true;break;case 6:hostHdr=true;break;}
  gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
  frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
  gfx::detail::begin_recording(frame,0);
  times={};profile=true;const auto c=workload(false,true,GX_TRIANGLES,true,2);
  const auto result=gx::fifo::process(c.data(),c.size());profile=false;
  assert(result.bytesProcessed==c.size()&&times[Config].calls==2);
  gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
 }
 hostHdr=false;
 std::puts("RASTER_ADVANCED_FALLBACK cases=7 full_configs=14");
#endif
 const char* names[]={"cached_triangles","uniform_triangles","material_uniform_triangles","material_uniform_strips","raster_uniform_triangles","raster_uniform_strips"};
 for(unsigned scenario=0;scenario<6;++scenario){
  const auto commands=workload(scenario==2||scenario==3,scenario>=1,(scenario==3||scenario==5)?GX_TRIANGLESTRIP:GX_TRIANGLES,scenario>=4);
  std::array<uint64_t,9> samples{};std::array<Timing,MetricCount> totals{};
  uint64_t uniformHash=0,vertexHash=0,indexHash=0;
  for(unsigned iteration=0;iteration<14;++iteration){
   state();gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
   frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
   gfx::detail::begin_recording(frame,0);
   times={};profile=iteration==13;
   const uint64_t begin=now();const auto result=gx::fifo::process(commands.data(),commands.size());const uint64_t elapsed=now()-begin;
   profile=false;assert(result.bytesProcessed==commands.size()&&!result.drawDone);
   if(iteration>=4&&iteration<13)samples[iteration-4]=elapsed;
   if(iteration==13)totals=times;
   // The actual recorded commands must address byte-identical source arrays.
   unsigned draws=0;for(const auto& pass:frame.renderPasses)for(const auto& cmd:pass.commands)if(cmd.type==gfx::detail::CommandType::Draw){
    const auto* d=reinterpret_cast<const gx::DrawData*>(cmd.data.draw.payload.data());
    if(iteration==13)uniformHash=xxh3_hash_s(frame.uniforms.data()+d->uniformRange.offset,d->uniformRange.size,uniformHash);
    const auto& m=models[draws%models.size()];
    for(auto [attr,bytes]:{std::pair{GX_VA_POS,&m.positions},std::pair{GX_VA_NRM,&m.normals},std::pair{GX_VA_TEX0,&m.uvs}}){
     const auto off=d->immediateData.arrayStart[attr-GX_VA_POS];assert(off+bytes->size()<=frame.storage.size());
     assert(!std::memcmp(frame.storage.data()+off,bytes->data(),bytes->size()));
    }
    ++draws;
   }
   assert(draws==1200);
   if(iteration==13){vertexHash=xxh3_hash_s(frame.verts.data(),frame.verts.size());indexHash=xxh3_hash_s(frame.indices.data(),frame.indices.size());}
   gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
  }
  std::sort(samples.begin(),samples.end());
  std::printf("DRAW_PREP scenario=%s draws=1200 fifo_bytes=%zu median_ms=%.6f min_ms=%.6f max_ms=%.6f storage_bytes=%zu vertex_bytes=%zu index_bytes=%zu uniform_bytes=%zu verified_arrays=3600 uniform_fetch_hash=%016llx vertex_hash=%016llx index_hash=%016llx\n",
    names[scenario],commands.size(),samples[4]/1e6,samples.front()/1e6,samples.back()/1e6,frame.storage.size(),frame.verts.size(),frame.indices.size(),frame.uniforms.size(),(unsigned long long)uniformHash,(unsigned long long)vertexHash,(unsigned long long)indexHash);
  const char* labels[]={"pipeline_config","shader_info","uniform_pack","array_staging","vertex_staging","index_staging","texture_resolution"};
  for(unsigned i=0;i<MetricCount;++i)std::printf("DRAW_STAGE scenario=%s stage=%s calls=%llu total_ms=%.6f bytes=%llu\n",names[scenario],labels[i],(unsigned long long)totals[i].calls,totals[i].ns/1e6,(unsigned long long)totals[i].bytes);
 }
 gfx::detail::shutdown_recording();gx::g_gxState={};
}
#endif
