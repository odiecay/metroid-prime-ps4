// SPDX-License-Identifier: MIT
// Built with the pinned Dawn archive's exact native compiler/include definitions.
#include "dawn/native/DawnNative.h"
#include "src/dawn/native/Device.h"
#include "src/dawn/native/ErrorData.h"
#include "src/dawn/native/PhysicalDevice.h"
#include "src/dawn/native/vulkan/QueueVk.h"
#include <cstdio>
#include "src/dawn/native/vulkan/DeviceVk.h"
#include "../../renderer/orbis/timestamp_capabilities.h"

extern "C" bool prime_ps4_wait_for_gpu(WGPUDevice handle,uint64_t timeoutNs) {
    using namespace dawn::native;
    auto *device=FromAPI(handle);
    auto guard=device->GetGuard();
    if(device->IsLost() || device->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::Vulkan)
        return false;
    auto *queue=static_cast<vulkan::Queue *>(device->GetQueue());
    auto submit=queue->SubmitPendingCommands();
    if(submit.IsError()) {
        auto error=submit.AcquireError();
        std::fprintf(stderr,"orbis: GPU pressure submission failed: %s\n",error->GetFormattedMessage().c_str());
        return false;
    }
    const auto serial=queue->GetLastSubmittedCommandSerial();
    auto wait=queue->WaitForQueueSerial(serial,dawn::Nanoseconds(timeoutNs));
    if(wait.IsError()) {
        auto error=wait.AcquireError();
        std::fprintf(stderr,"orbis: GPU pressure wait failed: %s\n",error->GetFormattedMessage().c_str());
        return false;
    }
    return queue->GetCompletedCommandSerial()>=serial;
}

extern "C" bool prime_ps4_timestamp_caps(void* handle,PrimeTimestampCaps* out) {
    using namespace dawn::native;
    if(!handle || !out) return false;
    auto* base=FromAPI(static_cast<WGPUDevice>(handle));
    auto guard=base->GetGuard();
    if(base->IsLost() || base->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::Vulkan) return false;
    auto* device=static_cast<vulkan::Device*>(base);
    const auto family=device->GetGraphicsQueueFamily();
    const auto& info=device->GetDeviceInfo();
    if(family>=info.queueFamilies.size()) return false;
    *out={family,info.queueFamilies[family].timestampValidBits,
          uint32_t(info.properties.limits.timestampComputeAndGraphics),info.properties.limits.timestampPeriod};
    return true;
}
