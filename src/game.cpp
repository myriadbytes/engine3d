#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <strsafe.h>

#include "common.h"
#include "containers.h"
#include "img.h"
#include "game_api.h"
#include "maths.h"
#include "noise.h"
#include "gpu.h"
#include "world.h"

struct TextRenderingState {
    b32 is_initialized;

    VulkanPipeline text_pipeline;

    VkImage bitmap_font;
    VkImageView bitmap_font_view;

    // NOTE: Contains just a combined image+sampler
    // for the font texture.
    VkDescriptorSet descriptor_set;
};

void textRenderingInitialize(TextRenderingState* text_rendering_state, Renderer* renderer, Arena* scratch_arena) {
    // NOTE: Create the pipeline.
    VkShaderModule text_vert_shader = loadAndCreateShader(renderer, "./shaders/bitmap_text.vert.spv", scratch_arena);
    VkShaderModule text_frag_shader = loadAndCreateShader(renderer, "./shaders/bitmap_text.frag.spv", scratch_arena);

    VulkanPipelineBuilder pipeline_builder = {};
    pipelineBuilderInitialize(&pipeline_builder);

    pipelineBuilderSetVertexShader(&pipeline_builder, text_vert_shader);
    pipelineBuilderSetFragmentShader(&pipeline_builder, text_frag_shader);

    pipelineBuilderEnableAlphaBlending(&pipeline_builder);

    // NOTE: 1 Sampler for the bitmap font.
    pipelineBuilderAddImageSampler(&pipeline_builder);

    // NOTE: 2 push constants:
    // - transform matrix
    // - char codepoint
    pipelineBuilderAddPushConstant(&pipeline_builder, sizeof(m4), VK_SHADER_STAGE_VERTEX_BIT);
    pipelineBuilderAddPushConstant(&pipeline_builder, sizeof(u32), VK_SHADER_STAGE_VERTEX_BIT);

    pipelineBuilderCreatePipeline(&pipeline_builder, renderer->device, &text_rendering_state->text_pipeline);

    vkDestroyShaderModule(renderer->device, text_vert_shader, nullptr);
    vkDestroyShaderModule(renderer->device, text_frag_shader, nullptr);

    // NOTE: Read the bitmap font png, and create a texture handle for it.
    u32 bitmap_width, bitmap_height;
    u8* bitmap_bytes = read_image("./assets/monogram-bitmap.png", &bitmap_width, &bitmap_height, scratch_arena, scratch_arena);

    auto image_allocation = graphicsMemoryAllocateImage(
        &renderer->vram_allocator,
        VK_FORMAT_R8G8B8A8_SRGB,
        bitmap_width,
        bitmap_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
     );

    text_rendering_state->bitmap_font = image_allocation.image;
    text_rendering_state->bitmap_font_view = image_allocation.image_view;

    // NOTE: Get a staging buffer, and write the image bytes to it.
    AllocatedBuffer* staging_buffer = rendererRequestStagingBuffer(renderer);
    ASSERT(staging_buffer != nullptr);
    ASSERT(staging_buffer->alloc.alloc_size >= bitmap_width * bitmap_height * 4);

    for (u32 i = 0; i < bitmap_width * bitmap_height * 4; i++) {
        staging_buffer->alloc.mapped_data[i] = bitmap_bytes[i];
    }

    // NOTE: Get the current frame's command buffer to record an upload
    // for the bitmap font image. The command buffer better be ready to
    // record, otherwise this will crash ! This will be solved once we
    // have an upload queue (the data structure, not necessarily the
    // Vulkan thing).
    VkCommandBuffer& cmd_buf = renderer->frames[renderer->frames_counter % FRAMES_IN_FLIGHT].cmd_buffer;

    // NOTE: Transition the image into a layout optimal for transfer.
    // There is nothing to wait on for the first synchronization scope,
    // and the operations that need to wait on the transition are the
    // transfer write when we're going to upload the image.
    VkImageMemoryBarrier2 transfer_dst_barrier = {};
    transfer_dst_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    transfer_dst_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    transfer_dst_barrier.srcAccessMask = VK_ACCESS_2_NONE;
    transfer_dst_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
    transfer_dst_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
    transfer_dst_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    transfer_dst_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transfer_dst_barrier.subresourceRange = VkImageSubresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    transfer_dst_barrier.image = text_rendering_state->bitmap_font;

    VkDependencyInfo transfer_dst_dep_info = {};
    transfer_dst_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    transfer_dst_dep_info.imageMemoryBarrierCount = 1;
    transfer_dst_dep_info.pImageMemoryBarriers = &transfer_dst_barrier;

    vkCmdPipelineBarrier2(cmd_buf, &transfer_dst_dep_info);

    // NOTE: Record the actual copy command.
    VkBufferImageCopy copy_region = {};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageExtent = {bitmap_width, bitmap_height, 1};

    vkCmdCopyBufferToImage(cmd_buf, staging_buffer->buffer, text_rendering_state->bitmap_font, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    // NOTE: Transition the image into a layout optimal for sampling.
    // We wait for the transfer to have finished before transitioning,
    // and any shader read of that image has to wait for the transition.
    VkImageMemoryBarrier2 shader_read_barrier = {};
    shader_read_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    shader_read_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
    shader_read_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
    shader_read_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    shader_read_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    shader_read_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    shader_read_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shader_read_barrier.subresourceRange = VkImageSubresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    shader_read_barrier.image = text_rendering_state->bitmap_font;

    VkDependencyInfo shader_read_dep_info = {};
    shader_read_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    shader_read_dep_info.imageMemoryBarrierCount = 1;
    shader_read_dep_info.pImageMemoryBarriers = &shader_read_barrier;

    vkCmdPipelineBarrier2(cmd_buf, &shader_read_dep_info);

    // NOTE: Now we need to allocate a descriptor set for the texture.
    // There's only one and it is constant tho, so this won't be that
    // much of a bother.

    VkDescriptorSetAllocateInfo set_alloc_info = {};
    set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc_info.descriptorPool = renderer->global_desc_pool;
    set_alloc_info.pSetLayouts = text_rendering_state->text_pipeline.desc_sets_layouts;
    set_alloc_info.descriptorSetCount = 1;
    VK_ASSERT(vkAllocateDescriptorSets(renderer->device, &set_alloc_info, &text_rendering_state->descriptor_set));

    // NOTE: Write the sampler + image to the descriptor set. This only needs
    // to happen once since the things that change at runtime are push constants.
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSampler font_sampler; // WARNING: The sampler is leaked after this function !
    VK_ASSERT(vkCreateSampler(renderer->device, &sampler_info, nullptr, &font_sampler));

    VkDescriptorImageInfo desc_image_info = {};
    desc_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    desc_image_info.imageView = text_rendering_state->bitmap_font_view;
    desc_image_info.sampler = font_sampler;

    VkWriteDescriptorSet desc_write = {};
    desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    desc_write.dstSet = text_rendering_state->descriptor_set;
    desc_write.dstBinding = 0;
    desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    desc_write.descriptorCount = 1;
    desc_write.pImageInfo = &desc_image_info;

    vkUpdateDescriptorSets(renderer->device, 1, &desc_write, 0, nullptr);

    text_rendering_state->is_initialized = true;
}

struct TextShaderPushConstants {
    m4 transform;
    u32 char_codepoint;
};

// TODO: Switch away from null-terminated strings.
// NOTE: The debug text is drawn on a terminal-like grid, using a monospace font.
void drawDebugTextOnScreen(TextRenderingState* text_rendering_state, VkCommandBuffer cmd_buf, const char* text, u32 start_row, u32 start_col) {

    // NOTE: Set up the pipeline.
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, text_rendering_state->text_pipeline.pipeline);
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, text_rendering_state->text_pipeline.layout, 0, 1, &text_rendering_state->descriptor_set, 0, 0);

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
        makeTranslation((f32)start_col * char_width, (f32)start_row * char_height, 0)
        * makeTranslation(-(1 - char_width), -1 + char_height / 2, 0)
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

        m4 char_translate = makeTranslation((f32)row * char_width, (f32)col * char_height , 0);
        m4 char_matrix = char_translate * quad_setup_matrix;

        // TODO: Implement text wrapping here.
        row++;

        TextShaderPushConstants push_constants = {};
        push_constants.transform = char_matrix;
        push_constants.char_codepoint = char_ascii_codepoint;

        vkCmdPushConstants(cmd_buf, text_rendering_state->text_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TextShaderPushConstants), &push_constants);
        vkCmdDraw(cmd_buf, 6, 1, 0, 0);
    }
}

void chunkPipelineInitialize(Renderer* renderer, VulkanPipeline* to_create, Arena* scratch_arena) {
    VkShaderModule chunk_vert_shader = loadAndCreateShader(renderer, "./shaders/debug_chunk.vert.spv", scratch_arena);
    VkShaderModule chunk_frag_shader = loadAndCreateShader(renderer, "./shaders/debug_chunk.frag.spv", scratch_arena);

    VulkanPipelineBuilder builder = {};
    pipelineBuilderInitialize(&builder);

    pipelineBuilderSetVertexShader(&builder, chunk_vert_shader);
    pipelineBuilderSetFragmentShader(&builder, chunk_frag_shader);
    pipelineBuilderEnableBackfaceCulling(&builder);
    pipelineBuilderEnableDepth(&builder);

    // NOTE: Add a vertex buffer and two vec3 for position and normals.
    pipelineBuilderAddVertexInputBinding(&builder, sizeof(ChunkVertex));
    pipelineBuilderAddVertexAttribute(&builder, VK_FORMAT_R32G32B32_SFLOAT, 0);
    pipelineBuilderAddVertexAttribute(&builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));

    // NOTE: Two uniform buffers for the view and proj matrices.
    // The model matrix is handled with a push constant so we don't have
    // to have too much descriptor set updating.
    pipelineBuilderAddUniformBuffer(&builder, VK_SHADER_STAGE_VERTEX_BIT);
    pipelineBuilderAddUniformBuffer(&builder, VK_SHADER_STAGE_VERTEX_BIT);
    pipelineBuilderAddPushConstant(&builder, sizeof(m4), VK_SHADER_STAGE_VERTEX_BIT);

    pipelineBuilderCreatePipeline(&builder, renderer->device, to_create);
    
    vkDestroyShaderModule(renderer->device, chunk_vert_shader, nullptr);
    vkDestroyShaderModule(renderer->device, chunk_frag_shader, nullptr);
}

struct GameState {
    f32 time;
    RandomSeries random_series;
    SimplexTable simplex_table;

    Renderer renderer;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;
    v3 camera_forward;
    b32 orbit_mode;

    Hashmap<Chunk, v3i, WORLD_HASHMAP_SIZE> world_hashmap;
    Pool<Chunk, CHUNK_POOL_SIZE> chunk_pool;

    VulkanPipeline chunk_render_pipeline;
    // VulkanPipeline wireframe_render_pipeline;

    AllocatedBuffer view_matrix_uniforms[FRAMES_IN_FLIGHT];
    AllocatedBuffer projection_matrix_uniforms[FRAMES_IN_FLIGHT];

    VkDescriptorSet matrices_desc_sets[FRAMES_IN_FLIGHT];

    TextRenderingState text_rendering_state;

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

        ASSERT(rendererInitialize(
            &game_state->renderer,
            true,
            &game_state->static_arena,
            &game_state->frame_arena)
        );

        game_state->player_position = {110, 40, 110};
        game_state->orbit_mode = false;
        game_state->time = 0;
        game_state->camera_pitch = -1 * PI32 / 6;
        game_state->camera_yaw = 1 * PI32 / 3;
        game_state->random_series = 0xC0FFEE; // fixed seed for now
        simplex_table_from_seed(&game_state->simplex_table, 0xC0FFEE);

        chunkPipelineInitialize(&game_state->renderer, &game_state->chunk_render_pipeline, &game_state->frame_arena);

        // NOTE: For each frame, create 2 uniforms the view and projection matrices.
        // Also create a descriptor set that will point to that frame's uniforms.
        for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
            game_state->view_matrix_uniforms[frame_idx] =
                graphicsMemoryAllocateBuffer(&game_state->renderer.host_allocator, sizeof(m4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

            game_state->projection_matrix_uniforms[frame_idx] =
                graphicsMemoryAllocateBuffer(&game_state->renderer.host_allocator, sizeof(m4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

            VkDescriptorSetAllocateInfo set_alloc_info = {};
            set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            set_alloc_info.descriptorPool = game_state->renderer.global_desc_pool;
            set_alloc_info.descriptorSetCount = 1;
            set_alloc_info.pSetLayouts = &game_state->chunk_render_pipeline.desc_sets_layouts[0];
            VK_ASSERT(vkAllocateDescriptorSets(game_state->renderer.device, &set_alloc_info, &game_state->matrices_desc_sets[frame_idx]));

            VkDescriptorBufferInfo buffer_desc_infos[2];
            buffer_desc_infos[0].buffer = game_state->view_matrix_uniforms[frame_idx].buffer;
            buffer_desc_infos[0].offset = 0;
            buffer_desc_infos[0].range = sizeof(m4);
            buffer_desc_infos[1].buffer = game_state->projection_matrix_uniforms[frame_idx].buffer;
            buffer_desc_infos[1].offset = 0;
            buffer_desc_infos[1].range = sizeof(m4);
            
            VkWriteDescriptorSet set_writes[2] = {};
            set_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            set_writes[0].dstSet = game_state->matrices_desc_sets[frame_idx];
            set_writes[0].dstBinding = 0;
            set_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            set_writes[0].descriptorCount = 1;
            set_writes[0].pBufferInfo = &buffer_desc_infos[0];
            set_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            set_writes[1].dstSet = game_state->matrices_desc_sets[frame_idx];
            set_writes[1].dstBinding = 1;
            set_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            set_writes[1].descriptorCount = 1;
            set_writes[1].pBufferInfo = &buffer_desc_infos[1];

            vkUpdateDescriptorSets(game_state->renderer.device, ARRAY_COUNT(set_writes), set_writes, 0, nullptr);
        }

        hashmapInitialize(&game_state->world_hashmap, chunkPositionHash);
        poolInitialize(&game_state->chunk_pool);

        memory->is_initialized = true;
    }

    // SIMULATION
    game_state->time += dt;

    f32 mouse_sensitivity = 0.01;
    f32 stick_sensitivity = 0.05;

    game_state->camera_pitch += input->kb.mouse_delta.y() * mouse_sensitivity;
    game_state->camera_yaw += input->kb.mouse_delta.x() * mouse_sensitivity;

    game_state->camera_pitch += input->ctrl.right_stick.y() * stick_sensitivity;
    game_state->camera_yaw += input->ctrl.right_stick.x() * stick_sensitivity;

    f32 safe_pitch = PI32 / 2.f - 0.05f;
    game_state->camera_pitch = clamp(game_state->camera_pitch, -safe_pitch, safe_pitch);

    game_state->camera_forward.x() = cosf(game_state->camera_yaw) * cosf(game_state->camera_pitch);
    game_state->camera_forward.y() = sinf(game_state->camera_pitch);
    game_state->camera_forward.z() = sinf(game_state->camera_yaw) * cosf(game_state->camera_pitch);

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
        game_state->player_position.y() -= speed;
    }
    if(input->kb.keys[SCANCODE_E].is_down || input->ctrl.rb.is_down) {
        game_state->player_position.y() += speed;
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

    game_state->player_position += input->ctrl.left_stick.x() * camera_right * speed;
    game_state->player_position += input->ctrl.left_stick.y() * game_state->camera_forward * speed;

    if (input->kb.keys[SCANCODE_G].is_down && input->kb.keys[SCANCODE_G].transitions == 1) {
        game_state->is_wireframe = !game_state->is_wireframe;
    }

    if (input->kb.keys[SCANCODE_O].is_down && input->kb.keys[SCANCODE_O].transitions == 1) {
        game_state->orbit_mode = !game_state->orbit_mode;
    }

    // NOTE: Unload chunks too far from the player.
    for (usize chunk_idx = 0; chunk_idx < CHUNK_POOL_SIZE; chunk_idx++) {
        Chunk* chunk = &game_state->chunk_pool.slots[chunk_idx];
        if (!chunk->is_loaded) continue;

        v3 chunk_to_unload_world_pos = chunkToWorldPos(chunk->chunk_position);
        v3 chunk_to_unload_center_pos = chunk_to_unload_world_pos + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

        v3 player_chunk_center_pos = chunkToWorldPos(worldPosToChunk(game_state->player_position)) + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

        f32 dist = length(player_chunk_center_pos - chunk_to_unload_center_pos);
        if (dist > (f32)LOAD_RADIUS * CHUNK_W) {

            if (chunk->vertex_buffer.buffer != nullptr) {
                graphicsMemoryFreeBuffer(&game_state->renderer.vram_allocator, &chunk->vertex_buffer);
            }

            hashmapRemove(&game_state->world_hashmap, chunk->chunk_position);
            PoolReleaseItem(&game_state->chunk_pool, chunk);
        }
    }

    // NOTE: Iterate over all chunk positions that should be loaded, and
    // load them if they aren't.
    v3i player_chunk_pos = worldPosToChunk(game_state->player_position);
    for (i32 x = player_chunk_pos.x() - LOAD_RADIUS; x <= player_chunk_pos.x() + LOAD_RADIUS; x++) {
        for (i32 y = player_chunk_pos.y() - LOAD_RADIUS; y <= player_chunk_pos.y() + LOAD_RADIUS; y++) {
            for (i32 z = player_chunk_pos.z() - LOAD_RADIUS; z <= player_chunk_pos.z() + LOAD_RADIUS; z++) {

                v3i chunk_to_load_pos = v3i {x, y, z};

                v3 chunk_to_load_world_pos = chunkToWorldPos(chunk_to_load_pos);
                v3 chunk_to_load_center_pos = chunk_to_load_world_pos + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

                v3 player_chunk_center_pos = chunkToWorldPos(worldPosToChunk(game_state->player_position)) + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

                if (length(player_chunk_center_pos - chunk_to_load_center_pos) > (f32)LOAD_RADIUS * CHUNK_W) continue;
                if (hashmapContains(&game_state->world_hashmap, chunk_to_load_pos)) continue;

                // NOTE: Now we know that we need to load a new chunk.
                Chunk* new_chunk = PoolAcquireItem(&game_state->chunk_pool);
                hashmapInsert(&game_state->world_hashmap, chunk_to_load_pos, new_chunk);

                // NOTE: Someone forgot to free VRAM...
                ASSERT(new_chunk->vertex_buffer.buffer == nullptr);

                *new_chunk = {};
                new_chunk->is_loaded = true;
                new_chunk->chunk_position = chunk_to_load_pos;                   
                new_chunk->needs_remeshing = true;

                for(usize block_idx = 0; block_idx < CHUNK_W * CHUNK_W * CHUNK_W; block_idx++){
                    u32 block_x = new_chunk->chunk_position.x() * CHUNK_W + (block_idx % CHUNK_W);
                    u32 block_y = new_chunk->chunk_position.y() * CHUNK_W + (block_idx / CHUNK_W) % (CHUNK_W);
                    u32 block_z = new_chunk->chunk_position.z() * CHUNK_W + (block_idx / (CHUNK_W * CHUNK_W));

                    // TODO: This needs to be parameterized and put into a function.
                    // The fancy name is "fractal brownian motion", but it's just summing
                    // noise layers with reducing intensity and increasing frequency.
                    f32 space_scaling_factor = 0.01;
                    f32 height_intensity = 32.0;
                    f32 height = 0;
                    for (i32 octave = 0; octave < 5; octave ++) {
                        height += ((simplex_noise_2d(&game_state->simplex_table, (f32)block_x * space_scaling_factor, (f32) block_z * space_scaling_factor) + 1.f) / 2.f) * height_intensity;
                        space_scaling_factor *= 2.f;
                        height_intensity /= 3.f;
                    }

                    if (block_y <= height) {
                        new_chunk->data[block_idx] = 1;
                    } else {
                        // TODO: We cleared the whole chunk
                        // memory a dozen lines up, but should we ?
                        // Or would it be better to define precise
                        // chunk memory lifetimes so we know when
                        // it has or has not been cleared ?
                        // new_chunk->data[block_idx] = 0;
                    }
                }
            }
        }
    }
    // RENDERING

    // NOTE: Wait until the work previously submitted by the frame is done.
    Frame& current_frame = game_state->renderer.frames[game_state->renderer.frames_counter % FRAMES_IN_FLIGHT];
    VK_ASSERT(vkWaitForFences(game_state->renderer.device, 1, &current_frame.render_fence, true, ONE_SECOND_TIMEOUT));
    VK_ASSERT(vkResetFences(game_state->renderer.device, 1, &current_frame.render_fence));

    // NOTE: Request an image from the swapchain that we can use for rendering.
    u32 swapchain_img_idx;
    VK_ASSERT(vkAcquireNextImageKHR(game_state->renderer.device, game_state->renderer.swapchain, ONE_SECOND_TIMEOUT, current_frame.swapchain_semaphore, nullptr, &swapchain_img_idx));
    ASSERT(swapchain_img_idx == (game_state->renderer.frames_counter % FRAMES_IN_FLIGHT));

    // TODO: This needs to be put somewhere else.
    game_state->renderer.distributed_staging_buffers = 0;

    // NOTE: Begin using the command buffer, specifying that we will only be
    // submitting once before resetting it (because the render commands needed
    // for the scene are different every frame).
    VK_ASSERT(vkResetCommandBuffer(current_frame.cmd_buffer, 0));

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_ASSERT(vkBeginCommandBuffer(current_frame.cmd_buffer, &begin_info));

    // NOTE: The first time, initialise the text renderer with pipeline creation
    // and bitmap font texture upload.
    // TODO: The only reason we have this check here is that we have to
    // initialize the text rendering once inside of a command buffer recording.
    // We could simply have a queue of GPU uploads, and call the text rendering
    // init with all the other init stuff. The function would just push a
    // texture upload command that would be processed later.
    if (!game_state->text_rendering_state.is_initialized) {
        textRenderingInitialize(
            &game_state->text_rendering_state,
            &game_state->renderer,
            &game_state->frame_arena
        );
    }

    // NOTE: Iterate on all chunks from the pool and record copy commands
    // for every chunk from that pool that needs its mesh buffer updated.
    for (u32 chunk_idx = 0; chunk_idx < CHUNK_POOL_SIZE; chunk_idx++) {
        Chunk* chunk = &game_state->chunk_pool.slots[chunk_idx];
        if (!chunk->is_loaded) continue;
        if (!chunk->needs_remeshing) continue;

        AllocatedBuffer* staging_buffer = rendererRequestStagingBuffer(&game_state->renderer);
        // NOTE: No more staging buffers available !
        // The remaining chunks will have to wait for the next frame.
        if (staging_buffer == nullptr) break;

        // WARNING: I originally forgot to put this !
        // This needs to be before we continue in case
        // of empty chunk, but after we break if there
        // are no more staging buffers available.
        chunk->needs_remeshing = false;

        usize generated_vertices;
        generateNaiveChunkMesh(
            chunk,
            (ChunkVertex*)staging_buffer->alloc.mapped_data,
            &generated_vertices
        );
        ASSERT(generated_vertices * sizeof(ChunkVertex) <= staging_buffer->alloc.alloc_size);

        chunk->vertices_count = generated_vertices;

        // NOTE: Empty chunk ! No need to bother with it.
        if (chunk->vertices_count == 0) continue;

        // NOTE: If the current vertex buffer is too small, we need to allocate a bigger one.
        if (chunk->vertex_buffer.alloc.alloc_size < generated_vertices * sizeof(ChunkVertex)) {

            // NOTE: Compute the size to allocate. If the buffer has never been allocated,
            // we'll start at 32K. Otherwise, multiply the size by 2, so that we don't have
            // to reallocate on every change. This is kinda like std::vector !
            usize to_allocate_size = chunk->vertex_buffer.buffer != nullptr ? chunk->vertex_buffer.alloc.alloc_size : KILOBYTES(32);
            while (to_allocate_size < generated_vertices * sizeof(ChunkVertex)) {
                to_allocate_size *= 2;
            }

            // NOTE: De-allocate the previous buffer.
            if (chunk->vertex_buffer.buffer != nullptr) {
                graphicsMemoryFreeBuffer(&game_state->renderer.vram_allocator, &chunk->vertex_buffer);
            }

            // NOTE: Allocate the new one.
            chunk->vertex_buffer = graphicsMemoryAllocateBuffer(
                &game_state->renderer.vram_allocator,
                to_allocate_size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
            );
        }

        // NOTE: Record the transfer.
        VkBufferCopy copy_region = {};
        copy_region.srcOffset = 0;
        copy_region.dstOffset = 0;
        copy_region.size = generated_vertices * sizeof(ChunkVertex);

        vkCmdCopyBuffer(current_frame.cmd_buffer, staging_buffer->buffer, chunk->vertex_buffer.buffer, 1, &copy_region);

        // NOTE: Vertex attributes reading stages accessing this buffer after
        // this barrier will have to wait on copy stages that wrote to it
        // before the barrier.
        VkBufferMemoryBarrier2 transfer_barrier = {};
        transfer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        transfer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        transfer_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        transfer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        transfer_barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        transfer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transfer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transfer_barrier.buffer = chunk->vertex_buffer.buffer;
        transfer_barrier.size = generated_vertices * sizeof(ChunkVertex);

        VkDependencyInfo transfer_barrier_dep_info = {};
        transfer_barrier_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        transfer_barrier_dep_info.pBufferMemoryBarriers = &transfer_barrier;
        transfer_barrier_dep_info.bufferMemoryBarrierCount = 1;

        vkCmdPipelineBarrier2(current_frame.cmd_buffer, &transfer_barrier_dep_info);
    }

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
    render_barrier.image = current_frame.swapchain_image;

    VkDependencyInfo render_dep_info = {};
    render_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    render_dep_info.imageMemoryBarrierCount = 1;
    render_dep_info.pImageMemoryBarriers = &render_barrier;

    vkCmdPipelineBarrier2(current_frame.cmd_buffer, &render_dep_info);

    // NOTE: For dynamic rendering (without a render pass), we need to give info about
    // the render targets and fill out a render info struct.

    VkRenderingAttachmentInfo render_target_info = {};
    render_target_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    render_target_info.imageView = current_frame.swapchain_image_view;
    render_target_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    render_target_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    render_target_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_target_info.clearValue = {.color = {0.01, 0.01, 0.02, 1.0}};

    VkRenderingAttachmentInfo depth_target_info = {};
    depth_target_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_target_info.imageView = current_frame.depth_view;
    depth_target_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_target_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_target_info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_target_info.clearValue = {.depthStencil = {1.0, 0}};

    // TODO: Have a way to get the window dimensions without going to the OS...
    VkRect2D render_area = {};
    RECT client_rect;
    GetClientRect(game_state->renderer.window, &client_rect);
    render_area.extent.height = client_rect.bottom;
    render_area.extent.width = client_rect.right;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.pColorAttachments = &render_target_info;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pDepthAttachment = &depth_target_info;
    rendering_info.layerCount = 1;
    rendering_info.renderArea = render_area;

    vkCmdBeginRendering(current_frame.cmd_buffer, &rendering_info);

    // NOTE: Set the dynamic state shared by all draw commands.
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = client_rect.right;
    viewport.height = client_rect.bottom;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(current_frame.cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = client_rect.right;
    scissor.extent.height = client_rect.bottom;

    vkCmdSetScissor(current_frame.cmd_buffer, 0, 1, &scissor);

    // NOTE: Update the matrices for this frame.
    m4* view_mat = (m4*)(game_state->view_matrix_uniforms[swapchain_img_idx].alloc.mapped_data);
    m4* projection_mat = (m4*)(game_state->projection_matrix_uniforms[swapchain_img_idx].alloc.mapped_data);

    *view_mat = lookAt(game_state->player_position, game_state->player_position + game_state->camera_forward);
    *projection_mat = makeProjection(0.1, 1000, 90);

    // NOTE: Bind the pipeline and the descriptor set for the frame-constant matrices.
    vkCmdBindPipeline(current_frame.cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, game_state->chunk_render_pipeline.pipeline);

    vkCmdBindDescriptorSets(
        current_frame.cmd_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        game_state->chunk_render_pipeline.layout,
        0,
        1,
        &game_state->matrices_desc_sets[swapchain_img_idx],
        0,
        nullptr
    );

    // NOTE: Draw the chunks !

    for (u32 chunk_idx = 0; chunk_idx < CHUNK_POOL_SIZE; chunk_idx++) {
        Chunk* chunk = &game_state->chunk_pool.slots[chunk_idx];
        if (!chunk->is_loaded) continue;

        m4 model_mat = makeTranslation(chunkToWorldPos(chunk->chunk_position));
        vkCmdPushConstants(
            current_frame.cmd_buffer,
            game_state->chunk_render_pipeline.layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(m4),
            &model_mat
        );

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(current_frame.cmd_buffer, 0, 1, &chunk->vertex_buffer.buffer, &offset);

        vkCmdDraw(current_frame.cmd_buffer, chunk->vertices_count, 1, 0, 0);
    }

    // NOTE: Text rendering test.
    char debug_text_string[256];
    v3i chunk_position = worldPosToChunk(game_state->player_position);
    StringCbPrintf(debug_text_string,
        ARRAY_COUNT(debug_text_string),
        "Pos: %.2f, %.2f, %.2f\nChunk: %d, %d, %d\nHashmap: %d/%d\nPool: %d/%d",
        game_state->player_position.x(),
        game_state->player_position.y(),
        game_state->player_position.z(),
        chunk_position.x(),
        chunk_position.y(),
        chunk_position.z(),
        game_state->world_hashmap.nb_occupied,
        WORLD_HASHMAP_SIZE,
        game_state->chunk_pool.nb_allocated,
        CHUNK_POOL_SIZE
    );
    drawDebugTextOnScreen(&game_state->text_rendering_state, current_frame.cmd_buffer, debug_text_string, 0, 0);

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
    present_barrier.image = current_frame.swapchain_image;

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

    VK_ASSERT(vkQueueSubmit2(game_state->renderer.queue, 1, &submit_info, current_frame.render_fence));

    // NOTE: Request presentation of the frame, waiting on the render semaphore.
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pSwapchains = &game_state->renderer.swapchain;
    present_info.swapchainCount = 1;
    present_info.pWaitSemaphores = &current_frame.render_semaphore;
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_img_idx;

    VK_ASSERT(vkQueuePresentKHR(game_state->renderer.queue, &present_info));

    game_state->renderer.frames_counter++;

    clearArena(&game_state->frame_arena);
}
