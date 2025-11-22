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

struct GraphicsMemoryAllocator {
    VkDevice device;
    BuddyAllocator allocator;    
    VkDeviceMemory memory;    
};

struct Frame {
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;

    VkSemaphore swapchain_semaphore;
    VkSemaphore render_semaphore;
    VkFence render_fence;

    VkImage swapchain_image;
    VkImageView swapchain_image_view;
    VkImage depth_img;
    VkImageView depth_view;
};

struct StagingBuffer {
    VkBuffer buffer;
    usize buffer_size;
    u8* mapped_data;
};

constexpr u32 STAGING_BUFFERS_PER_FRAME = 16;
// NOTE: The size of the worst case chunk mesh for now.
constexpr u32 STAGING_BUFFER_MIN_SIZE = MEGABYTES(2);

struct Renderer {
    HWND window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;

    Frame frames[FRAMES_IN_FLIGHT];
    u64 frames_counter;

    // NOTE: VRAM with big subdivision for things like
    // vertex buffers or render targets.
    GraphicsMemoryAllocator vram_allocator;
    // NOTE: GPU-visible host RAM with small subdivision
    // for uniform buffers (e.g. matrices).
    GraphicsMemoryAllocator host_allocator;

    // NOTE: GPU-visible host RAM with big subdivisions,
    // for our staging buffers. We don't actually need
    // an allocator because they are currently fixed size
    // and allocated once at startup, but it's easier to
    // reuse the abstractions I already wrote.
    GraphicsMemoryAllocator staging_allocator;

    // NOTE: There is number of staging buffers per frame for uploads.
    // I think it's better to separate per frames so you don't use a staging
    // buffer that's being copied from during the execution of frame A's
    // commands while you are recording frame B's commands.
    StagingBuffer staging_buffers[FRAMES_IN_FLIGHT*STAGING_BUFFERS_PER_FRAME];
    u32 distributed_staging_buffers;

    VkDescriptorPool global_desc_pool;
};

b32 rendererInitialize(Renderer* to_init, b32 debug_mode, Arena* static_arena, Arena* scratch_arena);
// NOTE: Returns NULL if no more staging buffers available for this frame.
StagingBuffer* rendererRequestStagingBuffer(Renderer* renderer);

VkShaderModule loadAndCreateShader(Renderer* vk_context, const char* path, Arena* scratch_arena);

struct GraphicsMemoryImageAllocation {
    VkImage image;  
    VkImageView image_view;
};
GraphicsMemoryImageAllocation graphicsMemoryAllocateImage(GraphicsMemoryAllocator* gpu_allocator, VkFormat img_format, u32 img_width, u32 img_height, VkImageUsageFlags usage);

// NOTE: The simple pipelines we have now
// only need one descriptor set, but that
// could change. I have seen 3 as like a
// per-object -> per-frame -> per-app
// thing so we're going with that for now.
constexpr u32 PIPELINES_MAX_SETS = 3;

struct VulkanPipeline {
    VkPipeline pipeline;  
    VkPipelineLayout layout;
    VkDescriptorSetLayout desc_sets_layouts[PIPELINES_MAX_SETS];
};

constexpr u32 BUILDER_MAX_DESC_PER_SET = 4;

struct VulkanPipelineBuilder {
    Arena* scratch_arena;

    VkPipelineShaderStageCreateInfo shader_stages[2];
    VkPipelineVertexInputStateCreateInfo input_info;    
    VkPipelineInputAssemblyStateCreateInfo assembly_info;
    VkPipelineTessellationStateCreateInfo tessellation_info;
    VkPipelineViewportStateCreateInfo viewport_info;
    VkPipelineRasterizationStateCreateInfo raster_info;
    VkPipelineMultisampleStateCreateInfo multisample_info;
    VkPipelineDepthStencilStateCreateInfo depth_stencil_info;
    // NOTE: Only one blend attachment for all pipelines
    // since I am not doing deferred for now.
    VkPipelineColorBlendAttachmentState blend_attachments[1];
    VkPipelineColorBlendStateCreateInfo blend_info;
    // NOTE: All pipelines will just have viewport and scissor
    // as dynamic state, because rebuilding pipelines on window
    // resize sounds horrible.
    VkDynamicState dynamic_states[2];
    VkPipelineDynamicStateCreateInfo dynamic_state_info;

    VkFormat color_attachment_format;
    VkFormat depth_attachment_format;
    VkPipelineRenderingCreateInfo rendering_info;

    // NOTE: This is trying to do automatic set/binding number association
    // based on the order of the calls to pipelineBuilderAdd(...).
    VkDescriptorSetLayoutBinding shader_bindings[PIPELINES_MAX_SETS * BUILDER_MAX_DESC_PER_SET];
    VkDescriptorSetLayoutCreateInfo sets_info[PIPELINES_MAX_SETS];
    u32 sets_bindings_count[PIPELINES_MAX_SETS];
    u32 current_set;
    u32 current_binding;

    VkPushConstantRange push_constants;
};

// TODO: enable backface culling
// TODO: enable wireframe
// TODO: enable depth
// TODO: add uniform
// TODO: add vertex buffer ? or derive from attributes
// TODO: add vertex attribute
void pipelineBuilderInitialize(VulkanPipelineBuilder* builder);
void pipelineBuilderSetVertexShader(VulkanPipelineBuilder* builder, VkShaderModule shader);
void pipelineBuilderSetFragmentShader(VulkanPipelineBuilder* builder, VkShaderModule shader);
void pipelineBuilderEnableAlphaBlending(VulkanPipelineBuilder* builder);

void pipelineBuilderAddImageSampler(VulkanPipelineBuilder* builder);
void pipelineBuilderAddPushConstant(VulkanPipelineBuilder* builder, usize size, VkShaderStageFlags stages);

void pipelineBuilderCreatePipeline(VulkanPipelineBuilder* builder, VkDevice device, VulkanPipeline* to_create);
