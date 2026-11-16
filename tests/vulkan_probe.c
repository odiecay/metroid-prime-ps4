// SPDX-License-Identifier: MIT
// Capability queries and a real submit/fence/readback test. No capabilities are
// inferred from a device name or advertised Vulkan version.
#include <vkloader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef PRIME_HOST_FAULT_TEST
#include <orbis/libkernel.h>
#endif

static FILE *report;
static void json_string(const char *s) {
  fputc('"', report);
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') fprintf(report, "\\%c", c);
    else if (c < 32) fprintf(report, "\\u%04x", c);
    else fputc(c, report);
  }
  fputc('"', report);
}
static int result(const char *stage, VkResult r) {
  fprintf(report, "{\"stage\":"); json_string(stage);
  fprintf(report, ",\"vk_result\":%d}\n", r); fflush(report);
  return r == VK_SUCCESS;
}

// Returns 2 if resources still belong to an unfinished submission. The caller
// must not destroy the device or free/reuse those allocations in that case.
static int transfer_probe(VkDevice d, VkPhysicalDevice p, uint32_t family) {
  VkQueue queue;
  vkGetDeviceQueue(d, family, 0, &queue);
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  void *mapped = NULL;
  int passed = 0;
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = 4096, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  if (!result("create_buffer", vkCreateBuffer(d, &bi, NULL, &buffer))) goto done;
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(d, buffer, &requirements);
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(p, &mp);
  uint32_t type = UINT32_MAX;
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    if ((requirements.memoryTypeBits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { type = i; break; }
  if (type == UINT32_MAX) goto done;
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size, .memoryTypeIndex = type};
  if (!result("allocate_buffer", vkAllocateMemory(d, &ai, NULL, &memory))) goto done;
  if (!result("bind_buffer", vkBindBufferMemory(d, buffer, memory, 0))) goto done;
  if (!result("map_buffer", vkMapMemory(d, memory, 0, VK_WHOLE_SIZE, 0, &mapped))) goto done;
  VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
  if (!result("create_pool", vkCreateCommandPool(d, &pi, NULL, &pool))) goto done;
  VkCommandBufferAllocateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
  VkCommandBuffer cmd;
  if (!result("allocate_command", vkAllocateCommandBuffers(d, &ci, &cmd))) goto done;
  VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  if (!result("begin_command", vkBeginCommandBuffer(cmd, &begin))) goto done;
  vkCmdFillBuffer(cmd, buffer, 0, 4096, 0x1234abcd);
  VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = buffer, .offset = 0, .size = VK_WHOLE_SIZE};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
    0, 0, NULL, 1, &barrier, 0, NULL);
  if (!result("end_command", vkEndCommandBuffer(cmd))) goto done;
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (!result("create_fence", vkCreateFence(d, &fi, NULL, &fence))) goto done;
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .commandBufferCount = 1, .pCommandBuffers = &cmd};
  if (!result("submit", vkQueueSubmit(queue, 1, &submit, fence))) {
    // Device loss can make submission ownership uncertain even when the API
    // returns an error. Do not free possible live resources on this path.
    fprintf(report, "{\"resources_quarantined\":true,\"reason\":\"submission failed; ownership unconfirmed\"}\n");
    fflush(report);
    return 2;
  }
  if (!result("wait_fence_5s", vkWaitForFences(d, 1, &fence, VK_TRUE, 5000000000ull))) {
    fprintf(report, "{\"resources_quarantined\":true,\"reason\":\"submission completion unconfirmed\"}\n");
    fflush(report);
    return 2;
  }
  if (!(mp.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = memory, .offset = 0, .size = VK_WHOLE_SIZE};
    if (!result("invalidate_buffer", vkInvalidateMappedMemoryRanges(d, 1, &range))) goto done;
  }
  passed = 1;
  for (unsigned i = 0; i < 1024; ++i)
    if (((const uint32_t *)mapped)[i] != 0x1234abcd) { passed = 0; break; }
  fprintf(report, "{\"stage\":\"transfer_readback\",\"passed\":%s,\"bytes\":4096}\n", passed ? "true" : "false");
done:
  if (fence) vkDestroyFence(d, fence, NULL);
  if (pool) vkDestroyCommandPool(d, pool, NULL);
  if (mapped) vkUnmapMemory(d, memory);
  if (buffer) vkDestroyBuffer(d, buffer, NULL);
  if (memory) vkFreeMemory(d, memory, NULL);
  return passed ? 0 : 1;
}

#ifndef PRIME_HOST_FAULT_TEST
int main(void) {
  mkdir("/data/project-prime", 0700);
  report = fopen("/data/project-prime/vulkan.jsonl", "w");
  if (!report) { sceKernelDebugOutText(0, "Project Prime: cannot open Vulkan report\n"); return 1; }
  fprintf(report, "{\"schema\":1,\"target\":\"orbis\",\"probe\":\"vulkan\",\"aurora_rendered\":false}\n"); fflush(report);
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  int exit_code = 1;
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "Project Prime Capability Probe", .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
  if (!result("create_instance", vkCreateInstance(&ii, NULL, &instance))) goto done;
  VkExtensionProperties exts[512]; uint32_t count = 512;
  if (!result("instance_extensions", vkEnumerateInstanceExtensionProperties(NULL, &count, exts))) goto done;
  fprintf(report, "{\"instance_extensions\":[");
  for (uint32_t i = 0; i < count; ++i) { if (i) fputc(',', report); json_string(exts[i].extensionName); }
  fprintf(report, "]}\n"); fflush(report);
  VkPhysicalDevice devices[16]; count = 16;
  if (!result("enumerate_devices", vkEnumeratePhysicalDevices(instance, &count, devices)) || !count) goto done;
  VkPhysicalDevice physical = devices[0];
  VkPhysicalDeviceProperties props; VkPhysicalDeviceFeatures features;
  vkGetPhysicalDeviceProperties(physical, &props);
  vkGetPhysicalDeviceFeatures(physical, &features);
  fprintf(report, "{\"device_name\":"); json_string(props.deviceName);
  fprintf(report, ",\"vendor_id\":%u,\"device_id\":%u,\"api_version\":%u,\"features\":{", props.vendorID, props.deviceID, props.apiVersion);
#define FEATURE(name) fprintf(report, "\"" #name "\":%s,", features.name ? "true" : "false")
  FEATURE(robustBufferAccess); FEATURE(textureCompressionBC); FEATURE(textureCompressionETC2);
  FEATURE(textureCompressionASTC_LDR); FEATURE(depthBiasClamp); FEATURE(fragmentStoresAndAtomics);
  FEATURE(fullDrawIndexUint32); FEATURE(imageCubeArray); FEATURE(independentBlend);
  FEATURE(sampleRateShading); FEATURE(samplerAnisotropy); FEATURE(dualSrcBlend);
  FEATURE(shaderUniformBufferArrayDynamicIndexing); FEATURE(shaderStorageBufferArrayDynamicIndexing);
  FEATURE(shaderSampledImageArrayDynamicIndexing);
  fprintf(report, "\"shaderStorageImageArrayDynamicIndexing\":%s},", features.shaderStorageImageArrayDynamicIndexing ? "true" : "false");
  fprintf(report, "\"limits\":{\"maxImageDimension2D\":%u,\"maxPerStageDescriptorSampledImages\":%u,\"maxPerStageDescriptorStorageBuffers\":%u,\"maxPushConstantsSize\":%u,\"minUniformBufferOffsetAlignment\":%llu,\"minStorageBufferOffsetAlignment\":%llu,\"maxSamplerAnisotropy\":%g}}\n",
    props.limits.maxImageDimension2D, props.limits.maxPerStageDescriptorSampledImages,
    props.limits.maxPerStageDescriptorStorageBuffers, props.limits.maxPushConstantsSize,
    (unsigned long long)props.limits.minUniformBufferOffsetAlignment,
    (unsigned long long)props.limits.minStorageBufferOffsetAlignment, props.limits.maxSamplerAnisotropy);
  count = 512;
  if (!result("device_extensions", vkEnumerateDeviceExtensionProperties(physical, NULL, &count, exts))) goto done;
  fprintf(report, "{\"device_extensions\":[");
  for (uint32_t i = 0; i < count; ++i) { if (i) fputc(',', report); json_string(exts[i].extensionName); }
  fprintf(report, "]}\n");
  const VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_R32_UINT, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
    VK_FORMAT_BC3_UNORM_BLOCK};
  for (unsigned i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) {
    VkFormatProperties f; vkGetPhysicalDeviceFormatProperties(physical, formats[i], &f);
    fprintf(report, "{\"format\":%d,\"optimal_features\":%u,\"linear_features\":%u,\"buffer_features\":%u}\n",
      formats[i], f.optimalTilingFeatures, f.linearTilingFeatures, f.bufferFeatures);
  }
  uint32_t queues = 32; VkQueueFamilyProperties qp[32];
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &queues, qp);
  uint32_t family = UINT32_MAX;
  for (uint32_t i = 0; i < queues; ++i) {
    fprintf(report, "{\"queue_family\":%u,\"flags\":%u,\"count\":%u,\"timestamp_bits\":%u}\n", i, qp[i].queueFlags, qp[i].queueCount, qp[i].timestampValidBits);
    if (family == UINT32_MAX && qp[i].queueCount && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family = i;
  }
  if (family == UINT32_MAX) goto done;
  float priority = 1.0f;
  VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
  VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi};
  if (!result("create_device", vkCreateDevice(physical, &di, NULL, &device))) goto done;
  exit_code = transfer_probe(device, physical, family);
done:
  // A timeout is not completion. Keep the allocation and device intact until
  // native process teardown instead of returning live memory to the allocator.
  if (exit_code != 2) {
    if (device) vkDestroyDevice(device, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
  }
  fprintf(report, "{\"stage\":\"probe_end\",\"passed\":%s,\"presentation_tested\":false,\"aurora_rendered\":false}\n", exit_code == 0 ? "true" : "false");
  fflush(report); fclose(report);
  sceKernelDebugOutText(0, exit_code ? "Project Prime Vulkan probe FAIL\n" : "Project Prime Vulkan probe PASS (no presentation)\n");
  return exit_code;
}
#endif
