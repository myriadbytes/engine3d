#include "game_api.h"
#include "noise.h"
#include "img.h"

constexpr usize CHUNK_W = 16;

constexpr usize WORLD_W = 16;
constexpr usize WORLD_H = 2;

// NOTE: This is the worst case number of vertices that would be needed for a chunk mesh (with a "checkerboard" chunk).
// This stops being true once we add transparent blocks. Too bad !
constexpr usize WORST_CASE_CHUNK_VERTICES = ((CHUNK_W * CHUNK_W * CHUNK_W) / 2) * 6 * 2 * 3;

struct ChunkVertex {
    v3 position;
    v3 normal;
};

struct Chunk {
    u32 data[CHUNK_W * CHUNK_W * CHUNK_W];
    usize vertices_count;
    GPU_UploadBuffer* upload_buffer;
    GPU_Buffer* vertex_buffer;
};

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

void refreshChunk(GPU_Context* gpu_context, PlatformAPI* platform_api, Chunk* chunk) {

    // NOTE: generate directly in the mapped upload buffer
    ChunkVertex* buffer = (ChunkVertex*) platform_api->mapUploadBuffer(gpu_context, chunk->upload_buffer);

    generateNaiveChunkMesh(chunk, buffer, &chunk->vertices_count);

    platform_api->unmapUploadBuffer(gpu_context, chunk->upload_buffer);
    // TODO: add a way to check that the number of generated vertices is not bigger than the upload and vertex buffers

    platform_api->blockingUploadToGPUBuffer(gpu_context, chunk->upload_buffer, chunk->vertex_buffer, chunk->vertices_count * sizeof(ChunkVertex));
}

struct GameState {
    f32 time;
    RandomSeries random_series;
    SimplexTable* simplex_table;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;
    v3 camera_forward;
    b32 orbit_mode;

    Chunk world[WORLD_W * WORLD_H * WORLD_W];

    GPU_Pipeline* chunk_render_pipeline;
    GPU_Pipeline* wireframe_render_pipeline;

    b32 is_wireframe;

    u8 static_arena_memory[MEGABYTES(2)];
    Arena static_arena;

    u8 frame_arena_memory[MEGABYTES(2)];
    Arena frame_arena;
};

extern "C"
void gameUpdate(f32 dt, GPU_Context* gpu_context, PlatformAPI* platform_api, GameMemory* memory, InputState* input) {
    ASSERT(memory->permanent_storage_size >= sizeof(GameState));
    GameState* game_state = (GameState*)memory->permanent_storage;

    // INITIALIZATION
    if(!memory->is_initialized) {
        game_state->static_arena.base = game_state->static_arena_memory;
        game_state->static_arena.capacity = ARRAY_COUNT(game_state->static_arena_memory);

        game_state->frame_arena.base = game_state->frame_arena_memory;
        game_state->frame_arena.capacity = ARRAY_COUNT(game_state->frame_arena_memory);

        game_state->player_position = {0, 40, 0};
        game_state->orbit_mode = false;
        game_state->time = 0;
        game_state->camera_pitch = -1 * PI32 / 6;
        game_state->camera_yaw = 1 * PI32 / 3;
        game_state->random_series = 0xC0FFEE; // fixed seed for now
        game_state->simplex_table = simplex_table_from_seed(0xC0FFEE, &game_state->static_arena);

        u32 w, h;
        u8* bitmap_font = read_image(".\\assets\\monogram-bitmap.png", &w, &h, &game_state->static_arena, &game_state->frame_arena);
        (void) bitmap_font;

        for (usize chunk_idx = 0; chunk_idx < WORLD_W * WORLD_H * WORLD_W; chunk_idx++) {

            u32 chunk_origin_x = (chunk_idx % WORLD_W) * CHUNK_W;
            u32 chunk_origin_y = ((chunk_idx / WORLD_W) % WORLD_H) * CHUNK_W;
            u32 chunk_origin_z = (chunk_idx / (WORLD_W * WORLD_H)) * CHUNK_W;

            for(usize block_idx = 0; block_idx < CHUNK_W * CHUNK_W * CHUNK_W; block_idx++){
                u32 block_x = chunk_origin_x + (block_idx % CHUNK_W);
                u32 block_y = chunk_origin_y + (block_idx / CHUNK_W) % (CHUNK_W);
                u32 block_z = chunk_origin_z + (block_idx / (CHUNK_W * CHUNK_W));

                // TODO: This needs to be parameterized and put into a function.
                // The fancy name is "fractal brownian motion", but it's just summing
                // noise layers with reducing intensity and increasing frequency.
                f32 space_scaling_factor = 0.01;
                f32 height_intensity = 20.0;
                f32 height = 0;
                for (i32 octave = 0; octave < 3; octave ++) {
                    height += ((simplex_noise_2d(game_state->simplex_table, (f32)block_x * space_scaling_factor, (f32) block_z * space_scaling_factor) + 1.f) / 2.f) * height_intensity;
                    space_scaling_factor *= 2.f;
                    height_intensity /= 2.f;
                }

                if (block_y <= height) {
                    game_state->world[chunk_idx].data[block_idx] = 1;
                }
            }

            // TODO: the GPU API should have a way to specify ranges in the upload buffer, so you can create a big one and reuse it across a copy path
            game_state->world[chunk_idx].upload_buffer = platform_api->createUploadBuffer(gpu_context, WORST_CASE_CHUNK_VERTICES * sizeof(ChunkVertex), &game_state->static_arena);
            game_state->world[chunk_idx].vertex_buffer = platform_api->createGPUBuffer(gpu_context, WORST_CASE_CHUNK_VERTICES * sizeof(ChunkVertex), GPU_BUFFER_USAGE_VERTEX, &game_state->static_arena);
            refreshChunk(gpu_context, platform_api, &game_state->world[chunk_idx]);
        }

        GPU_Shader* chunk_vertex_shader = platform_api->createShader(gpu_context, (char*)L".\\shaders\\debug_chunk.hlsl", GPU_SHADER_TYPE_VERTEX, &game_state->static_arena);
        GPU_Shader* chunk_fragment_shader = platform_api->createShader(gpu_context, (char*)L".\\shaders\\debug_chunk.hlsl", GPU_SHADER_TYPE_FRAGMENT, &game_state->static_arena);
        GPU_RootConstant chunk_render_pipeline_constants[2] = {{0, sizeof(m4)}, {1, sizeof(m4)}};
        GPU_VertexAttribute chunk_render_pipeline_vertex_attributes[2] = {{0, sizeof(v3)}, {sizeof(v3), sizeof(v3)}};
        game_state->chunk_render_pipeline = platform_api->createPipeline(gpu_context, chunk_render_pipeline_constants, 2, chunk_render_pipeline_vertex_attributes, 2, chunk_vertex_shader, chunk_fragment_shader, true, false, &game_state->static_arena);

        GPU_Shader* wireframe_vertex_shader = platform_api->createShader(gpu_context, (char*)L".\\shaders\\solid_color.hlsl", GPU_SHADER_TYPE_VERTEX, &game_state->static_arena);
        GPU_Shader* wireframe_fragment_shader = platform_api->createShader(gpu_context, (char*)L".\\shaders\\solid_color.hlsl", GPU_SHADER_TYPE_FRAGMENT, &game_state->static_arena);
        GPU_RootConstant wireframe_render_pipeline_constants[3] = {{0, sizeof(m4)}, {1, sizeof(m4)}, {2, sizeof(v4)}};
        game_state->wireframe_render_pipeline = platform_api->createPipeline(gpu_context, wireframe_render_pipeline_constants, 3, chunk_render_pipeline_vertex_attributes, 2, wireframe_vertex_shader, wireframe_fragment_shader, false, true, &game_state->static_arena);

        memory->is_initialized = true;
    }

    // SIMULATION
    game_state->time += dt;

    if (!game_state->orbit_mode) {
        f32 sensitivity = 0.01;
        game_state->camera_pitch += input->kb.mouse_delta.y * sensitivity;
        game_state->camera_yaw += input->kb.mouse_delta.x * sensitivity;

        f32 safe_pitch = PI32 / 2.f - 0.05f;
        game_state->camera_pitch = clamp(game_state->camera_pitch, -safe_pitch, safe_pitch);

        game_state->camera_forward.x = cosf(game_state->camera_yaw) * cosf(game_state->camera_pitch);
        game_state->camera_forward.y = sinf(game_state->camera_pitch);
        game_state->camera_forward.z = sinf(game_state->camera_yaw) * cosf(game_state->camera_pitch);

        v3 camera_right = normalize(cross(game_state->camera_forward, {0, 1, 0}));

        f32 speed = 20 * dt;
        if (input->kb.keys[SCANCODE_LSHIFT].is_down) {
            speed *= 5;
        }

        // TODO: Nothing is normalized, so the player moves faster in diagonal directions.
        // Let's just say it's a Quake reference.

        if(input->kb.keys[SCANCODE_Q].is_down) {
            game_state->player_position.y -= speed;
        }
        if(input->kb.keys[SCANCODE_E].is_down) {
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
    } else {
        f32 orbit_speed = 1.f;
        f32 orbit_distance = 80.f;
        f32 orbit_height = 50.f;
        v3 orbit_center = v3 { ((f32)WORLD_W/2) * CHUNK_W, 0, ((f32)WORLD_W/2) * CHUNK_W };

        f32 orbit_t = game_state->time * orbit_speed;
        game_state->player_position = orbit_center + v3 {cosf(orbit_t) * orbit_distance, orbit_height, sinf(orbit_t) * orbit_distance};
        game_state->camera_forward = normalize(orbit_center - game_state->player_position);
    }

    if (input->kb.keys[SCANCODE_G].is_down && input->kb.keys[SCANCODE_G].transitions == 1) {
        game_state->is_wireframe = !game_state->is_wireframe;
    }

    if (input->kb.keys[SCANCODE_O].is_down && input->kb.keys[SCANCODE_O].transitions == 1) {
        game_state->orbit_mode = !game_state->orbit_mode;
    }

    // FIXME: The code for destroying the block the player is looking at is pretty convoluted.
    // There has to be a better way, maybe recursive ? But that has drawbacks too.
    if (input->kb.keys[SCANCODE_SPACE].is_down) {

        usize chunk_to_traverse = 0;
        bool found_chunk = false;
        v3 traversal_origin;

        // NOTE: If the player is inside a chunk already, we don't have to do a raycast the first time.
        // This would be the default case once the world is entirely filled with chunks, but not for now.
        for (usize chunk_idx = 0; chunk_idx < WORLD_W * WORLD_H * WORLD_W; chunk_idx++) {
            u32 chunk_origin_x = (chunk_idx % WORLD_W) * CHUNK_W;
            u32 chunk_origin_y = ((chunk_idx / WORLD_W) % WORLD_H) * CHUNK_W;
            u32 chunk_origin_z = (chunk_idx / (WORLD_W * WORLD_H)) * CHUNK_W;

            v3 chunk_bb_min = {(f32)(chunk_origin_x), (f32)(chunk_origin_y), (f32)(chunk_origin_z)};
            v3 chunk_bb_max = {(f32)(chunk_origin_x+CHUNK_W), (f32)(chunk_origin_y+CHUNK_W), (f32)(chunk_origin_z+CHUNK_W)};

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
                for (usize chunk_idx = 0; chunk_idx < WORLD_W * WORLD_H * WORLD_W; chunk_idx++) {
                    u32 chunk_origin_x = (chunk_idx % WORLD_W) * CHUNK_W;
                    u32 chunk_origin_y = ((chunk_idx / WORLD_W) % WORLD_H) * CHUNK_W;
                    u32 chunk_origin_z = (chunk_idx / (WORLD_W * WORLD_H)) * CHUNK_W;

                    v3 chunk_bb_min = {(f32)(chunk_origin_x), (f32)(chunk_origin_y), (f32)(chunk_origin_z)};
                    v3 chunk_bb_max = {(f32)(chunk_origin_x+CHUNK_W), (f32)(chunk_origin_y+CHUNK_W), (f32)(chunk_origin_z+CHUNK_W)};

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
                u32 chunk_origin_x = (chunk_to_traverse % WORLD_W) * CHUNK_W;
                u32 chunk_origin_y = ((chunk_to_traverse / WORLD_W) % WORLD_H) * CHUNK_W;
                u32 chunk_origin_z = (chunk_to_traverse / (WORLD_W * WORLD_H)) * CHUNK_W;

                v3 chunk_to_traverse_origin = v3 {(f32)chunk_origin_x, (f32)chunk_origin_y, (f32)chunk_origin_z};
                v3 intersection_relative = traversal_origin - chunk_to_traverse_origin;

                usize block_idx;
                if (raycast_chunk_traversal(&game_state->world[chunk_to_traverse], intersection_relative, game_state->camera_forward, &block_idx)) {
                    game_state->world[chunk_to_traverse].data[block_idx] = 0;
                    // FIXME: This is a quick temporary fix. Without that, we could modify the vertex buffer while it is used to render the previous
                    // frame. I would need profiling to know if it's THAT bad. A possible solution would be to decouple mesh generation and upload,
                    // i.e. generate the mesh but only upload it after the previous frame is rendered. Perhaps in a separate thread.
                    platform_api->waitForGPU(gpu_context);
                    refreshChunk(gpu_context, platform_api, &game_state->world[chunk_to_traverse]);
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

    // RENDERING
    GPU_CommandBuffer* cmd_buf = platform_api->waitForCommandBuffer(gpu_context, &game_state->frame_arena);
    f32 clear_color[] = {0.1, 0.1, 0.2, 1};
    platform_api->recordClearCommand(gpu_context, cmd_buf, clear_color);

    m4 camera_matrix = makeProjection(0.1, 1000, 90) * lookAt(game_state->player_position, game_state->player_position + game_state->camera_forward);

    if (game_state->is_wireframe) {
        platform_api->setPipeline(cmd_buf, game_state->wireframe_render_pipeline);
        v4 wireframe_color = {1, 1, 1, 1};
        platform_api->pushConstant(cmd_buf, 2, wireframe_color.data, sizeof(v4));
    } else {
        platform_api->setPipeline(cmd_buf, game_state->chunk_render_pipeline);
    }

    platform_api->pushConstant(cmd_buf, 1, camera_matrix.data, sizeof(m4));

    for (usize chunk_idx = 0; chunk_idx < WORLD_W * WORLD_H * WORLD_W; chunk_idx++) {
        u32 chunk_origin_x = (chunk_idx % WORLD_W) * CHUNK_W;
        u32 chunk_origin_y = ((chunk_idx / WORLD_W) % WORLD_H) * CHUNK_W;
        u32 chunk_origin_z = (chunk_idx / (WORLD_W * WORLD_H)) * CHUNK_W;

        m4 chunk_model = makeTranslation(v3 {(f32)chunk_origin_x, (f32)chunk_origin_y, (f32)chunk_origin_z});
        platform_api->pushConstant(cmd_buf, 0, chunk_model.data, sizeof(m4));
        platform_api->setVertexBuffer(cmd_buf, game_state->world[chunk_idx].vertex_buffer);
        platform_api->drawCall(cmd_buf, game_state->world[chunk_idx].vertices_count);
    }

    platform_api->sendCommandBufferAndPresent(gpu_context, cmd_buf);

    clearArena(&game_state->frame_arena);
}
