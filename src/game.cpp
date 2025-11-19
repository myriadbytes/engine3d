#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <strsafe.h>

#include "common.h"
#include "game_api.h"
#include "maths.h"
#include "noise.h"
#include "gpu.h"
#include "world.h"

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

struct GameState {
    f32 time;
    RandomSeries random_series;
    SimplexTable simplex_table;

    VulkanContext vk_context;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;
    v3 camera_forward;
    b32 orbit_mode;

    WorldHashMap world;

    //D3DPipeline chunk_render_pipeline;
    //D3DPipeline wireframe_render_pipeline;

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

        initGPUBuddyAllocator(&game_state->vk_context);

        //initChunkMemoryPool(&game_state->chunk_pool, game_state->d3d_context.device);
        game_state->world.nb_occupied = 0;

        game_state->player_position = {110, 40, 110};
        game_state->orbit_mode = false;
        game_state->time = 0;
        game_state->camera_pitch = -1 * PI32 / 6;
        game_state->camera_yaw = 1 * PI32 / 3;
        game_state->random_series = 0xC0FFEE; // fixed seed for now
        simplex_table_from_seed(&game_state->simplex_table, 0xC0FFEE);

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

    clearArena(&game_state->frame_arena);
}
