// SPDX-License-Identifier: MIT
// Real Dawn/Vulkan diagnostic. No game assets or simulated graphics results.
#include <webgpu/webgpu_cpp.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

namespace {
FILE *report;
std::mutex log_lock;
std::atomic<unsigned> errors{};

void event(const char *stage, bool passed) {
    std::lock_guard guard(log_lock);
    std::fprintf(report,"{\"stage\":\"%s\",\"passed\":%s}\n",stage,passed?"true":"false");
    std::fflush(report);
}

void error_message(wgpu::StringView message) {
    std::lock_guard guard(log_lock);
    std::fprintf(report,"{\"error\":\"");
    const size_t length=!message.data ? 0 : message.length==WGPU_STRLEN ? std::strlen(message.data) : message.length;
    for (size_t i=0; message.data && i<length; ++i) {
        const unsigned char c=message.data[i];
        if (c=='"' || c=='\\') std::fprintf(report,"\\%c",c);
        else if (c<32) std::fprintf(report,"\\u%04x",c);
        else std::fputc(c,report);
    }
    std::fprintf(report,"\"}\n");std::fflush(report);
}

void wait(const wgpu::Instance &instance,wgpu::Future future,const char *stage) {
    const auto status=instance.WaitAny(future,5'000'000'000ull);
    event(stage,status==wgpu::WaitStatus::Success);
    if (status!=wgpu::WaitStatus::Success) {
        // Neither GPU resources nor callback userdata may be released while
        // completion is unconfirmed. Let native process teardown reclaim them.
        event("resources_quarantined_completion_unconfirmed",true);
        sceKernelDebugOutText(0,"Project Prime Dawn probe: operation did not complete; resources quarantined\n");
        std::_Exit(2);
    }
}

bool complete(const wgpu::Instance &instance,const wgpu::Queue &queue) {
    bool okay=false;
    auto future=queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::QueueWorkDoneStatus status,wgpu::StringView message,bool *result) {
            *result=status==wgpu::QueueWorkDoneStatus::Success;
            if (!*result) error_message(message);
        },&okay);
    wait(instance,future,"gpu_completion_callback");
    if (!okay) { event("resources_quarantined_gpu_error",true);std::_Exit(2); }
    return okay;
}

wgpu::RenderPipeline pipeline(const wgpu::Instance &instance,const wgpu::Device &device,
                              const wgpu::ShaderModule &shader,wgpu::TextureFormat format) {
    wgpu::ColorTargetState target{.format=format};
    wgpu::FragmentState fragment{.module=shader,.entryPoint="fs",.targetCount=1,.targets=&target};
    wgpu::RenderPipelineDescriptor descriptor{
        .vertex={.module=shader,.entryPoint="vs"},.fragment=&fragment};
    wgpu::RenderPipeline result;
    const auto future=device.CreateRenderPipelineAsync(&descriptor,wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::CreatePipelineAsyncStatus status,wgpu::RenderPipeline value,wgpu::StringView message,
           wgpu::RenderPipeline *out) {
            if (status==wgpu::CreatePipelineAsyncStatus::Success) *out=std::move(value);
            else error_message(message);
        },&result);
    wait(instance,future,"wgsl_pipeline_compilation");
    return result;
}

wgpu::CommandEncoder draw(const wgpu::Device &device,const wgpu::TextureView &view,
                          const wgpu::RenderPipeline &pipeline) {
    auto encoder=device.CreateCommandEncoder();
    const wgpu::RenderPassColorAttachment color{
        .view=view,.loadOp=wgpu::LoadOp::Clear,.storeOp=wgpu::StoreOp::Store,
        .clearValue={0,0,0,1}};
    const wgpu::RenderPassDescriptor pass_desc{.colorAttachmentCount=1,.colorAttachments=&color};
    auto pass=encoder.BeginRenderPass(&pass_desc);
    pass.SetPipeline(pipeline);pass.Draw(3);pass.End();
    return encoder;
}

int run() {
    const wgpu::InstanceFeatureName required=wgpu::InstanceFeatureName::TimedWaitAny;
    const wgpu::InstanceDescriptor instance_desc{.requiredFeatureCount=1,.requiredFeatures=&required};
    auto instance=wgpu::CreateInstance(&instance_desc);
    event("create_dawn_instance",bool(instance));if (!instance) return 1;
    wgpu::SurfaceSourceOrbis orbis;
    const wgpu::SurfaceDescriptor surface_desc{.nextInChain=&orbis};
    auto surface=instance.CreateSurface(&surface_desc);
    event("create_native_orbis_surface",bool(surface));if (!surface) return 1;
    const wgpu::RequestAdapterOptions options{.backendType=wgpu::BackendType::Vulkan,.compatibleSurface=surface};
    wgpu::Adapter adapter;
    auto requested=instance.RequestAdapter(&options,wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::RequestAdapterStatus status,wgpu::Adapter value,wgpu::StringView message,wgpu::Adapter *out) {
            if (status==wgpu::RequestAdapterStatus::Success) *out=std::move(value);
            else error_message(message);
        },&adapter);
    wait(instance,requested,"request_native_vulkan_adapter");if (!adapter) return 1;
    wgpu::AdapterInfo info;
    if (!adapter.GetInfo(&info) || info.backendType!=wgpu::BackendType::Vulkan ||
        info.adapterType==wgpu::AdapterType::CPU || info.vendorID!=0x1002) {
        event("native_amd_vulkan_adapter",false);return 1;
    }
    event("native_amd_vulkan_adapter",true);
    wgpu::DeviceDescriptor descriptor;
    descriptor.SetUncapturedErrorCallback([](const wgpu::Device &,wgpu::ErrorType,wgpu::StringView message) {
        errors.fetch_add(1);error_message(message);
    });
    descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device &,wgpu::DeviceLostReason reason,wgpu::StringView message) {
            if (reason!=wgpu::DeviceLostReason::Destroyed) { errors.fetch_add(1);error_message(message); }
        });
    wgpu::Device device;
    auto device_future=adapter.RequestDevice(&descriptor,wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::RequestDeviceStatus status,wgpu::Device value,wgpu::StringView message,wgpu::Device *out) {
            if (status==wgpu::RequestDeviceStatus::Success) *out=std::move(value);
            else error_message(message);
        },&device);
    wait(instance,device_future,"create_dawn_device");if (!device) return 1;
    auto queue=device.GetQueue();
    wgpu::ShaderSourceWGSL code;
    code.code=R"(
@vertex fn vs(@builtin(vertex_index) index:u32)->@builtin(position) vec4f {
    let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
    return vec4f(p[index],0,1);
}
@fragment fn fs()->@location(0) vec4f { return vec4f(0.25,0.5,0.75,1); }
)";
    const wgpu::ShaderModuleDescriptor shader_desc{.nextInChain=&code};
    auto shader=device.CreateShaderModule(&shader_desc);
    auto offscreen_pipeline=pipeline(instance,device,shader,wgpu::TextureFormat::RGBA8Unorm);
    if (!offscreen_pipeline) return 1;
    const wgpu::TextureDescriptor texture_desc{
        .usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::CopySrc,
        .size={32,32,1},.format=wgpu::TextureFormat::RGBA8Unorm};
    auto texture=device.CreateTexture(&texture_desc);
    const wgpu::BufferDescriptor buffer_desc{
        .usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead,.size=256*32};
    auto buffer=device.CreateBuffer(&buffer_desc);
    auto encoder=draw(device,texture.CreateView(),offscreen_pipeline);
    const wgpu::TexelCopyTextureInfo from{.texture=texture};
    const wgpu::TexelCopyBufferInfo to{.layout={.bytesPerRow=256,.rowsPerImage=32},.buffer=buffer};
    const wgpu::Extent3D extent{32,32,1};
    encoder.CopyTextureToBuffer(&from,&to,&extent);
    auto commands=encoder.Finish();queue.Submit(1,&commands);
    complete(instance,queue);
    bool mapped=false;
    auto mapping=buffer.MapAsync(wgpu::MapMode::Read,0,256*32,wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::MapAsyncStatus status,wgpu::StringView message,bool *out) {
            *out=status==wgpu::MapAsyncStatus::Success;if (!*out) error_message(message);
        },&mapped);
    wait(instance,mapping,"readback_map");if (!mapped) return 1;
    const auto *bytes=static_cast<const unsigned char *>(buffer.GetConstMappedRange());
    bool pixels=bytes!=nullptr;
    const int expected[4]={64,128,191,255};
    for (int y=0;pixels && y<32;++y) for (int x=0;x<32;++x) for (int c=0;c<4;++c)
        if (std::abs(int(bytes[y*256+x*4+c])-expected[c])>1) pixels=false;
    buffer.Unmap();event("wgsl_render_and_1024_pixel_readback",pixels);
    if (!pixels || errors.load()) return 1;

    wgpu::SurfaceCapabilities caps;
    if (!surface.GetCapabilities(adapter,&caps)) return 1;
    wgpu::TextureFormat format=wgpu::TextureFormat::Undefined;
    for (size_t i=0;i<caps.formatCount;++i)
        if (caps.formats[i]==wgpu::TextureFormat::BGRA8Unorm) format=caps.formats[i];
    bool fifo=false,opaque=false;
    for (size_t i=0;i<caps.presentModeCount;++i) fifo|=caps.presentModes[i]==wgpu::PresentMode::Fifo;
    for (size_t i=0;i<caps.alphaModeCount;++i) opaque|=caps.alphaModes[i]==wgpu::CompositeAlphaMode::Opaque;
    if (format==wgpu::TextureFormat::Undefined || !fifo || !opaque) {
        event("native_surface_configuration_supported",false);return 1;
    }
    auto screen_pipeline=pipeline(instance,device,shader,format);if (!screen_pipeline) return 1;
    const wgpu::SurfaceConfiguration config{.device=device,.format=format,
        .usage=wgpu::TextureUsage::RenderAttachment,.width=1920,.height=1080,
        .alphaMode=wgpu::CompositeAlphaMode::Opaque,.presentMode=wgpu::PresentMode::Fifo};
    surface.Configure(&config);
    for (unsigned frame=0;frame<180;++frame) {
        const uint64_t start=sceKernelGetProcessTime();
        wgpu::SurfaceTexture acquired;
        surface.GetCurrentTexture(&acquired);
        if ((acquired.status!=wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
             acquired.status!=wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) || !acquired.texture) {
            complete(instance,queue);event("acquire_surface_texture",false);return 1;
        }
        auto frame_encoder=draw(device,acquired.texture.CreateView(),screen_pipeline);
        auto frame_commands=frame_encoder.Finish();queue.Submit(1,&frame_commands);
        // A successful Present queues a flip; it does not prove display completion.
        const bool presented=bool(surface.Present());
        complete(instance,queue);
        {
            std::lock_guard guard(log_lock);
            std::fprintf(report,"{\"frame\":%u,\"gpu_completed\":true,\"present_queued\":%s,\"display_completion_verified\":false,\"cpu_elapsed_us\":%llu}\n",
                frame,presented?"true":"false",(unsigned long long)(sceKernelGetProcessTime()-start));
            std::fflush(report);
        }
        if (!presented || errors.load()) return 1;
        if (frame==0) sceSystemServiceHideSplashScreen();
    }
    // The matched native WSI is responsible for retaining its scanned-out and
    // queued images during this call. Hardware acceptance must inspect its logs.
    surface.Unconfigure();device.Destroy();instance.ProcessEvents();
    event("dawn_probe_end",errors.load()==0);
    return errors.load() ? 1 : 0;
}
}

int main() {
    mkdir("/data/project-prime",0700);
    report=std::fopen("/data/project-prime/dawn.jsonl","w");if (!report) return 1;
    std::fprintf(report,"{\"schema\":1,\"target\":\"orbis\",\"probe\":\"dawn\",\"aurora_rendered\":false,\"gameplay_verified\":false}\n");
    std::fflush(report);
    const int status=run();
    event("process_end",status==0);
    // Keep the stream open for any final asynchronous driver/error callback.
    std::fflush(report);
    return status;
}
