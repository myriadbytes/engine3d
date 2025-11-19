#include <strsafe.h>

#include "gpu.h"

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
    OutputDebugStringA("\n");

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

void initGPUBuddyAllocator(VulkanContext* vk_context) {
    // NOTE: Find which memory index to use.

    VkPhysicalDeviceMemoryProperties2 mem_props = {};
    mem_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vkGetPhysicalDeviceMemoryProperties2(vk_context->physical_device, &mem_props);

    for (u32 heap_idx = 0; heap_idx < mem_props.memoryProperties.memoryHeapCount; heap_idx++) {
        char print_buffer[128];
        VkMemoryHeap& heap = mem_props.memoryProperties.memoryHeaps[heap_idx];
        StringCbPrintfA(
            print_buffer,
            ARRAY_COUNT(print_buffer),
            "Memory Heap #%u: %llu bytes %s\n",
            heap_idx,
            heap.size,
            heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? "(VRAM)" : ""
        );
        OutputDebugStringA(print_buffer);
    }

    i32 vram_memory_idx = -1;
    for (u32 type_idx = 0; type_idx < mem_props.memoryProperties.memoryTypeCount; type_idx++) {
        char print_buffer[128];
        VkMemoryType& mem_type = mem_props.memoryProperties.memoryTypes[type_idx];
        StringCbPrintfA(
            print_buffer,
            ARRAY_COUNT(print_buffer),
            "Memory Type #%u: Heap #%u %s%s\n",
            type_idx,
            mem_type.heapIndex,
            mem_type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? "| VRAM " : "",
            mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? "| HOST VISIBLE " : ""
        );
        OutputDebugStringA(print_buffer);

        if ((mem_type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        && !(mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            vram_memory_idx = mem_type.heapIndex;
        }
    }
    ASSERT(vram_memory_idx != -1);

    // NOTE: Do the actual allocation.
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = MEGABYTES(512);
    alloc_info.memoryTypeIndex = vram_memory_idx;

    VkDeviceMemory memory;
    VK_ASSERT(vkAllocateMemory(vk_context->device, &alloc_info, nullptr, &memory));
}
