// Offline proof on the existing real Dawn Null backend. No GPU/performance claim.
#include <webgpu/webgpu_cpp.h>
#include "../game/MetroidPrimePort/extern/aurora/lib/gfx/dawn_call_profile.hpp"
#include "src/dawn/native/RenderPipeline.h"
#include "src/dawn/native/BindGroup.h"
#include "src/dawn/native/CommandBuffer.h"
#include "src/dawn/native/RenderBundle.h"
#include "src/dawn/native/Commands.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define REQUIRE(c) do{if(!(c)){std::fprintf(stderr,"bundle proof failed %s:%d: %s\n",__FILE__,__LINE__,#c);std::abort();}}while(false)
namespace native=dawn::native;
using Bytes=std::vector<uint64_t>;
static unsigned errors=0;
static void flatten(native::CommandIterator* it,Bytes& out){
 it->Reset();native::Command type;
 while(it->NextCommandId(&type)){
  if(type==native::Command::BeginRenderPass||type==native::Command::EndRenderPass){native::SkipCommand(it,type);continue;}
  if(type==native::Command::ExecuteBundles){auto* c=it->NextCommand<native::ExecuteBundlesCmd>();auto bundles=it->NextData<dawn::Ref<native::RenderBundleBase>>(c->count);for(const auto& b:bundles)flatten(b->GetCommands(),out);continue;}
  out.push_back(uint32_t(type));
  switch(type){
   case native::Command::SetViewport:{auto* c=it->NextCommand<native::SetViewportCmd>();for(float v:{c->x,c->y,c->width,c->height,c->minDepth,c->maxDepth}){uint32_t bits;std::memcpy(&bits,&v,4);out.push_back(bits);}break;}
   case native::Command::SetScissorRect:{auto* c=it->NextCommand<native::SetScissorRectCmd>();out.push_back(c->x);out.push_back(c->y);out.push_back(c->width);out.push_back(c->height);break;}
   case native::Command::SetRenderPipeline:{auto* c=it->NextCommand<native::SetRenderPipelineCmd>();out.push_back(uintptr_t(c->pipeline.Get()));break;}
   case native::Command::SetBindGroup:{auto* c=it->NextCommand<native::SetBindGroupCmd>();out.push_back(uint32_t(c->index));out.push_back(uintptr_t(c->group.Get()));out.push_back(uint32_t(c->dynamicOffsetCount));if(c->dynamicOffsetCount!=native::BindingIndex{0u})for(auto off:it->NextData<uint32_t>(c->dynamicOffsetCount))out.push_back(off);break;}
   case native::Command::SetIndexBuffer:{auto* c=it->NextCommand<native::SetIndexBufferCmd>();out.push_back(uintptr_t(c->buffer.Get()));out.push_back(uint32_t(c->format));out.push_back(c->offset);out.push_back(c->size);break;}
   case native::Command::SetImmediates:{auto* c=it->NextCommand<native::SetImmediatesCmd>();out.push_back(c->offset);out.push_back(c->size);const auto b=it->NextData<uint8_t>(c->size);for(auto byte:b)out.push_back(byte);break;}
   case native::Command::Draw:{auto* c=it->NextCommand<native::DrawCmd>();out.push_back(c->vertexCount);out.push_back(c->instanceCount);out.push_back(c->firstVertex);out.push_back(c->firstInstance);break;}
   case native::Command::DrawIndexed:{auto* c=it->NextCommand<native::DrawIndexedCmd>();out.push_back(c->indexCount);out.push_back(c->instanceCount);out.push_back(c->firstIndex);out.push_back(uint32_t(c->baseVertex));out.push_back(c->firstInstance);break;}
   default:std::fprintf(stderr,"unexpected command%u\n",uint32_t(type));std::abort();
  }
 }
 it->Reset();
}
namespace profile=prime::dawn_call_profile;
namespace frontend=profile::frontend;
static std::array<uint64_t,frontend::CategoryCount> counts{};
static unsigned batches=0;
extern "C" void prime_ps4_dawn_frontend_stats(const PrimeDawnCallStats* s,unsigned n){
 REQUIRE(n==frontend::CategoryCount&&frontend::currentBatch==nullptr);++batches;
 for(unsigned i=0;i<n;++i)counts[i]+=s[i].calls;
}
int main(){
 setenv("MP_DAWN_CALL_PROFILE","1",1);
 const std::array features{wgpu::InstanceFeatureName::TimedWaitAny};
 const wgpu::InstanceDescriptor id{.requiredFeatureCount=features.size(),.requiredFeatures=features.data()};
 auto instance=wgpu::CreateInstance(&id);REQUIRE(instance);
 const wgpu::RequestAdapterOptions ao{.featureLevel=wgpu::FeatureLevel::Core,.backendType=wgpu::BackendType::Null};
 wgpu::Adapter adapter;instance.WaitAny(instance.RequestAdapter(&ao,wgpu::CallbackMode::WaitAnyOnly,[&](wgpu::RequestAdapterStatus s,wgpu::Adapter a,wgpu::StringView){if(s==wgpu::RequestAdapterStatus::Success)adapter=std::move(a);}),5000000000);REQUIRE(adapter);
 wgpu::DeviceDescriptor dd{};dd.SetUncapturedErrorCallback([](const wgpu::Device&,wgpu::ErrorType,wgpu::StringView m){++errors;std::fprintf(stderr,"Dawn:%.*s\n",int(m.length),m.data);});wgpu::Device device;
 instance.WaitAny(adapter.RequestDevice(&dd,wgpu::CallbackMode::WaitAnyOnly,[&](wgpu::RequestDeviceStatus s,wgpu::Device d,wgpu::StringView){if(s==wgpu::RequestDeviceStatus::Success)device=std::move(d);}),5000000000);REQUIRE(device);
 const auto scoped_error=[&](auto&& fn){device.PushErrorScope(wgpu::ErrorFilter::Validation);fn();bool error=false;instance.WaitAny(device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,[&](wgpu::PopErrorScopeStatus,wgpu::ErrorType t,wgpu::StringView){error=t!=wgpu::ErrorType::NoError;}),5000000000);return error;};
 const char* code="@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f { let p=array<vec2f,3>(vec2f(-1,-1),vec2f(1,-1),vec2f(0,1));return vec4f(p[i%3],0,1); } @fragment fn fs()->@location(0) vec4f{return vec4f(1,0,0,1);}";
 wgpu::ShaderSourceWGSL wgsl{};wgsl.code=code;const wgpu::ShaderModuleDescriptor sd{.nextInChain=&wgsl};auto shader=device.CreateShaderModule(&sd);
 const wgpu::BindGroupLayoutEntry le{.binding=0,.visibility=wgpu::ShaderStage::Vertex,.buffer={.type=wgpu::BufferBindingType::Uniform,.hasDynamicOffset=true,.minBindingSize=16}};
 const wgpu::BindGroupLayoutDescriptor ld{.entryCount=1,.entries=&le};auto layout=device.CreateBindGroupLayout(&ld);
 const wgpu::PipelineLayoutDescriptor pld{.bindGroupLayoutCount=1,.bindGroupLayouts=&layout,.immediateSize=64};auto pipelineLayout=device.CreatePipelineLayout(&pld);
 const wgpu::ColorTargetState color{.format=wgpu::TextureFormat::RGBA8Unorm};const wgpu::FragmentState fs{.module=shader,.entryPoint="fs",.targetCount=1,.targets=&color};
 const wgpu::RenderPipelineDescriptor pd{.layout=pipelineLayout,.vertex={.module=shader,.entryPoint="vs"},.primitive={.topology=wgpu::PrimitiveTopology::TriangleList},.fragment=&fs};auto pipeline=device.CreateRenderPipeline(&pd);REQUIRE(pipeline);
 const wgpu::BufferDescriptor ub{.usage=wgpu::BufferUsage::Uniform|wgpu::BufferUsage::CopyDst,.size=1024};auto uniforms=device.CreateBuffer(&ub);
 const wgpu::BufferDescriptor ib{.usage=wgpu::BufferUsage::Index|wgpu::BufferUsage::CopyDst,.size=1024};auto indices=device.CreateBuffer(&ib);
 const wgpu::BindGroupEntry ge{.binding=0,.buffer=uniforms,.offset=0,.size=16};const wgpu::BindGroupDescriptor gd{.layout=layout,.entryCount=1,.entries=&ge};auto group=device.CreateBindGroup(&gd);
 const wgpu::TextureDescriptor td{.usage=wgpu::TextureUsage::RenderAttachment,.size={64,64,1},.format=wgpu::TextureFormat::RGBA8Unorm};auto texture=device.CreateTexture(&td);auto view=texture.CreateView();

 const wgpu::RenderPassColorAttachment ca{.view=view,.loadOp=wgpu::LoadOp::Clear,.storeOp=wgpu::StoreOp::Store};const wgpu::RenderPassDescriptor rp{.colorAttachmentCount=1,.colorAttachments=&ca};
 const auto baselinePipelineRefs=native::FromAPI(pipeline.Get())->GetRefCountForTesting();
 const auto baselineGroupRefs=native::FromAPI(group.Get())->GetRefCountForTesting();
 for(unsigned frame=0;frame<64;++frame){
  Bytes expected;
  for(bool enabled:{false,true}){
   auto encoder=device.CreateCommandEncoder();auto pass=encoder.BeginRenderPass(&rp);
   {
    frontend::Batch batch(enabled);
    AURORA_DAWN_API(Pipeline,pass.SetPipeline(pipeline));
    pass.SetIndexBuffer(indices,wgpu::IndexFormat::Uint16,0,indices.GetSize());
    for(unsigned i=0;i<32;++i){
     const uint32_t offset=(i%3)*256;std::array<uint32_t,16> imm{};imm[0]=i*32;imm[7]=i%5;
     AURORA_DAWN_API(BindGroup,pass.SetBindGroup(0,group,1,&offset));
     AURORA_DAWN_API(Immediates,pass.SetImmediates(0,imm.data(),sizeof(imm)));
     if(i&1)AURORA_DAWN_API(Draw,pass.Draw(3,1,i%2,0));
     else AURORA_DAWN_API(DrawIndexed,pass.DrawIndexed(6,1,i*6,0,0));
    }
   }
   pass.End();
   auto command=enabled?AURORA_DAWN_API_FINISH(encoder.Finish()):encoder.Finish();
   REQUIRE(frontend::currentBatch==nullptr&&profile::currentBatch==nullptr);
   Bytes actual;flatten(native::FromAPI(command.Get())->GetCommandIteratorForTesting(),actual);
   if(enabled)REQUIRE(actual==expected);else expected=actual;
   const auto before=batches;device.GetQueue().Submit(1,&command);REQUIRE(batches==before);
  }
  REQUIRE(native::FromAPI(pipeline.Get())->GetRefCountForTesting()==baselinePipelineRefs);
  REQUIRE(native::FromAPI(group.Get())->GetRefCountForTesting()==baselineGroupRefs);
 }
 const std::array<uint64_t,6> expectedCounts{64,2048,2048,1024,1024,64};REQUIRE(counts==expectedCounts);
 REQUIRE(scoped_error([&]{auto cmd=device.CreateCommandEncoder();auto p=cmd.BeginRenderPass(&rp);{frontend::Batch b;AURORA_DAWN_API(Immediates,p.SetImmediates(0,nullptr,1));}p.End();(void)AURORA_DAWN_API_FINISH(cmd.Finish());}));
 REQUIRE(frontend::currentBatch==nullptr&&profile::currentBatch==nullptr&&errors==0);
 std::puts("actual_dawn_null_draws=4096 complete_command_stream_equal=1 dynamic_uniform_immediate_bytes=1 owned_refs_unchanged=1 finish_before_submit=1 validation_error_restore=1");
}
