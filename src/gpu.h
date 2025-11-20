#pragma once

#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include "common.h"
#include "allocators.h"

#define VK_ASSERT(statement)       \
    {                              \
        VkResult err = statement;  \
        ASSERT(err == VK_SUCCESS); \
    }                              \

constexpr usize FRAMES_IN_FLIGHT = 2;
constexpr u64 ONE_SECOND_TIMEOUT = 1'000'000'000;

struct FrameContext {
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;
    VkSemaphore swapchain_semaphore;
    VkSemaphore render_semaphore;
    VkFence render_fence;
};

struct VulkanContext {
    HWND window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkImage swapchain_images[FRAMES_IN_FLIGHT];
    VkImageView swapchain_images_views[FRAMES_IN_FLIGHT];
    FrameContext frames[FRAMES_IN_FLIGHT];
    VkDescriptorPool uniforms_desc_pool;
    u64 frames_counter;
};

b32 initializeVulkan(VulkanContext* to_init, b32 debug_mode, Arena* scratch_arena);

struct VulkanMemoryAllocator {
    BuddyAllocator allocator;    
    VkDeviceMemory memory;    
};

b32 initializeGPUAllocator(VulkanMemoryAllocator* gpu_allocator, VulkanContext* vk_context, VkMemoryPropertyFlags memory_properties, Arena* metadata_arena, usize min_alloc_size, usize max_alloc_size, usize total_size);

// NOTE: Sometimes you just want a big block
// of memory that you are not going to sub-allocate.
VkDeviceMemory debugAllocateDirectGPUMemory(VulkanContext* vk_context, VkMemoryPropertyFlags memory_properties, usize size);

VkShaderModule loadAndCreateShader(VulkanContext* vk_context, const char* path, Arena* scratch_arena);

struct VulkanPipeline {
    VkPipeline pipeline;  
    VkPipelineLayout layout;
    VkDescriptorSetLayout desc_set_layout;
};

void createChunkRenderPipelines(VulkanContext* vk_context, VulkanPipeline* chunk_pipeline, Arena* scratch_arena);
