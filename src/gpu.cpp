#include <strsafe.h>

#include "gpu.h"

struct GraphicsMemoryAllocatorConfig {
    // FIXME: The initialization will look for a memory type with
    // that EXACT COMBINATION OF FLAGS !! This sounds extremely
    // not portable with a different GPU from mine and I should
    // probably instead define vague "types" (VRAM, GPU-visible RAM)
    // and write a function find the best fit among all the memory
    // types. I think that's what VMA does ?
    VkMemoryPropertyFlags memory_properties;

    usize                 min_alloc_size;
    usize                 max_alloc_size;
    usize                 total_size;
};

b32 graphicsMemoryAllocatorInitialize(GraphicsMemoryAllocator* gpu_allocator, VkPhysicalDevice phys_device, VkDevice device, Arena* metadata_arena, GraphicsMemoryAllocatorConfig* config) {
    *gpu_allocator = {};

    // NOTE: Find which memory index to use.

    VkPhysicalDeviceMemoryProperties2 mem_props = {};
    mem_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vkGetPhysicalDeviceMemoryProperties2(phys_device, &mem_props);

    i32 memory_idx = -1;
    for (u32 type_idx = 0; type_idx < mem_props.memoryProperties.memoryTypeCount; type_idx++) {
        VkMemoryType& mem_type = mem_props.memoryProperties.memoryTypes[type_idx];

        #if ENGINE_SLOW
        char print_buffer[128];
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
        #endif

        if (mem_type.propertyFlags == config->memory_properties) {
            memory_idx = type_idx;
            break;
        }
    }
    ASSERT(memory_idx != -1);

    // NOTE: Do the actual allocation.
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = config->total_size;
    alloc_info.memoryTypeIndex = memory_idx;

    VK_ASSERT(vkAllocateMemory(device, &alloc_info, nullptr, &gpu_allocator->memory));

    // NOTE: Initialize the buddy allocator that will be used to manage the allocations
    buddyInitalize(
        &gpu_allocator->allocator,
        metadata_arena,
        config->min_alloc_size,
        config->max_alloc_size,
        config->total_size
    );

    // NOTE: If the memory can be permanantly mapped, map it.
    if (config->memory_properties ==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        |VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {

        VK_ASSERT(vkMapMemory(device, gpu_allocator->memory, 0, config->total_size, 0, (void**)&gpu_allocator->mapped));
    }

    // NOTE: Associate the allocator with a device. This makes
    // it easy to allocate memory without needing to specify it.
    gpu_allocator->device = device;

    return true;
}

AllocatedBuffer graphicsMemoryAllocateBuffer(GraphicsMemoryAllocator* gpu_allocator, usize desired_size, VkBufferUsageFlags usage) {
    AllocatedBuffer result = {};

    // NOTE: Allocate the memory.
    // TODO: Log the difference between the desired size
    // and the actual allocation size here, to get an idea
    // of the wasted memory depending on allocator config.
    BuddyAllocation alloc = buddyAlloc(&gpu_allocator->allocator, desired_size);
    ASSERT(alloc.size >= desired_size);

    result.alloc.alloc_size = alloc.size;
    result.alloc.alloc_offset = alloc.offset;
    if (gpu_allocator->mapped != nullptr) {
        result.alloc.mapped_data = gpu_allocator->mapped + alloc.offset;
    }

    // NOTE: Create the buffer object and check its requirements
    // against the allocation.
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = desired_size;
    buffer_info.usage = usage;

    VK_ASSERT(vkCreateBuffer(gpu_allocator->device, &buffer_info, nullptr, &result.buffer));

    VkMemoryRequirements memory_reqs;
    vkGetBufferMemoryRequirements(gpu_allocator->device, result.buffer, &memory_reqs);
    ASSERT(alloc.size >= memory_reqs.size);
    ASSERT(alloc.offset % memory_reqs.alignment == 0);
    // TODO: Check the memory type bits too.

    // NOTE: Bind the buffer with the memory.
    VK_ASSERT(vkBindBufferMemory(gpu_allocator->device, result.buffer, gpu_allocator->memory, alloc.offset));

    return result;
}

void graphicsMemoryFreeBuffer(GraphicsMemoryAllocator* gpu_allocator, AllocatedBuffer* allocated_buffer) {
    // NOTE: Do the allocation in reverse :
    // - Destroy the buffer
    // - Free the buddy alloc
    // - Clear the AllocatedBuffer so no one holds onto
    // stale references.    

    vkDestroyBuffer(gpu_allocator->device, allocated_buffer->buffer, nullptr);
    buddyFree(&gpu_allocator->allocator, allocated_buffer->alloc.alloc_offset);
    *allocated_buffer = {};
}

AllocatedImage graphicsMemoryAllocateImage(GraphicsMemoryAllocator* gpu_allocator, VkFormat img_format, u32 img_width, u32 img_height, VkImageUsageFlags usage) {
    AllocatedImage result = {};

    // NOTE: Create the image.
    VkImageCreateInfo img_info = {};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = img_format;
    img_info.extent = {img_width, img_height, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;

    VK_ASSERT(vkCreateImage(gpu_allocator->device, &img_info, nullptr, &result.image));

    // NOTE: Allocate the memory, checking the allocation meets the requirements.
    VkMemoryRequirements img_memory_reqs;
    vkGetImageMemoryRequirements(gpu_allocator->device, result.image, &img_memory_reqs);

    BuddyAllocation alloc = buddyAlloc(&gpu_allocator->allocator, img_memory_reqs.size);

    ASSERT(alloc.size >= img_memory_reqs.size);
    ASSERT(alloc.offset % img_memory_reqs.alignment == 0);

    result.alloc.alloc_offset = alloc.offset;
    result.alloc.alloc_size = alloc.size;
    // TODO: Should we set the alloc.mapped member here ? There's
    // nothing stopping us from doing so if the allocator's memory
    // has been mapped, but I don't know why I would use a memory-mapped
    // Vulkan image in Host memory.

    // NOTE: Bind the image to the allocation.
    VK_ASSERT(vkBindImageMemory(gpu_allocator->device, result.image, gpu_allocator->memory, alloc.offset));

    // NOTE: Create the image view. The aspect mask is deduced from the format.
    b32 is_depth_format = (img_format == VK_FORMAT_D32_SFLOAT);

    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = result.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = img_format;
    view_info.subresourceRange.aspectMask = is_depth_format ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VK_ASSERT(vkCreateImageView(gpu_allocator->device, &view_info, nullptr, &result.image_view));

    return result;
}

b32 initVulkan(Renderer* to_init, b32 debug_mode, Arena* scratch_arena) {
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

    return true;
}

b32 initSwapchain(Renderer* to_init, Arena* scratch_arena) {
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
    VkImage swapchain_images[FRAMES_IN_FLIGHT];
    vkGetSwapchainImagesKHR(to_init->device, to_init->swapchain, &swapchain_images_count, swapchain_images);

    // NOTE: For each frame, associate the VkImage and create the VkImageView.
    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        
        to_init->frames[frame_idx].swapchain_image = swapchain_images[frame_idx];

        VkImageViewCreateInfo swapchain_view_info = {};
        swapchain_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        swapchain_view_info.image = to_init->frames[frame_idx].swapchain_image;
        swapchain_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        swapchain_view_info.format = VK_FORMAT_B8G8R8A8_SRGB;
        swapchain_view_info.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        vkCreateImageView(to_init->device, &swapchain_view_info, nullptr, &to_init->frames[frame_idx].swapchain_image_view);
    }

    return true;   
}

b32 initCmdAndSync(Renderer* to_init) {
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

b32 initAllocation(Renderer* to_init, Arena* static_arena) {
    GraphicsMemoryAllocatorConfig large_vram_config = {};
    large_vram_config.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    large_vram_config.min_alloc_size = KILOBYTES(32);
    large_vram_config.max_alloc_size = MEGABYTES(4);
    large_vram_config.total_size = MEGABYTES(256);

    graphicsMemoryAllocatorInitialize(
        &to_init->vram_allocator,
        to_init->physical_device,
        to_init->device,
        static_arena,
        &large_vram_config
    );

    GraphicsMemoryAllocatorConfig small_ram_config = {};
    small_ram_config.memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    small_ram_config.min_alloc_size = BYTES(4);
    small_ram_config.max_alloc_size = BYTES(64);
    small_ram_config.total_size = KILOBYTES(1);

    graphicsMemoryAllocatorInitialize(
        &to_init->host_allocator,
        to_init->physical_device,
        to_init->device,
        static_arena,
        &small_ram_config
    );

    GraphicsMemoryAllocatorConfig staging_ram_config = {};
    staging_ram_config.memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    staging_ram_config.min_alloc_size = MEGABYTES(1);
    staging_ram_config.max_alloc_size = MEGABYTES(8);
    staging_ram_config.total_size = MEGABYTES(128);

    graphicsMemoryAllocatorInitialize(
        &to_init->staging_allocator,
        to_init->physical_device,
        to_init->device,
        static_arena,
        &staging_ram_config
    );

    return true;
}

b32 initDepth(Renderer* to_init) {
    RECT client_rect;
    GetClientRect(to_init->window, &client_rect);

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        VkImage& frame_depth_img = to_init->frames[frame_idx].depth_img;
        VkImageView& frame_depth_view = to_init->frames[frame_idx].depth_view;

        // NOTE: Create the image.
        VkImageCreateInfo img_info = {};
        img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = VK_FORMAT_D32_SFLOAT;
        img_info.extent = {(u32)client_rect.right, (u32)client_rect.bottom, 1};
        img_info.mipLevels = 1;
        img_info.arrayLayers = 1;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        VK_ASSERT(vkCreateImage(to_init->device, &img_info, nullptr, &frame_depth_img));

        // NOTE: Allocate the memory, checking the allocation meets the requirements.
        VkMemoryRequirements img_memory_reqs;
        vkGetImageMemoryRequirements(to_init->device, frame_depth_img, &img_memory_reqs);

        BuddyAllocation allocation = buddyAlloc(&to_init->vram_allocator.allocator, img_memory_reqs.size);
        ASSERT(allocation.size >= img_memory_reqs.size);
        ASSERT(allocation.offset % img_memory_reqs.alignment == 0);

        VK_ASSERT(vkBindImageMemory(to_init->device, frame_depth_img, to_init->vram_allocator.memory, allocation.offset));

        // NOTE: Create the image view for rendering.
        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = frame_depth_img;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_D32_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_ASSERT(vkCreateImageView(to_init->device, &view_info, nullptr, &frame_depth_view));
    }

    return true;
}

b32 initDescPool(Renderer* to_init) {
    constexpr u32 MAX_SETS = 32;
    constexpr u32 MAX_UNIFORMS = 32;
    constexpr u32 MAX_IMG_SAMPLERS = 32;

    // NOTE: Create the descriptor pool for:
    // - uniform buffers
    // - texture samplers
    VkDescriptorPoolSize uniform_pool_sizes[2] = {};
    uniform_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniform_pool_sizes[0].descriptorCount = MAX_UNIFORMS;
    uniform_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    uniform_pool_sizes[1].descriptorCount = MAX_IMG_SAMPLERS;

    VkDescriptorPoolCreateInfo uniform_pool_info = {};
    uniform_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    uniform_pool_info.pPoolSizes = uniform_pool_sizes;
    uniform_pool_info.poolSizeCount = ARRAY_COUNT(uniform_pool_sizes);
    uniform_pool_info.maxSets = MAX_SETS;
    VK_ASSERT(vkCreateDescriptorPool(to_init->device, &uniform_pool_info, nullptr, &to_init->global_desc_pool));
    
    return true;
}

b32 initStaging(Renderer* to_init) {

    for (AllocatedBuffer& staging_buffer : to_init->staging_buffers) {
        staging_buffer = graphicsMemoryAllocateBuffer(&to_init->staging_allocator, STAGING_BUFFER_MIN_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);        
    }

    return true;  
};

// TODO: Automatic debug mode based on internal / release build.
b32 rendererInitialize(Renderer* to_init, b32 debug_mode, Arena* static_arena, Arena* scratch_arena) {
    *to_init = {};

    ASSERT(initVulkan(to_init, debug_mode, scratch_arena));
    ASSERT(initSwapchain(to_init, scratch_arena));
    ASSERT(initCmdAndSync(to_init));
    ASSERT(initAllocation(to_init, static_arena));
    ASSERT(initDepth(to_init));
    ASSERT(initDescPool(to_init));
    ASSERT(initStaging(to_init));

    return true;
}

AllocatedBuffer* rendererRequestStagingBuffer(Renderer* renderer) {
    if(renderer->distributed_staging_buffers >= STAGING_BUFFERS_PER_FRAME) {
        return nullptr;
    }

    // FIXME: We assume that by the time we get to doing CPU work for
    // a swapchain image index, its previously submitted work has completed.
    // This is actually NOT TRUE ! We only call vkWaitForFences when we
    // begin recording commands, but all CPU work before that might happen
    // concurrently while the frame's resources are still used by the GPU.
    // For now, I think I will just try to write to staging buffers only
    // AFTER that vkWaitForFences call.
    u32 current_frame = renderer->frames_counter % FRAMES_IN_FLIGHT;

    AllocatedBuffer* result = &renderer->staging_buffers[current_frame * STAGING_BUFFERS_PER_FRAME + renderer->distributed_staging_buffers];
    renderer->distributed_staging_buffers++;

    return result;
}

VkDeviceMemory debugAllocateDirectGPUMemory(Renderer* vk_context, VkMemoryPropertyFlags memory_properties, usize size) {
    VkPhysicalDeviceMemoryProperties2 mem_props = {};
    mem_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vkGetPhysicalDeviceMemoryProperties2(vk_context->physical_device, &mem_props);

    i32 memory_idx = -1;
    for (u32 type_idx = 0; type_idx < mem_props.memoryProperties.memoryTypeCount; type_idx++) {
        VkMemoryType& mem_type = mem_props.memoryProperties.memoryTypes[type_idx];
        if (mem_type.propertyFlags == memory_properties) {
            memory_idx = type_idx;
            break;
        }
    }
    ASSERT(memory_idx != -1);

    // NOTE: Do the actual allocation.
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = size;
    alloc_info.memoryTypeIndex = memory_idx;

    VkDeviceMemory result;
    VK_ASSERT(vkAllocateMemory(vk_context->device, &alloc_info, nullptr, &result));

    return result;
}


// TODO: This should handle errors gracefully. Maybe a default shader enbedded in the exe ?
VkShaderModule loadAndCreateShader(Renderer* vk_context, const char* path, Arena* scratch_arena) {
    // FIXME : write a function to query the exe's directory instead of forcing a specific CWD
    wchar_t windows_path[128];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, windows_path, ARRAY_COUNT(windows_path));
    
    
    HANDLE file_handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT(file_handle != INVALID_HANDLE_VALUE);

    // WARN: This crashes for shader files larger than 4GB...
    // I think we're safe !
    DWORD file_size_high = 0;
    DWORD file_size = GetFileSize(file_handle, &file_size_high);
    ASSERT(file_size_high == 0);

    u8* shader_bytes = (u8*)pushBytes(scratch_arena, file_size);
    DWORD bytes_read;
    ReadFile(file_handle, (void*)shader_bytes, file_size, &bytes_read, NULL);
    ASSERT(bytes_read == file_size);
    CloseHandle(file_handle);

    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = file_size;
    create_info.pCode = (const u32*)shader_bytes;

    VkShaderModule result;
    VK_ASSERT(vkCreateShaderModule(vk_context->device, &create_info, nullptr, &result));

    return result;
}

constexpr u32 BUILDER_VRTX_STAGE_ID = 0;
constexpr u32 BUILDER_FRAG_STAGE_ID = 1;

void pipelineBuilderInitialize(VulkanPipelineBuilder* builder) {
    *builder = {};

    // FIXME: Have a default shader here to prevent crashes
    // on bad pipeline creation.
    builder->shader_stages[BUILDER_VRTX_STAGE_ID].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    builder->shader_stages[BUILDER_VRTX_STAGE_ID].stage = VK_SHADER_STAGE_VERTEX_BIT;
    builder->shader_stages[BUILDER_VRTX_STAGE_ID].module = nullptr;
    builder->shader_stages[BUILDER_VRTX_STAGE_ID].pName = "main";
    builder->shader_stages[BUILDER_FRAG_STAGE_ID].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    builder->shader_stages[BUILDER_FRAG_STAGE_ID].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    builder->shader_stages[BUILDER_FRAG_STAGE_ID].module = nullptr;
    builder->shader_stages[BUILDER_FRAG_STAGE_ID].pName = "main";

    // NOTE: Would be extended by adding vertex buffer / attribute info.
    builder->input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    builder->input_info.pVertexBindingDescriptions = builder->vertex_input_bindings_info;
    builder->input_info.vertexBindingDescriptionCount = 0;
    builder->input_info.pVertexAttributeDescriptions = builder->vertex_input_attributes_info;
    builder->input_info.vertexAttributeDescriptionCount = 0;

    builder->vertex_input_binding_count = 0;
    builder->vertex_input_attributes_count = 0;

    // NOTE: Defaults to triangle list.
    builder->assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    builder->assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // NOTE: Will never be used.
    builder->tessellation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;

    // NOTE: All pipelines will use dynamic state for the viewport and scissor.
    builder->viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    builder->viewport_info.viewportCount = 1;
    builder->viewport_info.scissorCount = 1;

    // NOTE: Defaults to:
    // - No culling
    // - Fill mode.
    builder->raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    builder->raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    builder->raster_info.cullMode = VK_CULL_MODE_NONE;
    builder->raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    builder->raster_info.lineWidth = 1.0f;

    // NOTE: Not used.
    builder->multisample_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    builder->multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // NOTE: Defaults to no depth testing.
    builder->depth_stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    builder->depth_stencil_info.depthTestEnable = VK_FALSE;
    builder->depth_stencil_info.depthWriteEnable = VK_FALSE;
    builder->depth_stencil_info.stencilTestEnable = VK_FALSE;

    // NOTE: Defaults to no blending.
    builder->blend_attachments[0].blendEnable = VK_FALSE;
    builder->blend_attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                                  | VK_COLOR_COMPONENT_G_BIT
                                                  | VK_COLOR_COMPONENT_B_BIT
                                                  | VK_COLOR_COMPONENT_A_BIT;

    builder->blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    builder->blend_info.logicOpEnable = VK_FALSE;
    builder->blend_info.logicOp = VK_LOGIC_OP_COPY;
    builder->blend_info.pAttachments = builder->blend_attachments;
    builder->blend_info.attachmentCount = 1;

    // NOTE: Viewport and scissor is dynamic for all pipelines.
    builder->dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
    builder->dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;

    builder->dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    builder->dynamic_state_info.pDynamicStates = builder->dynamic_states;
    builder->dynamic_state_info.dynamicStateCount = ARRAY_COUNT(builder->dynamic_states);

    // NOTE: Defaults to those values. Maybe it should be more flexible
    // because I have no idea if they are supported on all GPUs. But this
    // is an engine-wide issue, not just for this builder.
    builder->color_attachment_format = VK_FORMAT_B8G8R8A8_SRGB;
    builder->depth_attachment_format = VK_FORMAT_D32_SFLOAT;

    builder->rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    builder->rendering_info.colorAttachmentCount = 1;
    builder->rendering_info.pColorAttachmentFormats = &builder->color_attachment_format;
    builder->rendering_info.depthAttachmentFormat = builder->depth_attachment_format;

    // NOTE: Just set the sType, everything else will be handled during creation.
    for (u32 set_idx = 0; set_idx < PIPELINES_MAX_SETS; set_idx++) {
        builder->sets_info[set_idx].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    }
}

void pipelineBuilderSetVertexShader(VulkanPipelineBuilder* builder, VkShaderModule shader) {
    ASSERT(shader != nullptr);

    builder->shader_stages[BUILDER_VRTX_STAGE_ID].module = shader;
}

void pipelineBuilderSetFragmentShader(VulkanPipelineBuilder* builder, VkShaderModule shader) {
    ASSERT(shader != nullptr);

    builder->shader_stages[BUILDER_FRAG_STAGE_ID].module = shader;
}

void pipelineBuilderEnableAlphaBlending(VulkanPipelineBuilder* builder) {
    builder->blend_attachments[0].blendEnable = VK_TRUE;
    builder->blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    builder->blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    builder->blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    builder->blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    builder->blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    builder->blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
}

void pipelineBuilderEnableBackfaceCulling(VulkanPipelineBuilder* builder) {
    builder->raster_info.cullMode = VK_CULL_MODE_BACK_BIT;
}

void pipelineBuilderEnableDepth(VulkanPipelineBuilder* builder) {
    builder->depth_stencil_info.depthTestEnable = VK_TRUE;
    builder->depth_stencil_info.depthWriteEnable = VK_TRUE;
    builder->depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS;
}

void pipelineBuilderAddImageSampler(VulkanPipelineBuilder* builder) {
    ASSERT(builder->current_desc_binding < BUILDER_MAX_DESC_PER_SET);

    u32 binding_idx = builder->current_desc_set * BUILDER_MAX_DESC_PER_SET + builder->current_desc_binding;

    builder->shader_bindings[binding_idx].binding = builder->current_desc_binding;
    builder->shader_bindings[binding_idx].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    builder->shader_bindings[binding_idx].descriptorCount = 1;
    // NOTE: Assume that samplers will only be sampled from in fragment shaders for now.
    builder->shader_bindings[binding_idx].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    builder->sets_bindings_count[builder->current_desc_set]++;
    builder->current_desc_binding++;
}

void pipelineBuilderAddUniformBuffer(VulkanPipelineBuilder* builder, VkShaderStageFlags stage) {
    ASSERT(builder->current_desc_binding < BUILDER_MAX_DESC_PER_SET);

    u32 binding_idx = builder->current_desc_set * BUILDER_MAX_DESC_PER_SET + builder->current_desc_binding;

    builder->shader_bindings[binding_idx].binding = builder->current_desc_binding;
    builder->shader_bindings[binding_idx].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    builder->shader_bindings[binding_idx].descriptorCount = 1;
    builder->shader_bindings[binding_idx].stageFlags = stage;

    builder->sets_bindings_count[builder->current_desc_set]++;
    builder->current_desc_binding++;
}

void pipelineBuilderAddPushConstant(VulkanPipelineBuilder* builder, usize size, VkShaderStageFlags stages) {
    // TODO: We would need two ranges if both the vertex and fragment
    // shader have push constants. For now we just assert that all push
    // constants have the same stage flags.
    ASSERT(!builder->push_constants.stageFlags || (builder->push_constants.stageFlags == stages));

    builder->push_constants.offset = 0;
    builder->push_constants.size += size;
    builder->push_constants.stageFlags = stages;
}

void pipelineBuilderAddVertexInputBinding(VulkanPipelineBuilder* builder, usize stride) {
    ASSERT(builder->vertex_input_binding_count < BUILDER_MAX_VERTEX_BINDINGS);

    builder->vertex_input_bindings_info[builder->vertex_input_binding_count].binding = builder->vertex_input_binding_count;
    builder->vertex_input_bindings_info[builder->vertex_input_binding_count].stride = stride;
    builder->vertex_input_bindings_info[builder->vertex_input_binding_count].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    builder->vertex_input_binding_count++;
}

void pipelineBuilderAddVertexAttribute(VulkanPipelineBuilder* builder, VkFormat format, usize offset) {
    ASSERT(builder->vertex_input_attributes_count < BUILDER_MAX_VERTEX_ATTRIBUTES);

    builder->vertex_input_attributes_info[builder->vertex_input_attributes_count].binding = builder->vertex_input_binding_count-1;
    builder->vertex_input_attributes_info[builder->vertex_input_attributes_count].location = builder->vertex_input_attributes_count;
    builder->vertex_input_attributes_info[builder->vertex_input_attributes_count].format = format;
    builder->vertex_input_attributes_info[builder->vertex_input_attributes_count].offset = offset;

    builder->vertex_input_attributes_count++;
}

void pipelineBuilderCreatePipeline(VulkanPipelineBuilder* builder, VkDevice device, VulkanPipeline* to_create) {
    *to_create = {};

    // NOTE: Update the vertex bindings & attributes number.
    builder->input_info.vertexBindingDescriptionCount = builder->vertex_input_binding_count;
    builder->input_info.vertexAttributeDescriptionCount= builder->vertex_input_attributes_count;

    // NOTE: Set up all the descriptor sets and create their layouts.
    for (u32 set_idx = 0; set_idx <= builder->current_desc_set; set_idx++) {
        builder->sets_info[set_idx].pBindings = &builder->shader_bindings[set_idx*BUILDER_MAX_DESC_PER_SET];
        builder->sets_info[set_idx].bindingCount = builder->sets_bindings_count[set_idx];

        VK_ASSERT(vkCreateDescriptorSetLayout(device, &builder->sets_info[set_idx], nullptr, &to_create->desc_sets_layouts[set_idx]));
    }
    
    // NOTE: Create the pipeline layout.
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pSetLayouts = to_create->desc_sets_layouts;
    pipeline_layout_info.setLayoutCount = builder->current_desc_set+1;
    pipeline_layout_info.pPushConstantRanges = &builder->push_constants;
    pipeline_layout_info.pushConstantRangeCount = 1;

    VK_ASSERT(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &to_create->layout));

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &builder->rendering_info;
    pipeline_info.pStages = builder->shader_stages;
    pipeline_info.stageCount = ARRAY_COUNT(builder->shader_stages);
    pipeline_info.pVertexInputState = &builder->input_info;
    pipeline_info.pInputAssemblyState = &builder->assembly_info;
    pipeline_info.pTessellationState = &builder->tessellation_info;
    pipeline_info.pViewportState = &builder->viewport_info;
    pipeline_info.pRasterizationState = &builder->raster_info;
    pipeline_info.pMultisampleState = &builder->multisample_info;
    pipeline_info.pDepthStencilState = &builder->depth_stencil_info;
    pipeline_info.pColorBlendState = &builder->blend_info;
    pipeline_info.pDynamicState = &builder->dynamic_state_info;
    pipeline_info.layout = to_create->layout;

    VK_ASSERT(vkCreateGraphicsPipelines(device, nullptr, 1, &pipeline_info, nullptr, &to_create->pipeline));
}
