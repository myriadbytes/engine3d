#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <strsafe.h>

#include "common.h"
#include "game_api.h"
#include "maths.h"
#include "noise.h"
#include "img.h"
#include "world.h"

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
    u64 frames_counter;
};

// TODO: Automatic debug mode based on internal / release build.
b32 initializeVulkan(VulkanContext* to_init, b32 debug_mode, Arena* scratch_arena) {
    *to_init = {};

    to_init->frames_counter = 0;

    // NOTE: Vulkan instance creation.
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    // FIXME: If the debug mode was enabled but the layers are not present, the
    // app should not crash and instead should just run without the layers.
    const char* instance_extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    const char* enabled_layers[] = {"VK_LAYER_KHRONOS_validation"};
    instance_info.ppEnabledExtensionNames = instance_extensions;
    instance_info.enabledExtensionCount = ARRAY_COUNT(instance_extensions);
    instance_info.ppEnabledLayerNames = debug_mode ? enabled_layers : nullptr;
    instance_info.enabledLayerCount = debug_mode ? ARRAY_COUNT(enabled_layers) : 0;

    VK_ASSERT(vkCreateInstance(&instance_info, nullptr, &to_init->instance));

    // NOTE: Pick the first physical device.
    // FIXME: This should loop over the physical devices and find the best one.
    u32 physical_device_count;
    vkEnumeratePhysicalDevices(to_init->instance, &physical_device_count, nullptr);
    VkPhysicalDevice* physical_devices = (VkPhysicalDevice*) pushBytes(scratch_arena, physical_device_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(to_init->instance, &physical_device_count, physical_devices);

    ASSERT(physical_device_count > 0);
    to_init->physical_device = physical_devices[0];

    VkPhysicalDeviceProperties2 physical_device_props = {};
    physical_device_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2(to_init->physical_device, &physical_device_props);
    OutputDebugStringA(physical_device_props.properties.deviceName);

    // NOTE: Create the logical device out of the physical device.
    // WARN: We'll just create one queue out of family zero. I believe that
    // on most GPUs this is queue family that contains the general-purpose queue ?
    f32 queue_priorities[] = {1.0f};
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = queue_priorities;

    const char* device_extensions[] = {"VK_KHR_swapchain"};

    VkPhysicalDeviceVulkan13Features vk_1_3_features = {};
    vk_1_3_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk_1_3_features.synchronization2 = VK_TRUE;
    vk_1_3_features.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &vk_1_3_features;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.queueCreateInfoCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.enabledExtensionCount = ARRAY_COUNT(device_extensions);

    VK_ASSERT(vkCreateDevice(to_init->physical_device, &device_info, nullptr, &to_init->device));
    vkGetDeviceQueue(to_init->device, 0, 0, &to_init->queue);
    ASSERT(to_init->queue);

    // NOTE: Surface creation.
    // WARNING: Getting the handles like that is error prone, but it's convenient
    // for now.
    to_init->window = FindWindowA("Voxel Game Window Class", nullptr);
    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hinstance = GetModuleHandleA(NULL);
    surface_info.hwnd = to_init->window;
    VK_ASSERT(vkCreateWin32SurfaceKHR(to_init->instance, &surface_info, nullptr, &to_init->surface));

    // NOTE: Swapchain creation.
    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(to_init->physical_device, to_init->surface, &surface_caps);

    // TODO: Support more swapchain formats, and find the "best" one to use
    // in the swapchain creation struct.
    u32 surface_formats_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(to_init->physical_device, to_init->surface, &surface_formats_count, nullptr);
    VkSurfaceFormatKHR* surface_formats = (VkSurfaceFormatKHR*)pushBytes(scratch_arena, surface_formats_count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(to_init->physical_device, to_init->surface, &surface_formats_count, surface_formats);
    b32 found_suitable_format = false;
    for (u32 surface_format_idx = 0; surface_format_idx < surface_formats_count; surface_format_idx++) {
        VkSurfaceFormatKHR surface_format = surface_formats[surface_format_idx];

        if (surface_format.format == VK_FORMAT_B8G8R8A8_SRGB && surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            found_suitable_format = true;
            break;
        }
    }
    ASSERT(found_suitable_format);

    // TODO: Support more presentation modes ? Vsync is fine for now but
    // maybe it would be better to run in Mailbox and handle the frame pacing
    // outselves. Apparently my Intel B580 doesn't even have mailbox, so that
    // makes it easier to decide.
    u32 present_modes_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(to_init->physical_device, to_init->surface, &present_modes_count, nullptr);
    VkPresentModeKHR* present_modes = (VkPresentModeKHR*)pushBytes(scratch_arena, present_modes_count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(to_init->physical_device, to_init->surface, &present_modes_count, present_modes);
    b32 found_suitable_present_mode = false;
    for (u32 present_mode_idx = 0; present_mode_idx < present_modes_count; present_mode_idx++) {
        VkPresentModeKHR present_mode = present_modes[present_mode_idx];

        if (present_mode == VK_PRESENT_MODE_FIFO_KHR) {
            found_suitable_present_mode = true;
            break;
        }
    }
    ASSERT(found_suitable_present_mode);

    // TODO: Vulkan-Tutorial says: "However, simply sticking to this minimum means
    // that we may sometimes have to wait on the driver to complete internal operations
    // before we can acquire another image to render to. Therefore it is recommended
    // to request at least one more image than the minimum."
    // Should we use 3 framebuffers ?
    ASSERT(surface_caps.minImageCount <= FRAMES_IN_FLIGHT && (surface_caps.maxImageCount >= FRAMES_IN_FLIGHT || surface_caps.maxImageCount == 0));

    VkSwapchainCreateInfoKHR swapchain_create_info = {};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = to_init->surface;
    swapchain_create_info.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_create_info.minImageCount = FRAMES_IN_FLIGHT;
    swapchain_create_info.imageExtent = surface_caps.currentExtent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform = surface_caps.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSwapchainKHR(to_init->device, &swapchain_create_info, nullptr, &to_init->swapchain));

    u32 swapchain_images_count;
    vkGetSwapchainImagesKHR(to_init->device, to_init->swapchain, &swapchain_images_count, nullptr);
    // FIXME: Apparently the driver is allowed to create more images than what we asked for. We should
    // support that eventually.
    ASSERT(swapchain_images_count == FRAMES_IN_FLIGHT);
    vkGetSwapchainImagesKHR(to_init->device, to_init->swapchain, &swapchain_images_count, to_init->swapchain_images);

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        VkImageViewCreateInfo swapchain_view_info = {};
        swapchain_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        swapchain_view_info.image = to_init->swapchain_images[frame_idx];
        swapchain_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        swapchain_view_info.format = VK_FORMAT_B8G8R8A8_SRGB;
        swapchain_view_info.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        vkCreateImageView(to_init->device, &swapchain_view_info, nullptr, &to_init->swapchain_images_views[frame_idx]);
    }

    // NOTE: Create the command pool and buffers used by our frames.
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.pNext = nullptr;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = 0;
    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        VK_ASSERT(vkCreateCommandPool(to_init->device, &pool_info, nullptr, &to_init->frames[frame_idx].cmd_pool));

        VkCommandBufferAllocateInfo cmd_buffer_info = {};
        cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_info.commandPool = to_init->frames[frame_idx].cmd_pool;
        cmd_buffer_info.commandBufferCount = 1;
        cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_ASSERT(vkAllocateCommandBuffers(to_init->device, &cmd_buffer_info, &to_init->frames[frame_idx].cmd_buffer));
    }

    // NOTE: ... And the synchronization primitives.
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        VK_ASSERT(vkCreateFence(to_init->device, &fence_info, nullptr, &to_init->frames[frame_idx].render_fence));

        VK_ASSERT(vkCreateSemaphore(to_init->device, &semaphore_info, nullptr, &to_init->frames[frame_idx].swapchain_semaphore));
        VK_ASSERT(vkCreateSemaphore(to_init->device, &semaphore_info, nullptr, &to_init->frames[frame_idx].render_semaphore));
    }

    return true;
}

// TODO: Many duplicate vertices. Is it easy/possible to use indices here ?
// TODO: Currently the chunk doesn't look into neighboring chunks. This means there are generated
// triangles between solid blocks on two different chunks.
// TODO: Look into switching to greedy meshing.
void generateNaiveChunkMesh(Chunk* chunk, ChunkVertex* out_vertices, usize* out_generated_vertex_count) {
    usize emitted = 0;
    for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){

        usize x = (i % CHUNK_W);
        usize y = (i / CHUNK_W) % (CHUNK_W);
        usize z = (i / (CHUNK_W * CHUNK_W));

        if (!chunk->data[i]) continue;

        b32 empty_pos_x = true;
        b32 empty_neg_x = true;
        b32 empty_pos_y = true;
        b32 empty_neg_y = true;
        b32 empty_pos_z = true;
        b32 empty_neg_z = true;

        if (x < (CHUNK_W - 1)) {
            empty_pos_x = !chunk->data[i + 1];
        }
        if (x > 0) {
            empty_neg_x = !chunk->data[i - 1];
        }

        if (y < (CHUNK_W - 1)) {
            empty_pos_y = !chunk->data[i + CHUNK_W];
        }
        if (y > 0) {
            empty_neg_y = !chunk->data[i - CHUNK_W];
        }

        if (z < (CHUNK_W - 1)) {
            empty_pos_z = !chunk->data[i + CHUNK_W * CHUNK_W];
        }
        if (z > 0) {
            empty_neg_z = !chunk->data[i - CHUNK_W * CHUNK_W];
        }

        v3 position = {(f32)x, (f32)y, (f32)z};

        if (empty_pos_x) {
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};
        }

        if (empty_neg_x) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {-1, 0, 0}};
        }

        if (empty_pos_y) {
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};
        }

        if (empty_neg_y) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

        }

        if (empty_pos_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 0, 1}};
        }

        if (empty_neg_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, 0, -1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 0, -1}};
        }
    }

    *out_generated_vertex_count = emitted;
}

b32 raycast_aabb(v3 ray_origin, v3 ray_direction, v3 bb_min, v3 bb_max, f32* out_t) {
    // NOTE: Original algorithm from here:
    // https://tavianator.com/2011/ray_box.html
    f32 tmin = -INFINITY;
    f32 tmax = INFINITY;

    // TODO: The original blog post suggests using SSE2's min and max instructions
    // as an easy optimization.

    if (ray_direction.x != 0.0f) {
        f32 tx1 = (bb_min.x - ray_origin.x)/ray_direction.x;
        f32 tx2 = (bb_max.x - ray_origin.x)/ray_direction.x;

        tmin = max(tmin, min(tx1, tx2));
        tmax = min(tmax, max(tx1, tx2));
    }

    if (ray_direction.y != 0.0f) {
        f32 ty1 = (bb_min.y - ray_origin.y)/ray_direction.y;
        f32 ty2 = (bb_max.y - ray_origin.y)/ray_direction.y;

        tmin = max(tmin, min(ty1, ty2));
        tmax = min(tmax, max(ty1, ty2));
    }

    if (ray_direction.z != 0.0f) {
        f32 tz1 = (bb_min.z - ray_origin.z)/ray_direction.z;
        f32 tz2 = (bb_max.z - ray_origin.z)/ray_direction.z;

        tmin = max(tmin, min(tz1, tz2));
        tmax = min(tmax, max(tz1, tz2));
    }

    if (tmax < tmin || tmin <= 0.0f) {
        return false;
    }

    *out_t = tmin;
    return true;
}

inline b32 point_inclusion_aabb(v3 point, v3 bb_min, v3 bb_max) {
    return point.x >= bb_min.x && point.x <= bb_max.x
        && point.y >= bb_min.y && point.y <= bb_max.y
        && point.z >= bb_min.z && point.z <= bb_max.z;
}

// NOTE: Classic Amanatides & Woo algorithm.
// http://www.cse.yorku.ca/~amana/research/grid.pdf
// The traversal origin needs to be on the boundary or inside the chunk already,
// and in chunk-relative coordinates. That first part should not be a problem in
// the final game since the whole world will be filled with chunks, but for now
// we raycast with the single debug chunk before calling this function.
b32 raycast_chunk_traversal(Chunk* chunk, v3 traversal_origin, v3 traversal_direction, usize* out_i) {

    // FIXME: This assertion to check if the origin is in chunk-relative coordinates sometimes fires
    // because one of the components is -0.000001~. This could just be expected precision issues with
    // the chunk bounding box raycast function.
    // ASSERT(point_inclusion_aabb(traversal_origin, v3 {0, 0, 0}, v3 {CHUNK_W, CHUNK_W, CHUNK_W}));

    i32 x, y, z;
    x = (i32)(traversal_origin.x);
    y = (i32)(traversal_origin.y);
    z = (i32)(traversal_origin.z);

    // NOTE: If the ray origin is on a positive boundary, the truncation
    // will create x/y/z = 16 ("first block of neighboring chunk") instead
    // of x/y/z = 15 ("last block of this chunk") so we need to correct this.
    if (traversal_origin.x == (f32)CHUNK_W) x--;
    if (traversal_origin.y == (f32)CHUNK_W) y--;
    if (traversal_origin.z == (f32)CHUNK_W) z--;

    i32 step_x = traversal_direction.x > 0.0 ? 1 : -1;
    i32 step_y = traversal_direction.y > 0.0 ? 1 : -1;
    i32 step_z = traversal_direction.z > 0.0 ? 1 : -1;

    f32 t_max_x = ((step_x > 0.0) ? ((f32)x + 1 - traversal_origin.x) : (traversal_origin.x - (f32)x)) / fabsf(traversal_direction.x);
    f32 t_max_y = ((step_y > 0.0) ? ((f32)y + 1 - traversal_origin.y) : (traversal_origin.y - (f32)y)) / fabsf(traversal_direction.y);
    f32 t_max_z = ((step_z > 0.0) ? ((f32)z + 1 - traversal_origin.z) : (traversal_origin.z - (f32)z)) / fabsf(traversal_direction.z);

    f32 t_delta_x = 1.0 / fabs(traversal_direction.x);
    f32 t_delta_y = 1.0 / fabs(traversal_direction.y);
    f32 t_delta_z = 1.0 / fabs(traversal_direction.z);

    while (x >= 0 && x < CHUNK_W && y >= 0 && y < CHUNK_W && z >= 0 && z < CHUNK_W) {
        if (chunk->data[x + CHUNK_W * y + CHUNK_W * CHUNK_W * z]) {
            *out_i = x + CHUNK_W * y + CHUNK_W * CHUNK_W * z;
            return true;
        }

        if (t_max_x < t_max_y) {
            if (t_max_x < t_max_z) {
                x += step_x;
                t_max_x += t_delta_x;
            } else {
                z += step_z;
                t_max_z += t_delta_z;
            }
        } else {
            if (t_max_y < t_max_z) {
                y += step_y;
                t_max_y += t_delta_y;
            } else {
                z += step_z;
                t_max_z += t_delta_z;
            }
        }
    }

    return false;
}

/*

void refreshChunk(D3DContext* d3d_context, Chunk* chunk) {

    // NOTE: generate directly in the mapped upload buffer
    ChunkVertex* buffer;
    D3D12_RANGE read_range = {};
    // NOTE: Passing NULL as the read range would mean we are gonna read the entire
    // buffer, but for optimization and simplicity's sake let's say that the user
    // should not reading from a mapped upload buffer.
    chunk->upload_buffer->Map(0, &read_range, (void**)&buffer);

    generateNaiveChunkMesh(chunk, buffer, &chunk->vertices_count);

    // NOTE: Passing NULL as the write range signals to the driver that we
    // we wrote the entire buffer.
    chunk->upload_buffer->Unmap(0, NULL);

    // TODO: add a way to check that the number of generated vertices is not bigger than the upload and vertex buffers
    blockingUploadToGPUBuffer(d3d_context, chunk->upload_buffer, chunk->vertex_buffer, chunk->vertices_count * sizeof(ChunkVertex));

    // NOTE: After the upload, the buffer is in COPY_DEST state, so it needs
    // to be transitioned back into a vertex buffer.
    chunk->vbo_ready = false;
}

void waitForGPU(D3DContext* d3d_context) {
    D3DFrameContext* current_frame = &d3d_context->frames[d3d_context->current_frame_idx];

    d3d_context->graphics_command_queue->Signal(current_frame->fence, ++current_frame->fence_ready_value);
    if(current_frame->fence->GetCompletedValue() < current_frame->fence_ready_value) {
        current_frame->fence->SetEventOnCompletion(current_frame->fence_ready_value, current_frame->fence_wait_event);
        WaitForSingleObject(current_frame->fence_wait_event, INFINITE);
    }

    d3d_context->copy_command_queue->Signal(d3d_context->copy_fence, ++d3d_context->copy_fence_ready_value);
    if (d3d_context->copy_fence->GetCompletedValue() < d3d_context->copy_fence_ready_value) {
        d3d_context->copy_fence->SetEventOnCompletion(d3d_context->copy_fence_ready_value, d3d_context->copy_fence_wait_event);
        WaitForSingleObject(d3d_context->copy_fence_wait_event, INFINITE);
    }
}

// TODO: Switch away from null-terminated strings.
// NOTE: The debug text is drawn on a terminal-like grid, using a monospace font.
void drawDebugTextOnScreen(TextRenderer* text_renderer, ID3D12GraphicsCommandList* command_list, const char* text, u32 start_row, u32 start_col) {
    // NOTE: Setup pipeline and texture binding.
    if (!text_renderer->texture_ready) {
        D3D12_RESOURCE_BARRIER to_shader_res_barrier = {};
        to_shader_res_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_shader_res_barrier.Transition.pResource = text_renderer->font_texture_buffer;
        to_shader_res_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        to_shader_res_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        to_shader_res_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &to_shader_res_barrier);

        text_renderer->texture_ready = true;
    }
    command_list->SetPipelineState(text_renderer->render_pipeline.pipeline_state);
    command_list->SetGraphicsRootSignature(text_renderer->render_pipeline.root_signature);
    command_list->SetDescriptorHeaps(1, &text_renderer->font_texture_descriptor_heap);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_texture_handle = text_renderer->font_texture_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    command_list->SetGraphicsRootDescriptorTable(0, gpu_texture_handle);

    // NOTE: https://datagoblin.itch.io/monogram
    // The bitmap font is 96x96 and has 16x8 chars, so the individual
    // characters are 6x12.
    // The vertical layout is :
    // - 2px ascender on some chars
    // - 5px for all chars
    // - 2px descender on some chars
    // - 3px padding on the bottom
    // And the horizontal layout is :
    // - 1px padding on the left
    // - 5px for all chars
    // So when laying out chars on a grid, there is already a horizontal
    // space between them because of the 1px left padding, and a big line
    // space of 3 pixels. Most chars are also vertically centered, due to the
    // ascender/descender pair.
    constexpr f32 char_ratio = 6.f / 12.f;
    constexpr f32 char_scale = 0.04f; // TODO: Make this configurable ?
    constexpr f32 char_width = (char_ratio * char_scale) * 2;
    constexpr f32 char_height = (char_scale) * 2;

    // NOTE: The shader produce a quad that covers the whole screen.
    // We need to make it a quad of the right proportions and
    // located in the first slot.
    m4 quad_setup_matrix =
        makeTranslation((f32)start_col * char_width, (f32)start_row * -char_height, 0)
        * makeTranslation(-(1 - char_width), 1 - char_height / 2, 0)
        * makeScale(char_width/2, char_height/2, 0);

    i32 row = start_row;
    i32 col = start_col;
    for (int char_i = 0; text[char_i] != 0; char_i++) {
        u32 char_ascii_codepoint = (u32)text[char_i];
        if (char_ascii_codepoint == '\n') {
            row = start_row;
            col++;
            continue;
        }

        m4 char_translate = makeTranslation((f32)row * char_width, (f32)col * -char_height , 0);
        m4 char_matrix = char_translate * quad_setup_matrix;

        // TODO: Implement text wrapping here.
        row++;

        command_list->SetGraphicsRoot32BitConstants(1, 16, char_matrix.data, 0);
        command_list->SetGraphicsRoot32BitConstants(2, 1, &char_ascii_codepoint, 0);
        command_list->DrawInstanced(6, 1, 0, 0);
    }
}

*/

struct GameState {
    f32 time;
    RandomSeries random_series;
    SimplexTable* simplex_table;

    VulkanContext vk_context;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;
    v3 camera_forward;
    b32 orbit_mode;

    ChunkMemoryPool chunk_pool;
    WorldHashMap world;

    //D3DPipeline chunk_render_pipeline;
    //D3DPipeline wireframe_render_pipeline;

    //TextRenderer text_renderer;

    b32 is_wireframe;

    u8 static_arena_memory[MEGABYTES(2)];
    Arena static_arena;

    u8 frame_arena_memory[MEGABYTES(2)];
    Arena frame_arena;
};

extern "C"
void gameUpdate(f32 dt, GameMemory* memory, InputState* input) {
    ASSERT(memory->permanent_storage_size >= sizeof(GameState));
    GameState* game_state = (GameState*)memory->permanent_storage;

    // INITIALIZATION
    if(!memory->is_initialized) {
        game_state->static_arena.base = game_state->static_arena_memory;
        game_state->static_arena.capacity = ARRAY_COUNT(game_state->static_arena_memory);

        game_state->frame_arena.base = game_state->frame_arena_memory;
        game_state->frame_arena.capacity = ARRAY_COUNT(game_state->frame_arena_memory);

        ASSERT(initializeVulkan(&game_state->vk_context, true, &game_state->frame_arena));

        //initChunkMemoryPool(&game_state->chunk_pool, game_state->d3d_context.device);
        game_state->world.nb_occupied = 0;

        game_state->player_position = {110, 40, 110};
        game_state->orbit_mode = false;
        game_state->time = 0;
        game_state->camera_pitch = -1 * PI32 / 6;
        game_state->camera_yaw = 1 * PI32 / 3;
        game_state->random_series = 0xC0FFEE; // fixed seed for now
        game_state->simplex_table = simplex_table_from_seed(0xC0FFEE, &game_state->static_arena);

        //createChunkRenderPipelines(&game_state->d3d_context, &game_state->chunk_render_pipeline, &game_state->wireframe_render_pipeline);

        //initializeTextRendering(&game_state->d3d_context, &game_state->text_renderer, &game_state->frame_arena);

        memory->is_initialized = true;
    }

    // SIMULATION
    game_state->time += dt;

    f32 mouse_sensitivity = 0.01;
    f32 stick_sensitivity = 0.05;

    game_state->camera_pitch += input->kb.mouse_delta.y * mouse_sensitivity;
    game_state->camera_yaw += input->kb.mouse_delta.x * mouse_sensitivity;

    game_state->camera_pitch += input->ctrl.right_stick.y * stick_sensitivity;
    game_state->camera_yaw += input->ctrl.right_stick.x * stick_sensitivity;

    f32 safe_pitch = PI32 / 2.f - 0.05f;
    game_state->camera_pitch = clamp(game_state->camera_pitch, -safe_pitch, safe_pitch);

    game_state->camera_forward.x = cosf(game_state->camera_yaw) * cosf(game_state->camera_pitch);
    game_state->camera_forward.y = sinf(game_state->camera_pitch);
    game_state->camera_forward.z = sinf(game_state->camera_yaw) * cosf(game_state->camera_pitch);

    v3 camera_right = normalize(cross(game_state->camera_forward, {0, 1, 0}));

    f32 speed = 20 * dt;
    if (input->kb.keys[SCANCODE_LSHIFT].is_down || input->ctrl.x.is_down) {
        speed *= 5;
    }

    // TODO: Nothing is normalized, so the player moves faster in diagonal directions.
    // Let's just say it's a Quake reference.
    // Also you can move twice as fast by using a keyboard and a controller at the same time.
    // That's for speedrunners.

    if(input->kb.keys[SCANCODE_Q].is_down || input->ctrl.lb.is_down) {
        game_state->player_position.y -= speed;
    }
    if(input->kb.keys[SCANCODE_E].is_down || input->ctrl.rb.is_down) {
        game_state->player_position.y += speed;
    }
    if(input->kb.keys[SCANCODE_A].is_down) {
        game_state->player_position += -camera_right * speed;
    }
    if(input->kb.keys[SCANCODE_D].is_down) {
        game_state->player_position += camera_right * speed;
    }
    if(input->kb.keys[SCANCODE_S].is_down) {
        game_state->player_position += -game_state->camera_forward * speed;
    }
    if(input->kb.keys[SCANCODE_W].is_down) {
        game_state->player_position += game_state->camera_forward * speed;
    }

    game_state->player_position += input->ctrl.left_stick.x * camera_right * speed;
    game_state->player_position += input->ctrl.left_stick.y * game_state->camera_forward * speed;

    if (input->kb.keys[SCANCODE_G].is_down && input->kb.keys[SCANCODE_G].transitions == 1) {
        game_state->is_wireframe = !game_state->is_wireframe;
    }

    if (input->kb.keys[SCANCODE_O].is_down && input->kb.keys[SCANCODE_O].transitions == 1) {
        game_state->orbit_mode = !game_state->orbit_mode;
    }

    /*
    // FIXME: The code for destroying the block the player is looking at is pretty convoluted.
    // There has to be a better way, maybe recursive ? But that has drawbacks too.
    if (input->kb.keys[SCANCODE_SPACE].is_down || input->ctrl.a.is_down) {

        usize chunk_to_traverse = 0;
        bool found_chunk = false;
        v3 traversal_origin;

        // NOTE: If the player is inside a chunk already, we don't have to do a raycast the first time.
        // This would be the default case once the world is entirely filled with chunks, but not for now.
        for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {
            Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
            if (!chunk) continue;

            v3 chunk_world_pos = chunkToWorldPos(chunk->chunk_position);
            v3 chunk_bb_min = {(f32)(chunk_world_pos.x), (f32)(chunk_world_pos.y), (f32)(chunk_world_pos.z)};
            v3 chunk_bb_max = {(f32)(chunk_world_pos.x+CHUNK_W), (f32)(chunk_world_pos.y+CHUNK_W), (f32)(chunk_world_pos.z+CHUNK_W)};

            if (point_inclusion_aabb(game_state->player_position, chunk_bb_min, chunk_bb_max)) {
                chunk_to_traverse = chunk_idx;
                traversal_origin = game_state->player_position;
                found_chunk = true;
                break;
            }
        }

        f32 minimum_t = 0.0;
        // TODO: We should have a termination condition here just in case. Maybe based on minimum_t ?
        while (true) {

            // NOTE: If a chunk to traverse was not set during init, meaning the player is not inside a chunk,
            // we need to look for one. We increment the minimum_t variable every time we intersect with a chunk
            // so that each iteration traverses the next chunk along the line of sight, until a block is found.
            f32 closest_t_during_search = INFINITY;
            if (!found_chunk) {
                for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {
                    Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
                    if (!chunk) continue;

                    v3 chunk_world_pos = chunkToWorldPos(chunk->chunk_position);
                    v3 chunk_bb_min = {(f32)(chunk_world_pos.x), (f32)(chunk_world_pos.y), (f32)(chunk_world_pos.z)};
                    v3 chunk_bb_max = {(f32)(chunk_world_pos.x+CHUNK_W), (f32)(chunk_world_pos.y+CHUNK_W), (f32)(chunk_world_pos.z+CHUNK_W)};

                    f32 hit_t;
                    b32 did_hit = raycast_aabb(game_state->player_position, game_state->camera_forward, chunk_bb_min, chunk_bb_max, &hit_t);

                    if (did_hit && hit_t < closest_t_during_search && hit_t > minimum_t) {
                        closest_t_during_search = hit_t;
                        chunk_to_traverse = chunk_idx;
                        found_chunk = true;
                    }
                }

                if (found_chunk) {
                    // NOTE: Update the minimum_t so that if no block is found in this chunk, we can look into the
                    // next furthest chunk next iteration.
                    traversal_origin = game_state->player_position + game_state->camera_forward * closest_t_during_search;
                    minimum_t = closest_t_during_search;
                }
            }

            if (found_chunk) {
                Chunk* chunk = getChunkByIndex(&game_state->world, chunk_to_traverse);

                v3 chunk_to_traverse_origin = chunkToWorldPos(chunk->chunk_position);
                v3 intersection_relative = traversal_origin - chunk_to_traverse_origin;

                usize block_idx;
                if (raycast_chunk_traversal(chunk, intersection_relative, game_state->camera_forward, &block_idx)) {
                    chunk->data[block_idx] = 0;
                    // FIXME: This is a quick temporary fix. Without that, we could modify the vertex buffer while it is used to render the previous
                    // frame. I would need profiling to know if it's THAT bad. A possible solution would be to decouple mesh generation and upload,
                    // i.e. generate the mesh but only upload it after the previous frame is rendered. Perhaps in a separate thread.
                    waitForGPU(&game_state->d3d_context);
                    refreshChunk(&game_state->d3d_context, chunk);
                    break;
                }

                // NOTE: This is confusing but I can't think of a better place to put it.
                // When we arrive here, it means the raycast found a chunk but that it didn't have any solid block
                // along the line. So we need to find a new one during the next iteration.
                found_chunk = false;
            } else {
                break;
            }
        }
    }

    // NOTE: Unload chunks too far from the player
    for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {
        Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
        if (!chunk) continue;

        v3 chunk_to_unload_world_pos = chunkToWorldPos(chunk->chunk_position);
        v3 chunk_to_unload_center_pos = chunk_to_unload_world_pos + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

        v3 player_chunk_center_pos = chunkToWorldPos(worldPosToChunk(game_state->player_position)) + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

        f32 dist = length(player_chunk_center_pos - chunk_to_unload_center_pos);
        if (dist > (f32)LOAD_RADIUS * CHUNK_W) {
            // NOTE: This chunk is too far away and needs to be unloaded.

            worldDelete(&game_state->world, chunk->chunk_position);
            releaseChunkMemoryToPool(&game_state->chunk_pool, chunk);
        }
    }

    // NOTE: Load new chunks as needed
    v3i player_chunk_pos = worldPosToChunk(game_state->player_position);
    for (i32 x = player_chunk_pos.x - LOAD_RADIUS; x <= player_chunk_pos.x + LOAD_RADIUS; x++) {
        for (i32 y = player_chunk_pos.y - LOAD_RADIUS; y <= player_chunk_pos.y + LOAD_RADIUS; y++) {
            for (i32 z = player_chunk_pos.z - LOAD_RADIUS; z <= player_chunk_pos.z + LOAD_RADIUS; z++) {

                v3i chunk_to_load_pos = v3i {x, y, z};
                v3 chunk_to_load_world_pos = chunkToWorldPos(chunk_to_load_pos);
                v3 chunk_to_load_center_pos = chunk_to_load_world_pos + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

                v3 player_chunk_center_pos = chunkToWorldPos(worldPosToChunk(game_state->player_position)) + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

                if (length(player_chunk_center_pos - chunk_to_load_center_pos) > (f32)LOAD_RADIUS * CHUNK_W) continue;

                if (!isChunkInWorld(&game_state->world, chunk_to_load_pos)) {
                    Chunk* chunk = acquireChunkMemoryFromPool(&game_state->chunk_pool);
                    chunk->chunk_position = chunk_to_load_pos;

                    for(usize block_idx = 0; block_idx < CHUNK_W * CHUNK_W * CHUNK_W; block_idx++){
                        u32 block_x = chunk_to_load_pos.x * CHUNK_W + (block_idx % CHUNK_W);
                        u32 block_y = chunk_to_load_pos.y * CHUNK_W + (block_idx / CHUNK_W) % (CHUNK_W);
                        u32 block_z = chunk_to_load_pos.z * CHUNK_W + (block_idx / (CHUNK_W * CHUNK_W));

                        // TODO: This needs to be parameterized and put into a function.
                        // The fancy name is "fractal brownian motion", but it's just summing
                        // noise layers with reducing intensity and increasing frequency.
                        f32 space_scaling_factor = 0.01;
                        f32 height_intensity = 32.0;
                        f32 height = 0;
                        for (i32 octave = 0; octave < 5; octave ++) {
                            height += ((simplex_noise_2d(game_state->simplex_table, (f32)block_x * space_scaling_factor, (f32) block_z * space_scaling_factor) + 1.f) / 2.f) * height_intensity;
                            space_scaling_factor *= 2.f;
                            height_intensity /= 3.f;
                        }

                        if (block_y <= height) {
                            chunk->data[block_idx] = 1;
                        } else {
                            chunk->data[block_idx] = 0;
                        }
                    }

                    refreshChunk(&game_state->d3d_context, chunk);
                    worldInsert(&game_state->world, chunk_to_load_pos, chunk);
                }
            }
        }
    }

    */

    // RENDERING

    // NOTE: Wait until the work previously submitted by the frame is done.
    FrameContext& current_frame = game_state->vk_context.frames[game_state->vk_context.frames_counter % FRAMES_IN_FLIGHT];
    VK_ASSERT(vkWaitForFences(game_state->vk_context.device, 1, &current_frame.render_fence, true, ONE_SECOND_TIMEOUT));
    VK_ASSERT(vkResetFences(game_state->vk_context.device, 1, &current_frame.render_fence));

    // NOTE: Request an image from the swapchain that we can use for rendering.
    u32 swapchain_img_idx;
    VK_ASSERT(vkAcquireNextImageKHR(game_state->vk_context.device, game_state->vk_context.swapchain, ONE_SECOND_TIMEOUT, current_frame.swapchain_semaphore, nullptr, &swapchain_img_idx));

    // NOTE: Begin using the command buffer, specifying that we will only be
    // submitting once before resetting it (because the render commands needed
    // for the scene are different every frame).
    VK_ASSERT(vkResetCommandBuffer(current_frame.cmd_buffer, 0));

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_ASSERT(vkBeginCommandBuffer(current_frame.cmd_buffer, &begin_info));

    // NOTE: Transition the framebuffer into a format suitable for rendering.
    VkImageMemoryBarrier2 render_barrier = {};
    render_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    render_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    render_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    render_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    render_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    render_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    render_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // NOTE: This is really only useful for images with mip maps / layers,
    // but we have to fill it no matter what.
    render_barrier.subresourceRange = VkImageSubresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    render_barrier.image = game_state->vk_context.swapchain_images[swapchain_img_idx];

    VkDependencyInfo render_dep_info = {};
    render_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    render_dep_info.imageMemoryBarrierCount = 1;
    render_dep_info.pImageMemoryBarriers = &render_barrier;

    vkCmdPipelineBarrier2(current_frame.cmd_buffer, &render_dep_info);

    // NOTE: For dynamic rendering (without a render pass), we need to give info about
    // the render targets and fill out a render info struct.

    VkRenderingAttachmentInfo render_target_info = {};
    render_target_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    render_target_info.imageView = game_state->vk_context.swapchain_images_views[swapchain_img_idx];
    render_target_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    render_target_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    render_target_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_target_info.clearValue = {.color = {0.1, 0.1, 0.2, 1.0}};

    // TODO: Have a way to get the window dimensions without going to the OS...
    VkRect2D render_area = {};
    RECT client_rect;
    GetClientRect(game_state->vk_context.window, &client_rect);
    render_area.extent.height = client_rect.bottom;
    render_area.extent.width = client_rect.right;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.pColorAttachments = &render_target_info;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.layerCount = 1;
    rendering_info.renderArea = render_area;

    vkCmdBeginRendering(current_frame.cmd_buffer, &rendering_info);
    
    vkCmdEndRendering(current_frame.cmd_buffer);

    // NOTE: Transition the framebuffer into a format suitable for presentation.
    VkImageMemoryBarrier2 present_barrier = {};
    present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    present_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    present_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    present_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    present_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    present_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // NOTE: This is really only useful for images with mip maps / layers,
    // but we have to fill it no matter what.
    present_barrier.subresourceRange = VkImageSubresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    present_barrier.image = game_state->vk_context.swapchain_images[swapchain_img_idx];

    VkDependencyInfo present_dep_info = {};
    present_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    present_dep_info.imageMemoryBarrierCount = 1;
    present_dep_info.pImageMemoryBarriers = &present_barrier;

    vkCmdPipelineBarrier2(current_frame.cmd_buffer, &present_dep_info);

    // NOTE: Finalize the command buffer.
    VK_ASSERT(vkEndCommandBuffer(current_frame.cmd_buffer));

    // NOTE: And send it for execution, wiring all the synchronization.
    VkCommandBufferSubmitInfo cmd_buffer_submit_info = {};
    cmd_buffer_submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buffer_submit_info.commandBuffer = current_frame.cmd_buffer;
    // NOTE: The color attatchment output stage will wait for the swapchain
    // semaphore to be signaled.
    VkSemaphoreSubmitInfo wait_info = {};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = current_frame.swapchain_semaphore;
    wait_info.value = 1;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    // NOTE: When all graphics stages will be completed, the render semaphore
    // will be signaled.
    VkSemaphoreSubmitInfo signal_info = {};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = current_frame.render_semaphore;
    signal_info.value = 1;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkSubmitInfo2 submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.pCommandBufferInfos = &cmd_buffer_submit_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = &wait_info;
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_info;
    submit_info.signalSemaphoreInfoCount = 1;

    VK_ASSERT(vkQueueSubmit2(game_state->vk_context.queue, 1, &submit_info, current_frame.render_fence));

    // NOTE: Request presentation of the frame, waiting on the render semaphore.
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pSwapchains = &game_state->vk_context.swapchain;
    present_info.swapchainCount = 1;
    present_info.pWaitSemaphores = &current_frame.render_semaphore;
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_img_idx;

    VK_ASSERT(vkQueuePresentKHR(game_state->vk_context.queue, &present_info));

    game_state->vk_context.frames_counter++;

    /*
    // NOTE: Set the shared graphics pipeline stuff.
    RECT clientRect;
    GetClientRect(game_state->d3d_context.window, &clientRect);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = clientRect.right;
    viewport.Height = clientRect.bottom;
    viewport.MinDepth = 0.0;
    viewport.MaxDepth = 1.0;

    D3D12_RECT scissor = {};
    scissor.top = viewport.TopLeftY;
    scissor.bottom = viewport.Height;
    scissor.left = viewport.TopLeftX;
    scissor.right = viewport.Width;

    current_frame->command_list->RSSetViewports(1, &viewport);
    current_frame->command_list->RSSetScissorRects(1, &scissor);

    current_frame->command_list->OMSetRenderTargets(1, &current_frame->render_target_view_descriptor, FALSE, &current_frame->depth_target_view_descriptor);
    current_frame->command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // NOTE: Draw the chunks.
    if (game_state->is_wireframe) {
        current_frame->command_list->SetPipelineState(game_state->wireframe_render_pipeline.pipeline_state);
        current_frame->command_list->SetGraphicsRootSignature(game_state->wireframe_render_pipeline.root_signature);

        v4 wireframe_color = {1, 1, 1, 1};
        current_frame->command_list->SetGraphicsRoot32BitConstants(2, 4, wireframe_color.data, 0);
    } else {
        current_frame->command_list->SetPipelineState(game_state->chunk_render_pipeline.pipeline_state);
        current_frame->command_list->SetGraphicsRootSignature(game_state->chunk_render_pipeline.root_signature);
    }

    m4 fused_perspective_view = makeProjection(0.1, 1000, 90) * lookAt(game_state->player_position, game_state->player_position + game_state->camera_forward);
    current_frame->command_list->SetGraphicsRoot32BitConstants(1, 16, fused_perspective_view.data, 0);

    // TODO: We are recoring a draw call both for empty chunks and chunks outside the
    // player view... This needs culling !!
    for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {

        Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
        if (!chunk) continue;
        if (!chunk->vertices_count) continue;

        m4 chunk_model = makeTranslation(chunkToWorldPos(chunk->chunk_position));
        current_frame->command_list->SetGraphicsRoot32BitConstants(0, 16, chunk_model.data, 0);

        // NOTE: If the chunk has just been updated, it needs to be transitioned
        // back into a vertex buffer, from a copy destination buffer.
        if (!chunk->vbo_ready) {
            D3D12_RESOURCE_BARRIER to_vertex_buffer_barrier = {};
            to_vertex_buffer_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_vertex_buffer_barrier.Transition.pResource = chunk->vertex_buffer;
            to_vertex_buffer_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            to_vertex_buffer_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            to_vertex_buffer_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            current_frame->command_list->ResourceBarrier(1, &to_vertex_buffer_barrier);

            chunk->vbo_ready = true;
        }

        D3D12_VERTEX_BUFFER_VIEW vb_view;
        vb_view.BufferLocation = chunk->vertex_buffer->GetGPUVirtualAddress();
        vb_view.StrideInBytes = sizeof(ChunkVertex);
        vb_view.SizeInBytes = chunk->vertices_count * sizeof(ChunkVertex);

        current_frame->command_list->IASetVertexBuffers(0, 1, &vb_view);

        current_frame->command_list->DrawInstanced(chunk->vertices_count, 1, 0, 0);
    }

    // NOTE: Text rendering test.
    char debug_text_string[256];
    v3i chunk_position = worldPosToChunk(game_state->player_position);
    StringCbPrintf(debug_text_string, ARRAY_COUNT(debug_text_string),
                   "Pos: %.2f, %.2f, %.2f\nChunk: %d, %d, %d\n\nWorld: %d/%d\n\nPool: %d/%d",
                   game_state->player_position.x,
                   game_state->player_position.y,
                   game_state->player_position.z,
                   chunk_position.x,
                   chunk_position.y,
                   chunk_position.z,
                   game_state->world.nb_occupied,
                   HASHMAP_SIZE,
                   game_state->chunk_pool.nb_used,
                   CHUNK_POOL_SIZE
    );
    drawDebugTextOnScreen(&game_state->text_renderer, current_frame->command_list, debug_text_string, 0, 0);
    */

    clearArena(&game_state->frame_arena);
}
