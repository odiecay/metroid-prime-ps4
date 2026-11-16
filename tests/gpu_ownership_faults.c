// SPDX-License-Identifier: MIT
// Host-only fault injection for the actual native probe's cleanup policy.
// These mock calls are never part of a console target or graphics evidence.
#define PRIME_HOST_FAULT_TEST
#include "vulkan_probe.c"
#include <assert.h>

static uint32_t contents[1024];
static VkResult submit_result,wait_result,invalidate_result,allocate_result;
static unsigned destroyed,invalidations;

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice d,uint32_t f,uint32_t i,VkQueue *q) { *q=(VkQueue)1; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d,const VkBufferCreateInfo *i,const VkAllocationCallbacks *a,VkBuffer *b) { *b=(VkBuffer)1;return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d,VkBuffer b,VkMemoryRequirements *r) { *r=(VkMemoryRequirements){.size=4096,.alignment=64,.memoryTypeBits=1}; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *m) { *m=(VkPhysicalDeviceMemoryProperties){.memoryTypeCount=1};m->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d,const VkMemoryAllocateInfo *i,const VkAllocationCallbacks *a,VkDeviceMemory *m) { if (allocate_result==VK_SUCCESS) *m=(VkDeviceMemory)1;return allocate_result; }
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice d,VkDeviceMemory m,VkDeviceSize o,VkDeviceSize s,VkMemoryMapFlags f,void **out) { *out=contents;return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice d,const VkCommandPoolCreateInfo *i,const VkAllocationCallbacks *a,VkCommandPool *p) { *p=(VkCommandPool)1;return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice d,const VkCommandBufferAllocateInfo *i,VkCommandBuffer *b) { *b=(VkCommandBuffer)1;return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer b,const VkCommandBufferBeginInfo *i) { return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkDeviceSize s,uint32_t v) { for (unsigned i=0;i<1024;++i) contents[i]=v; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags s,VkPipelineStageFlags d,VkDependencyFlags f,uint32_t n,const VkMemoryBarrier *m,uint32_t bn,const VkBufferMemoryBarrier *b,uint32_t in,const VkImageMemoryBarrier *im) { }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice d,const VkFenceCreateInfo *i,const VkAllocationCallbacks *a,VkFence *f) { *f=(VkFence)1;return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo *i,VkFence f) { return submit_result; }
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t n,const VkFence *f,VkBool32 all,uint64_t timeout) { assert(timeout==5000000000ull);return wait_result; }
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d,uint32_t n,const VkMappedMemoryRange *r) { ++invalidations;return invalidate_result; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice d,VkFence f,const VkAllocationCallbacks *a) { ++destroyed; }
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice d,VkCommandPool p,const VkAllocationCallbacks *a) { ++destroyed; }
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice d,VkDeviceMemory m) { ++destroyed; }
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks *a) { ++destroyed; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a) { ++destroyed; }

static void reset(void) {
    submit_result=wait_result=invalidate_result=allocate_result=VK_SUCCESS;
    destroyed=invalidations=0;memset(contents,0,sizeof(contents));
}
int main(void) {
    report=tmpfile();assert(report);
    reset();assert(transfer_probe(NULL,NULL,0)==0);assert(destroyed==5);assert(invalidations==1);
    reset();wait_result=VK_TIMEOUT;assert(transfer_probe(NULL,NULL,0)==2);assert(destroyed==0);assert(invalidations==0);
    reset();submit_result=VK_ERROR_DEVICE_LOST;assert(transfer_probe(NULL,NULL,0)==2);assert(destroyed==0);assert(invalidations==0);
    reset();invalidate_result=VK_ERROR_MEMORY_MAP_FAILED;assert(transfer_probe(NULL,NULL,0)==1);assert(destroyed==5);
    reset();allocate_result=VK_ERROR_OUT_OF_DEVICE_MEMORY;assert(transfer_probe(NULL,NULL,0)==1);assert(destroyed==1);
    fclose(report);
    puts("Host cleanup policy: 5 fault/success scenarios passed; no GPU executed");
    return 0;
}
