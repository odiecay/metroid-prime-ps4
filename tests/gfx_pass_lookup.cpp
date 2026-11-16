#include <webgpu/webgpu_cpp.h>
#include "src/dawn/native/CommandBuffer.h"
#include "src/dawn/native/Commands.h"
#include "src/dawn/native/dawn_platform.h"
#include "src/dawn/native/RenderPipeline.h"
#include "src/dawn/native/BindGroup.h"
#include "absl/container/flat_hash_map.h"
#include "pass_lookup_memo.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>
namespace native=dawn::native;
struct Device {
    wgpu::Instance instance;
    wgpu::Device device;
    std::vector<std::string> errors;
    explicit Device(bool benchmark) {
        const std::array features{wgpu::InstanceFeatureName::TimedWaitAny};
        wgpu::InstanceDescriptor id{};
        id.requiredFeatureCount = features.size();
        id.requiredFeatures = features.data();
        instance = wgpu::CreateInstance(&id);
        assert(instance);
        wgpu::RequestAdapterOptions options{};
        options.backendType = wgpu::BackendType::Null;
        wgpu::Adapter adapter;
        instance.WaitAny(instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::RequestAdapterStatus status, wgpu::Adapter value, wgpu::StringView) {
                if (status == wgpu::RequestAdapterStatus::Success) adapter = std::move(value);
            }), 5000000000);
        assert(adapter);
        wgpu::DeviceDescriptor desc{};
        const char* enabled[] = {"skip_validation"};
        wgpu::DawnTogglesDescriptor toggles{};
        if (benchmark) {
            toggles.enabledToggleCount = 1;
            toggles.enabledToggles = enabled;
            desc.nextInChain = &toggles;
        }
        desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message,
                                           std::vector<std::string>* errors) {
            errors->emplace_back(message.data, message.length);
        }, &errors);
        instance.WaitAny(adapter.RequestDevice(&desc, wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::RequestDeviceStatus status, wgpu::Device value, wgpu::StringView) {
                if (status == wgpu::RequestDeviceStatus::Success) device = std::move(value);
            }), 5000000000);
        assert(device);
    }
    ~Device() {
        for (const auto& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        assert(errors.empty());
    }
};

using PipelineRef=uint64_t;using BindGroupRef=uint64_t;
using aurora::gfx::detail::PassLookupMemo;
PipelineRef g_currentPipeline=UINTPTR_MAX;BindGroupRef g_currentTextureGroup=0;
struct CachedPipeline{wgpu::RenderPipeline pipeline;uint32_t lastUsedFrame=0;};
struct CachedBindGroup{wgpu::BindGroup bindGroup;uint32_t lastUsedFrame=0;};
absl::flat_hash_map<PipelineRef,CachedPipeline> g_pipelines;
absl::flat_hash_map<BindGroupRef,CachedBindGroup> g_cachedBindGroups;
std::mutex g_pipelineMutex,g_bindGroupCacheMutex;
uint64_t pipelineLookups=0,groupLookups=0,skipped=0;
#define CHECK(x,...) assert(x)
#include "actual_cache_lookup.inc"
[[gnu::noinline]] bool get_pipeline(PipelineRef ref,wgpu::RenderPipeline& value){++pipelineLookups;return actual_get_pipeline(ref,value);}
[[gnu::noinline]] wgpu::BindGroup find_bind_group(BindGroupRef ref){++groupLookups;return actual_find_bind_group(ref);}
void note_skipped_draw(){++skipped;}
#include "actual_pass_lookup_scopes.inc"
[[gnu::noinline]] bool before_bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass) {
  if (ref == g_currentPipeline) {
    return true;
  }
  wgpu::RenderPipeline pipeline;
  if (!get_pipeline(ref, pipeline)) {
    note_skipped_draw();
    return false;
  }
  pass.SetPipeline(pipeline);
  g_currentPipeline = ref;
  return true;
}
[[gnu::noinline]] void before_bind_texture_group(BindGroupRef ref, const wgpu::RenderPassEncoder& pass) {
  if (ref == g_currentTextureGroup) {
    return;
  }
  pass.SetBindGroup(2, find_bind_group(ref));
  g_currentTextureGroup = ref;
}
#include "after_lookup_bind.inc"

struct Fixtures {
 Device d;
 wgpu::Texture target;wgpu::TextureView view;
 wgpu::BindGroupLayout layout,emptyLayout;wgpu::PipelineLayout pipelineLayout;
 std::array<wgpu::Texture,2> sampled;std::array<wgpu::TextureView,2> sampledViews;wgpu::Sampler sampler;
 std::array<wgpu::RenderPipeline,2> pipelines;
 std::array<wgpu::BindGroup,2> groups;
 Fixtures(bool benchmark):d(benchmark){
  wgpu::TextureDescriptor td{};td.size={16,16,1};td.format=wgpu::TextureFormat::RGBA8Unorm;td.usage=wgpu::TextureUsage::RenderAttachment;
  target=d.device.CreateTexture(&td);view=target.CreateView();
  wgpu::BindGroupLayoutDescriptor gd{};emptyLayout=d.device.CreateBindGroupLayout(&gd);
  std::array<wgpu::BindGroupLayoutEntry,2> entries{};
  entries[0].binding=0;entries[0].visibility=wgpu::ShaderStage::Fragment;entries[0].sampler.type=wgpu::SamplerBindingType::Filtering;
  entries[1].binding=1;entries[1].visibility=wgpu::ShaderStage::Fragment;entries[1].texture.sampleType=wgpu::TextureSampleType::Float;entries[1].texture.viewDimension=wgpu::TextureViewDimension::e2D;
  gd.entryCount=entries.size();gd.entries=entries.data();layout=d.device.CreateBindGroupLayout(&gd);
  sampler=d.device.CreateSampler();
  std::array layouts{emptyLayout,emptyLayout,layout};wgpu::PipelineLayoutDescriptor ld{};ld.bindGroupLayoutCount=layouts.size();ld.bindGroupLayouts=layouts.data();pipelineLayout=d.device.CreatePipelineLayout(&ld);
  wgpu::ShaderSourceWGSL code{};code.code=R"(@group(2) @binding(0) var s:sampler; @group(2) @binding(1) var t:texture_2d<f32>; @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {return vec4f(f32(i),0,0,1);}@fragment fn fs()->@location(0) vec4f {return textureSample(t,s,vec2f(0.5,0.5));})";
  wgpu::ShaderModuleDescriptor smd{};smd.nextInChain=&code;auto shader=d.device.CreateShaderModule(&smd);
  for(unsigned i=0;i<2;++i){
   wgpu::ColorTargetState color{};color.format=td.format;
   wgpu::FragmentState fs{};fs.module=shader;fs.entryPoint="fs";fs.targetCount=1;fs.targets=&color;
   wgpu::RenderPipelineDescriptor pd{};pd.layout=pipelineLayout;pd.vertex.module=shader;pd.vertex.entryPoint="vs";pd.fragment=&fs;pd.primitive.cullMode=i?wgpu::CullMode::Back:wgpu::CullMode::None;
   pipelines[i]=d.device.CreateRenderPipeline(&pd);
   wgpu::TextureDescriptor texture{};texture.size={8,8,1};texture.format=td.format;texture.usage=wgpu::TextureUsage::TextureBinding;
   sampled[i]=d.device.CreateTexture(&texture);sampledViews[i]=sampled[i].CreateView();
   std::array<wgpu::BindGroupEntry,2> resources{};resources[0].binding=0;resources[0].sampler=sampler;resources[1].binding=1;resources[1].textureView=sampledViews[i];
   wgpu::BindGroupDescriptor bg{};bg.layout=layout;bg.entryCount=resources.size();bg.entries=resources.data();groups[i]=d.device.CreateBindGroup(&bg);
  }
  populate();
 }
 void populate(){
  for(unsigned i=1;i<=32;++i){g_pipelines[i]={pipelines[i%2],1000+i};g_cachedBindGroups[i]={groups[i%2],2000+i};}
 }
 ~Fixtures(){g_pipelines.clear();g_cachedBindGroups.clear();}
 wgpu::RenderPassEncoder begin(wgpu::CommandEncoder& encoder){
  wgpu::RenderPassColorAttachment a{};a.view=view;a.loadOp=wgpu::LoadOp::Clear;a.storeOp=wgpu::StoreOp::Store;
  wgpu::RenderPassDescriptor pd{};pd.colorAttachmentCount=1;pd.colorAttachments=&a;return encoder.BeginRenderPass(&pd);
 }
};
using Normalized=std::vector<std::array<uint64_t,6>>;
Normalized normalize(const wgpu::CommandBuffer& buffer){
 auto* iter=native::FromAPI(buffer.Get())->GetCommandIteratorForTesting();iter->Reset();
 Normalized out;native::Command type;
 while(iter->NextCommandId(&type)){
  switch(type){
   case native::Command::SetRenderPipeline:{auto* c=iter->NextCommand<native::SetRenderPipelineCmd>();out.push_back({uint64_t(type),uintptr_t(c->pipeline.Get())});break;}
   case native::Command::SetBindGroup:{auto*c=iter->NextCommand<native::SetBindGroupCmd>();out.push_back({uint64_t(type),uint64_t(c->index),uintptr_t(c->group.Get()),uint64_t(c->dynamicOffsetCount)});assert(c->dynamicOffsetCount==native::BindingIndex{0u});break;}
   case native::Command::Draw:{auto*c=iter->NextCommand<native::DrawCmd>();out.push_back({uint64_t(type),c->vertexCount,c->instanceCount,c->firstVertex,c->firstInstance});break;}
   default:native::SkipCommand(iter,type);break;
  }
 }
 iter->Reset();return out;
}
wgpu::CommandBuffer record(Fixtures& f,bool memo,const std::vector<unsigned>& sequence,bool custom=false){
 auto encoder=f.d.device.CreateCommandEncoder();auto pass=f.begin(encoder);
 g_currentPipeline=UINTPTR_MAX;g_currentTextureGroup=0;
 std::optional<ScopedPassObjectLookups> scope;if(memo)scope.emplace();
 for(unsigned at=0;at<sequence.size();++at){
  const unsigned key=sequence[at];
  bool good=memo?after_bind_pipeline(key,pass):before_bind_pipeline(key,pass);assert(good);
  if(memo)after_bind_texture_group(key,pass);else before_bind_texture_group(key,pass);
  pass.Draw(3);
  if(custom&&at==sequence.size()/2){
   if(memo){ScopedPassObjectLookupPause pause;assert(g_passObjectLookups==nullptr);after_bind_pipeline(2,pass);after_bind_texture_group(2,pass);}
   else {before_bind_pipeline(2,pass);before_bind_texture_group(2,pass);}
   if(memo)g_passObjectLookups->clear();
   g_currentPipeline=UINTPTR_MAX;g_currentTextureGroup=0;
  }
 }
 pass.End();return encoder.Finish();
}
void correctness(){
 for(unsigned cycle=0;cycle<12;++cycle){
  Fixtures f(false);
  static_assert(sizeof(PassObjectLookups)==2080);
  static_assert(!std::is_copy_constructible_v<ScopedPassObjectLookups>);
  static_assert(!std::is_copy_constructible_v<ScopedPassObjectLookupPause>);
  const auto pipeRefs=native::FromAPI(f.pipelines[0].Get())->GetRefCountForTesting();
  const auto groupRefs=native::FromAPI(f.groups[0].Get())->GetRefCountForTesting();
  std::mt19937 rng(0x108060+cycle);
  for(unsigned test=0;test<40;++test){
   std::vector<unsigned> sequence;
   for(unsigned draw=0;draw<200;++draw)sequence.push_back(1+rng()%32);
   auto before=record(f,false,sequence,test%2);auto after=record(f,true,sequence,test%2);
   assert(normalize(before)==normalize(after));assert(g_passObjectLookups==nullptr);
   const auto& bu=native::FromAPI(before.Get())->GetResourceUsages();
   const auto& au=native::FromAPI(after.Get())->GetResourceUsages();
   assert(bu.renderPasses.size()==native::PassIndex{1u}&&au.renderPasses.size()==native::PassIndex{1u});
   assert(bu.renderPasses[native::PassIndex{0u}].textures==au.renderPasses[native::PassIndex{0u}].textures);
   assert(bu.renderPasses[native::PassIndex{0u}].buffers==au.renderPasses[native::PassIndex{0u}].buffers);
   for(unsigned i=0;i<bu.renderPasses[native::PassIndex{0u}].textures.size();++i){
    const auto& b=bu.renderPasses[native::PassIndex{0u}].textureSyncInfos[i].Get(native::Aspect::Color,0,0);
    const auto& a=au.renderPasses[native::PassIndex{0u}].textureSyncInfos[i].Get(native::Aspect::Color,0,0);
    assert(b.usage==a.usage&&b.shaderStages==a.shaderStages);
   }
  }
  assert(native::FromAPI(f.pipelines[0].Get())->GetRefCountForTesting()==pipeRefs);
  assert(native::FromAPI(f.groups[0].Get())->GetRefCountForTesting()==groupRefs);
  // Nested pass scopes cannot strand the active memo pointer.
  {
   ScopedPassObjectLookups outer;const auto* previous=g_passObjectLookups;
   {ScopedPassObjectLookups inner;assert(g_passObjectLookups!=previous);}
   assert(g_passObjectLookups==previous);
  }
  assert(g_passObjectLookups==nullptr);
  // Failed pipeline lookup is not cached; late async publication remains visible.
  {
   ScopedPassObjectLookups scope;
   wgpu::RenderPipeline fallback;
   auto resolver=[](wgpu::RenderPipeline& p){return get_pipeline(999,p);};
   assert(!g_passObjectLookups->pipelines.find(999,fallback,resolver));
   g_pipelines[999]={f.pipelines[1],999};
   assert(g_passObjectLookups->pipelines.find(999,fallback,resolver)->Get()==f.pipelines[1].Get());
   // Clearing producer caches cannot destroy objects kept by this pass.
   auto* p=g_passObjectLookups->pipelines.find(999,fallback,resolver);
   g_pipelines.erase(999);assert(p->Get()==f.pipelines[1].Get());
   // A custom callback must bypass the memo, and its completion invalidates it.
   {ScopedPassObjectLookupPause pause;assert(!g_passObjectLookups);wgpu::RenderPipeline value;assert(!get_pipeline(999,value));}
   g_passObjectLookups->clear();assert(!g_passObjectLookups->pipelines.find(999,fallback,resolver));
  }
  // Collisions and every integer key, including zero/MAX, preserve exact ownership.
  {
   PassLookupMemo<uint64_t,wgpu::RenderPipeline,1> memo;
   wgpu::RenderPipeline fallback;
   for(uint64_t key:std::array<uint64_t,4>{0,1,UINT64_MAX,64}){
    auto* value=memo.find(key,fallback,[&](auto& p){p=f.pipelines[key%2];return true;});assert(value->Get()==f.pipelines[key%2].Get());
   }
   memo.clear();
   unsigned calls=0;auto resolve=[&](auto& p){++calls;p=f.pipelines[0];return true;};
   memo.find(5,fallback,resolve);memo.find(5,fallback,resolve);assert(calls==1);
  }
  assert(native::FromAPI(f.pipelines[0].Get())->GetRefCountForTesting()==pipeRefs);
  assert(native::FromAPI(f.groups[0].Get())->GetRefCountForTesting()==groupRefs);
  for(auto[key,value]:g_pipelines)assert(value.lastUsedFrame==1000+key);
  for(auto[key,value]:g_cachedBindGroups)assert(value.lastUsedFrame==2000+key);
 }
 printf("actual_dawn_draws=192000 same_command_stream=1 devices=12 owned_refs_return_to_baseline=1 bounded_bytes=2080 async_retry_custom_reset_collision=1\n");
}
void benchmark(){
 Fixtures f(true);
 for(unsigned scenario=0;scenario<5;++scenario){
  std::vector<unsigned> seq;for(unsigned i=0;i<(scenario==4?1u:1800u);++i)seq.push_back(scenario==0?1:scenario==1?1+i%2:scenario==2?1+i%32:scenario==3?1+i%128:1);
  if(scenario==3)for(unsigned i=33;i<=128;++i){g_pipelines[i]={f.pipelines[i%2],1000+i};g_cachedBindGroups[i]={f.groups[i%2],2000+i};}
  std::array<std::vector<double>,2> samples;std::array<uint64_t,2> hits{};
  for(unsigned repeat=0;repeat<100;++repeat){
   bool memo=repeat%4==1||repeat%4==2;pipelineLookups=groupLookups=0;
   const auto start=std::chrono::steady_clock::now();auto buffer=record(f,memo,seq);
   const auto finish=std::chrono::steady_clock::now();
   if(repeat>=20)samples[memo].push_back(std::chrono::duration<double,std::micro>(finish-start).count());
   hits[memo]=pipelineLookups+groupLookups;
  }
  for(auto&v:samples)std::sort(v.begin(),v.end());
  printf("{\"scenario\":\"%s\",\"before_median_us\":%.3f,\"after_median_us\":%.3f,\"before_p95_us\":%.3f,\"after_p95_us\":%.3f,\"before_lookups\":%llu,\"after_lookups\":%llu,\"samples_per_mode\":40}\n",
   std::array{"consecutive_one","alternating_two","alternating_32","collision_128","cold_one_draw"}[scenario],samples[0][20],samples[1][20],samples[0][38],samples[1][38],(unsigned long long)hits[0],(unsigned long long)hits[1]);
 }
}
int main(int argc,char**){if(argc>1)benchmark();else correctness();}
