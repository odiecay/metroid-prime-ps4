// Actual FIFO uniform-reuse regression. Device-owned objects are replaced at
// link time; register decode, configs, UBO packing, geometry and staging are real.
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
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

// Keep the renderer's release-layout ABI while retaining fixture assertions.
#undef assert
#define assert(c) do { if (!(c)) { std::fprintf(stderr, "draw-prep assertion failed: %s at %d\n", #c, __LINE__); std::abort(); } } while (false)

using namespace aurora;
struct Timing {uint64_t calls=0,ns=0,bytes=0;};
enum Metric {Config,Info,Uniform,Storage,Vertices,Indices,TextureResolve,MetricCount};
static std::array<Timing,MetricCount> times;
static bool profile=false;
static bool hostHdr=false;
static bool fakeTextureDevice=false;
static std::vector<u8> canonical;
extern "C" bool wrapHdr() noexcept asm("__wrap__ZN6aurora3gfx21scene_hdr_pass_activeEv");
extern "C" bool wrapHdr() noexcept {return hostHdr;}
static uint64_t now(){struct timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return uint64_t(t.tv_sec)*1000000000u+t.tv_nsec;}
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
extern "C" void wrapResolve(const gx::ShaderInfo& c) noexcept {
 Measure m(TextureResolve);
 if(fakeTextureDevice){for(unsigned i=0;i<gx::MaxTextures;++i)if(c.sampledTextures[i])gx::g_gxState.textures[i].texObj=gx::g_gxState.loadedTextures[i];}
 else realResolve(c);
}
// GPU objects are intentionally excluded; all actual configs are still hashed.
extern "C" gfx::PipelineRef wrapPipeline(const gx::PipelineConfig&,bool) asm("__wrap__ZN6aurora3gfx12pipeline_refERKNS_2gx14PipelineConfigEb");
extern "C" gfx::PipelineRef wrapPipeline(const gx::PipelineConfig& c,bool){return xxh3_hash(c,static_cast<HashType>(gfx::ShaderType::GX));}
extern "C" gfx::PipelineRef wrapPipelineTemplate(const gx::PipelineConfig&) asm("__wrap__ZN6aurora3gfx12pipeline_refINS_2gx14PipelineConfigEEEmRKT_");
extern "C" gfx::PipelineRef wrapPipelineTemplate(const gx::PipelineConfig& c){return wrapPipeline(c,false);}
extern "C" bool wrapTouch(gfx::PipelineRef) asm("__wrap__ZN6aurora3gfx14touch_pipelineEm");
extern "C" bool wrapTouch(gfx::PipelineRef){return true;}
extern "C" gx::GXBindGroups wrapGroups(const gx::ShaderInfo&) noexcept asm("__wrap__ZN6aurora2gx17build_bind_groupsERKNS0_10ShaderInfoE");
extern "C" gx::GXBindGroups wrapGroups(const gx::ShaderInfo& info) noexcept {
 gx::GXBindGroups result{};
 if(fakeTextureDevice)for(unsigned i=0;i<gx::MaxTextures;++i)if(info.sampledTextures[i])
  result.textureBindGroup^=uint64_t(gx::g_gxState.textures[i].texObj.texObjId+1)*(0x1234567ull+i);
 return result;
}


struct Model {std::vector<u8> positions,normals,uvs;};
static std::array<Model,16> models;
static void be(std::vector<u8>& out,uint64_t v,unsigned n){while(n)out.push_back(u8(v>>(--n*8)));}
static void auroraCommand(std::vector<u8>& out,u16 c){out.push_back(GX_AURORA);be(out,c,2);}
static void array(std::vector<u8>& out,GXAttr a,const std::vector<u8>& data){auroraCommand(out,GX_AURORA_LOAD_ARRAYBASE+a-GX_VA_POS);be(out,reinterpret_cast<uintptr_t>(data.data()),8);be(out,data.size(),4);be(out,0,1);}
static std::vector<u8> workload(bool materials,bool matrices,GXPrimitive primitive,bool raster=false,unsigned draws=16200,unsigned kind=0){
 std::vector<u8> out;
 for(unsigned draw=0;draw<draws;++draw){
  const unsigned group=draw/18;
  if(draw%18==0){
   const auto& model=models[group%models.size()];
   array(out,GX_VA_POS,model.positions);array(out,GX_VA_NRM,model.normals);array(out,GX_VA_TEX0,model.uvs);array(out,GX_VA_TEX1,model.uvs);
   auroraCommand(out,GX_AURORA_PORT_DRAW_SERIAL);be(out,group+1,4);
   if(matrices && (kind!=4 || group%4==0)){out.push_back(GX_LOAD_XF_REG);be(out,(3u<<16),4);for(unsigned i=0;i<4;++i)be(out,std::bit_cast<u32>(float(group*4+i)),4);}
   if(raster){out.push_back(GX_LOAD_BP_REG);be(out,(0x41u<<24)|1u|8u|16u|(GX_BL_SRCALPHA<<8)|((group&1?GX_BL_ONE:GX_BL_INVSRCALPHA)<<5),4);}
   if(materials){out.push_back(GX_LOAD_BP_REG);be(out,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|((group&1)?GX_CC_RASC:GX_CC_KONST),4);}
   if(kind==1||kind==4){out.push_back(GX_LOAD_BP_REG);be(out,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|GX_CC_RASC|((group&1)<<19),4);}
   if(kind==2||kind==3){
    auroraCommand(out,GX_AURORA_LOAD_TEXOBJ);be(out,0,1);be(out,0x100000ull+group*128,8);be(out,0,8);
    be(out,64+(kind==3?group%2:0),4);be(out,128,4);be(out,GX_TF_RGBA8,4);be(out,0,4);be(out,0,1);be(out,group+1,4);be(out,1,4);
   }

  }
  out.push_back(primitive|GX_VTXFMT0);be(out,6,2);
  for(unsigned v=0;v<6;++v){be(out,v,2);be(out,v,2);be(out,v,2);be(out,v,2);}
 }
 return out;
}
static void state(){
 gx::g_gxState={};auto& s=gx::g_gxState;
 for(auto a:{GX_VA_POS,GX_VA_NRM,GX_VA_TEX0}){
  s.vtxDesc[a]=GX_INDEX16;s.vtxFmts[0].attrs[a]={a==GX_VA_TEX0?GX_TEX_ST:(a==GX_VA_NRM?GX_NRM_XYZ:GX_POS_XYZ),GX_F32,0};
  s.arrays[a].stride=a==GX_VA_TEX0?8:12;
 }
 s.vtxDesc[GX_VA_TEX1]=GX_INDEX16;s.vtxFmts[0].attrs[GX_VA_TEX1]=s.vtxFmts[0].attrs[GX_VA_TEX0];s.arrays[GX_VA_TEX1].stride=8;
 s.numTevStages=1;s.numChans=1;
 s.tevStages[0].colorPass.d=GX_CC_RASC;s.tevStages[0].alphaPass.d=GX_CA_RASA;
 s.tevStages[0].channelId=GX_COLOR0A0;
 s.colorChannelConfig[0].lightingEnabled=true;s.colorChannelConfig[1].lightingEnabled=true;
 s.colorChannelState[0].matColor={1,1,1,1};s.colorChannelState[0].ambColor={.2f,.2f,.2f,1};
 s.logicalViewport={0,0,1917,1080,0,1};s.logicalScissor={0,0,1917,1080};
 s.renderViewport=s.logicalViewport;s.renderScissor=s.logicalScissor;
}
static void reuse_transition_proof(gfx::detail::FramePacket& frame){
 unsigned cases=0,resets=0;
 fakeTextureDevice=true;
 for(unsigned epoch=0;epoch<32;++epoch){
  state();auto& st=gx::g_gxState;
  for(auto [attr,data]:{std::pair{GX_VA_POS,&models[0].positions},std::pair{GX_VA_NRM,&models[0].normals},std::pair{GX_VA_TEX0,&models[0].uvs},std::pair{GX_VA_TEX1,&models[0].uvs}}){st.arrays[attr].data=data->data();st.arrays[attr].size=data->size();}
  gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
  frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
  gfx::detail::begin_recording(frame,0);
  auto xf=[&](u16 addr,u32 value){std::array<u8,4> bytes{u8(value>>24),u8(value>>16),u8(value>>8),u8(value)};gx::fifo::handle_xf(addr,bytes);};
  for(unsigned step=0;step<48;++step){
   const unsigned action=step%16;const u32 n=epoch*48+step+1;
   if(action==0||action==1)gx::fifo::handle_bp((0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|GX_CC_RASC|((step&1)<<19));
   if(action==2)xf(0,std::bit_cast<u32>(float(n)));
   if(action==3)gx::fifo::handle_bp((0xf3u<<24)|(n&0xff)|((GX_GREATER&7)<<16));
   if(action==4)xf(0x100e,((n&1)<<1)|(1u<<6));
   if(action==5)xf(0x100a,n*1357u);
   if(action==6){st.numTexGens=1;st.tevStages[0].texCoordId=GX_TEXCOORD0;st.tevStages[0].texMapId=GX_TEXMAP0;st.tevStages[0].colorPass.d=GX_CC_TEXC;st.dirty|=gx::DirtyPipeline;}
   if(action==7||action==8||action==9){auto& t=st.loadedTextures[0];t.mWidth=64+(action==9);t.mHeight=128;t.texObjId=n;t.mode0=action==8?(1u<<9):0;st.dirty|=gx::DirtyTextures;}
   if(action==10){auto viewport=st.renderViewport;viewport.width=0.f;gx::set_render_viewport(viewport);}
   if(action==11){auto viewport=st.renderViewport;viewport.width=std::bit_cast<float>(0x80000000u);gx::set_render_viewport(viewport);gx::fifo::handle_bp((0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|GX_CC_TEXC|((step&1)<<19));}
   if(action==12){auto viewport=st.renderViewport;viewport.width=1917.f;gx::set_render_viewport(viewport);}
   if(action==13){st.pbr=n&1;st.dirty|=gx::DirtyPipeline;}
   if(action==14){st.pbr=0;st.dirty|=gx::DirtyPipeline;}
   if(action==15){gx::fifo::clear_draw_cache();++resets;}
   std::vector<u8> cmd;auroraCommand(cmd,GX_AURORA_PORT_DRAW_SERIAL);be(cmd,n,4);cmd.push_back(GX_TRIANGLESTRIP|GX_VTXFMT0);be(cmd,6,2);
   for(unsigned v=0;v<6;++v)for(unsigned a=0;a<4;++a)be(cmd,v,2);
   const auto result=gx::fifo::process(cmd.data(),cmd.size());assert(result.bytesProcessed==cmd.size());
   const auto* draw=gfx::get_last_draw_command<gx::DrawData>();assert(draw&&draw->immediateData.serial==n);
   gx::PipelineConfig config;realConfig(config,GX_TRIANGLESTRIP,GX_VTXFMT0);const auto info=realInfo(config.shaderConfig);
   const auto expected=realUniform(info);
   assert(draw->pipeline==xxh3_hash(config,static_cast<HashType>(gfx::ShaderType::GX)));
   assert(draw->uniformRange.size==expected.size);
   assert(!std::memcmp(frame.uniforms.data()+draw->uniformRange.offset,frame.uniforms.data()+expected.offset,expected.size));
   ++cases;
  }
  gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
 }
 fakeTextureDevice=false;gx::fifo::clear_draw_cache();
 std::printf("REUSE_TRANSITIONS cases=%u cache_resets=%u frame_resets=32 exact_ubo_pipeline=1\n",cases,resets);
}

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
 const char* names[]={"cached_triangles","uniform_triangles","material_uniform_triangles","material_uniform_strips","raster_uniform_triangles","raster_uniform_strips","shader_only_triangles","shader_only_strips","same_sized_texture_triangles","same_sized_texture_strips","changed_texture_width_strips","mixed_shader_matrix_strips","cached_raster_strips"};
 for(unsigned scenario : {0u,6u,7u,9u,10u,11u}){
  const unsigned kind=scenario==6||scenario==7?1:scenario==8||scenario==9?2:scenario==10?3:scenario==11?4:0;
  fakeTextureDevice=kind==2||kind==3;
  const bool strips=scenario==3||scenario==5||scenario==7||scenario>=9;
  const bool matrices=scenario>=1&&scenario<6||scenario==11;
  const auto commands=workload(scenario==2||scenario==3,matrices,strips?GX_TRIANGLESTRIP:GX_TRIANGLES,scenario==4||scenario==5||scenario==12,16200,kind);
  std::array<uint64_t,9> samples{};std::array<Timing,MetricCount> totals{};
  uint64_t uniformHash=0,vertexHash=0,indexHash=0;
  for(unsigned iteration=0;iteration<14;++iteration){
   state();
   if(fakeTextureDevice){auto& st=gx::g_gxState;st.numTexGens=1;st.tevStages[0].colorPass.d=GX_CC_TEXC;st.tevStages[0].alphaPass.d=GX_CA_TEXA;st.tevStages[0].texCoordId=GX_TEXCOORD0;st.tevStages[0].texMapId=GX_TEXMAP0;}
   gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
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
    if(iteration==13){
     uniformHash=xxh3_hash_s(frame.uniforms.data()+d->uniformRange.offset,d->uniformRange.size,uniformHash);
     auto save=[&](const auto& value){const auto* bytes=reinterpret_cast<const u8*>(&value);canonical.insert(canonical.end(),bytes,bytes+sizeof(value));};
     save(scenario);save(d->pipeline);save(d->immediateData);save(d->vtxCount);save(d->indexCount);save(d->instanceCount);save(d->bindGroups.textureBindGroup);save(d->dstAlpha);save(d->shadowGroup);save(d->uniformRange.size);
     canonical.insert(canonical.end(),frame.uniforms.data()+d->uniformRange.offset,frame.uniforms.data()+d->uniformRange.offset+d->uniformRange.size);
    }
    const auto& m=models[draws%models.size()];
    for(auto [attr,bytes]:{std::pair{GX_VA_POS,&m.positions},std::pair{GX_VA_NRM,&m.normals},std::pair{GX_VA_TEX0,&m.uvs}}){
     const auto off=d->immediateData.arrayStart[attr-GX_VA_POS];assert(off+bytes->size()<=frame.storage.size());
     assert(!std::memcmp(frame.storage.data()+off,bytes->data(),bytes->size()));
    }
    ++draws;
   }
   assert(draws==900);
   if(iteration==13){vertexHash=xxh3_hash_s(frame.verts.data(),frame.verts.size());indexHash=xxh3_hash_s(frame.indices.data(),frame.indices.size());}
   gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
  }
  std::sort(samples.begin(),samples.end());
  std::printf("DRAW_PREP scenario=%s draws=900 preparations=16200 fifo_bytes=%zu median_ms=%.6f min_ms=%.6f max_ms=%.6f storage_bytes=%zu vertex_bytes=%zu index_bytes=%zu uniform_bytes=%zu verified_arrays=2700 uniform_fetch_hash=%016llx vertex_hash=%016llx index_hash=%016llx\n",
    names[scenario],commands.size(),samples[4]/1e6,samples.front()/1e6,samples.back()/1e6,frame.storage.size(),frame.verts.size(),frame.indices.size(),frame.uniforms.size(),(unsigned long long)uniformHash,(unsigned long long)vertexHash,(unsigned long long)indexHash);
  const char* labels[]={"pipeline_config","shader_info","uniform_pack","array_staging","vertex_staging","index_staging","texture_resolution"};
  for(unsigned i=0;i<MetricCount;++i)std::printf("DRAW_STAGE scenario=%s stage=%s calls=%llu total_ms=%.6f bytes=%llu\n",names[scenario],labels[i],(unsigned long long)totals[i].calls,totals[i].ns/1e6,(unsigned long long)totals[i].bytes);
 }
 if(!getenv("GX_REUSE_BENCH_ONLY"))reuse_transition_proof(frame);
 if(const char* dump=getenv("GX_REUSE_CANONICAL")){std::ofstream out(dump,std::ios::binary);out.write(reinterpret_cast<const char*>(canonical.data()),canonical.size());assert(bool(out));}
 std::printf("REUSE_CANONICAL bytes=%zu hash=%016llx\n",canonical.size(),(unsigned long long)xxh3_hash_s(canonical.data(),canonical.size()));
 gfx::detail::shutdown_recording();gx::g_gxState={};
}
