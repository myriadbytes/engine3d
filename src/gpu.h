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

    VkImage depth_img;
    VkImageView depth_view;
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

struct VulkanMemoryAllocator {
    BuddyAllocator allocator;    
    VkDeviceMemory memory;    
};

b32 initializeVulkan(VulkanContext* to_init, b32 debug_mode, Arena* scratch_arena);
b32 initializeDepthTextures(VulkanContext* vk_context, VulkanMemoryAllocator* vram_allocator);

b32 initializeGPUAllocator(VulkanMemoryAllocator* gpu_allocator, VulkanContext* vk_context, VkMemoryPropertyFlags memory_properties, Arena* metadata_arena, usize min_alloc_size, usize max_alloc_size, usize total_size);

// NOTE: Sometimes you just want a big block
// of memory that you are not going to sub-allocate.
VkDeviceMemory debugAllocateDirectGPUMemory(VulkanContext* vk_context, VkMemoryPropertyFlags memory_properties, usize size);

VkShaderModule loadAndCreateShader(VulkanContext* vk_context, const char* path, Arena* scratch_arena);

struct VulkanPipeline {
    VkPipeline pipeline;  
    VkPipelineLayout layout;
    // WARNING: The simple pipelines we have now
    // only need one descriptor set, but that
    // could change.
    VkDescriptorSetLayout desc_set_layout;
};

void createChunkRenderPipelines(VulkanContext* vk_context, VulkanPipeline* chunk_pipeline, Arena* scratch_arena);

struct TextRenderingState {
    b32 is_initialized;

    VulkanPipeline text_pipeline;

    VkImage bitmap_font;
    VkImageView bitmap_font_view;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
};

b32 initializeTextRenderingState(TextRenderingState* text_rendering_state, VulkanContext* vk_context, VkCommandBuffer cmd_buf, VulkanMemoryAllocator* vram_allocator, u8* staging_buffer_mapped, VkBuffer staging_buffer, Arena* scratch_arena);
// NOTE: The debug text is drawn on a terminal-like grid, using a monospace font.
void drawDebugTextOnScreen(TextRenderingState* text_rendering_state, VkCommandBuffer cmd_buf, const char* text, u32 start_row, u32 start_col);
