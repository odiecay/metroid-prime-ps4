// Real complete GX builders against Dawn Null, with its advertised16-texture limit.
// The optional17th lightmap entry is a descriptor-only equality test, never API validation.
#include "gx/gx.hpp"
#include "gx/texture.hpp"
#include "gfx/frame.hpp"
#include "gfx/recording.hpp"
#include "gfx/resources.hpp"
#include "gfx/resource_cache.hpp"
#include "gfx/probe.hpp"
#include "gfx/volfog.hpp"
#include "gfx/hash.hpp"
#include "webgpu/gpu.hpp"
#include "src/dawn/native/BindGroup.h"
#include "src/dawn/native/dawn_platform.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <ctime>
#undef assert
#define assert(c) do{if(!(c)){std::fprintf(stderr,"binding assertion %s line%d\n",#c,__LINE__);std::abort();}}while(false)
using namespace aurora;
#include "gx_bindings_device.inc"
namespace aurora::gx {
GXBindGroups reference_build_bind_groups(const ShaderInfo&) noexcept;
size_t test_ldr_binding_scratch_size() noexcept;
}
static uint32_t producerFrame=0;
extern "C" uint32_t wrapFrame() noexcept asm("__wrap__ZN6aurora3gfx13current_frameEv");
extern "C" uint32_t wrapFrame() noexcept {return producerFrame;}
struct Capture {
 uint64_t id;WGPUBindGroupLayout layout;std::vector<std::array<uint64_t,7>> entries;
 bool operator==(const Capture&)const=default;
};
static std::vector<Capture> captures;static bool capture=true,reenter=false,inside=false,descriptorOnly=false;
extern "C" gfx::BindGroupRef realGroup(gfx::BindGroupRef,const WGPUBindGroupDescriptor&) asm("__real__ZN6aurora3gfx14bind_group_refEmRK23WGPUBindGroupDescriptor");
extern "C" gfx::BindGroupRef wrapGroup(gfx::BindGroupRef,const WGPUBindGroupDescriptor&) asm("__wrap__ZN6aurora3gfx14bind_group_refEmRK23WGPUBindGroupDescriptor");
extern "C" gfx::BindGroupRef wrapGroup(gfx::BindGroupRef id,const WGPUBindGroupDescriptor& d){
 if(capture){Capture c{id,d.layout,{}};for(size_t n=0;n<d.entryCount;++n){const auto&e=d.entries[n];c.entries.push_back({uintptr_t(e.nextInChain),e.binding,uintptr_t(e.buffer),e.offset,e.size,uintptr_t(e.sampler),uintptr_t(e.textureView)});}captures.push_back(std::move(c));}
 if(reenter&&!inside){inside=true;gx::build_bind_groups(gx::ShaderInfo{.sampledTextures=std::bitset<gx::MaxTextures>{3}});inside=false;}
 if(descriptorOnly)return id;
 return realGroup(id,d);
}
static uint64_t cpuNow(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct Fixture {
 Device d;std::array<gfx::TextureHandle,16> textures;std::array<uint8_t,65536> uploadMemory{};std::array<uint8_t,gx::MaxUniformSize> uniformMemory{};gfx::detail::FramePacket frame;
 Fixture(bool benchmark,bool lightmap):d(benchmark){
  webgpu::g_device=d.device;webgpu::g_hasCoreFeatures=true;webgpu::g_lightmapBinding=lightmap;
  webgpu::g_graphicsConfig.surfaceConfiguration.format=wgpu::TextureFormat::RGBA8Unorm;webgpu::g_graphicsConfig.depthFormat=wgpu::TextureFormat::Depth24Plus;webgpu::g_graphicsConfig.msaaSamples=1;
  webgpu::g_frameBuffer.size={16,16,1};webgpu::g_frameBuffer.format=wgpu::TextureFormat::RGBA8Unorm;
  wgpu::BindGroupLayoutDescriptor empty{};gfx::detail::resources().staticBindGroupLayout=d.device.CreateBindGroupLayout(&empty);gfx::detail::resources().uniformBindGroupLayout=d.device.CreateBindGroupLayout(&empty);
  gx::g_gxState={};gx::initialize();
  frame.textureUpload=ByteBuffer{uploadMemory.data(),uploadMemory.size()};frame.uniforms=ByteBuffer{uniformMemory.data(),uniformMemory.size()};gfx::detail::testing::suppress_render_worker(true);gfx::detail::begin_recording(frame,0);
  for(unsigned i=0;i<textures.size();++i){wgpu::TextureDescriptor td{};td.size={16,16,1};td.format=wgpu::TextureFormat::RGBA8Unorm;td.usage=wgpu::TextureUsage::TextureBinding;auto t=d.device.CreateTexture(&td);auto v=t.CreateView();textures[i]=std::make_shared<gfx::TextureRef>(t,v,v,td.size,td.format,1,GX_TF_RGBA8);}
 }
 ~Fixture(){
  reenter=false;captures.clear();frame.textureUploads.clear();gfx::finish();gfx::detail::end_recording();gfx::detail::shutdown_recording();
  gx::shutdown();gfx::detail::shutdown_resource_cache();gfx::probe::shutdown();gfx::volfog::shutdown();
  gfx::detail::resources().staticBindGroupLayout={};gfx::detail::resources().uniformBindGroupLayout={};textures.fill({});webgpu::g_device={};
 }
 gx::ShaderInfo material(unsigned n,bool advanced=false){
  gx::g_gxState.pbr=0;gx::g_gxState.pbrCube=gx::g_gxState.pbrVolume=gx::g_gxState.pbrLightmap=0;
  for(unsigned i=0;i<gx::MaxTextures;++i){GXTexObj_ obj{};obj.mode0=((n+i)%3)|(((n/3+i)%3)<<2)|((n&1)<<4)|(((n>>1)&1)<<5);obj.mWidth=obj.mHeight=16;obj.mFormat=GX_TF_RGBA8;gx::g_gxState.textures[i]=gfx::TextureBind{obj,((n+i)%11)?textures[(n+i)%textures.size()]:gfx::TextureHandle{}};}
  gx::ShaderInfo info{};info.sampledTextures=std::bitset<gx::MaxTextures>{(n*149u)&255u};info.sampledIndTextures=std::bitset<gx::MaxTextures>{(n/7u)&255u};
  if(advanced)switch(n%8){case 0:gx::g_gxState.pbr=1;break;case 1:gx::g_gxState.pbrCube=123;break;case 2:gx::g_gxState.pbrVolume=123;break;case 3:gx::g_gxState.pbrLightmap=123;break;case 4:info.usesHdrTone=true;break;case 5:info.usesVolFog=true;break;case 6:info.usesLightmap=true;break;case 7:info.shellScene=true;break;}
  return info;
 }
};
static unsigned compared=0;
static void compare(const gx::ShaderInfo& info){
 captures.clear();auto a=gx::reference_build_bind_groups(info);auto before=captures;captures.clear();auto b=gx::build_bind_groups(info);assert(a.textureBindGroup==b.textureBindGroup);assert(before==captures);++compared;
}
static void bench(Fixture& f){
 capture=false;using Fn=gx::GXBindGroups(*)(const gx::ShaderInfo&)noexcept;
 const bool same=getenv("GX_BIND_SAME_CODE")!=nullptr;
 for(unsigned scenario=0;scenario<5;++scenario){
  auto info=f.material(17);info.sampledIndTextures.reset();info.sampledTextures=std::bitset<gx::MaxTextures>{scenario==0?1u:scenario==1?3u:scenario==2?255u:scenario==3?3u:0u};
  if(scenario==3)info.usesHdrTone=true;
  for(unsigned n=0;n<128;++n){gx::g_gxState.textures[0].ref=f.textures[n&15];gx::reference_build_bind_groups(info);gx::build_bind_groups(info);}
  for(unsigned block=0;block<4;++block)for(unsigned order=0;order<4;++order){
   const unsigned label=(order==1||order==2)^(block&1);Fn fn=label&&!same?gx::build_bind_groups:gx::reference_build_bind_groups;
   uint64_t hash=0;constexpr unsigned Calls=50000;const auto start=cpuNow();
   for(unsigned n=0;n<Calls;++n){gx::g_gxState.textures[0].ref=f.textures[n&15];hash+=fn(info).textureBindGroup;}
   const auto elapsed=cpuNow()-start;
   std::printf("BIND_BENCH scenario=%u block=%u label=%u calls=%u cpu_ns=%llu checksum=%llu same_code=%u\n",scenario,block,label,Calls,(unsigned long long)elapsed,(unsigned long long)hash,unsigned(same));
  }
 }
 capture=true;
}
int main(int argc,char**argv){
 const bool benchmark=argc>1&&!std::strcmp(argv[1],"bench");
 if(benchmark){Fixture f(true,false);bench(f);return 0;}
 for(unsigned cycle=0;cycle<(benchmark?1:8);++cycle){
  Fixture f(benchmark,false);producerFrame=1000+cycle*100;
  for(unsigned i=0;i<(benchmark?16:512);++i){auto info=f.material(i+cycle*71);compare(info);}
  if(!benchmark){for(unsigned i=0;i<128;++i)compare(f.material(i,true));descriptorOnly=true;webgpu::g_lightmapBinding=true;for(unsigned i=0;i<128;++i)compare(f.material(i+19));webgpu::g_lightmapBinding=false;descriptorOnly=false;}
 }
 std::printf("GX_BINDINGS paired_builders=%u actual_api_pairs=5120 lightmap17_descriptor_only_pairs=1024 devices=%u descriptors_hashes_and_fields_equal=1 scratch_bytes=%zu\n",compared,benchmark?1:8,gx::test_ldr_binding_scratch_size());
}
