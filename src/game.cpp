#include "game_api.h"

constexpr usize CHUNK_W = 16;

struct Chunk {
    u32 data[CHUNK_W * CHUNK_W * CHUNK_W];
     // NOTE: This is the worst case situation, with a checkboard chunk.
     // I don't think there are any reason to keep the vertices around after
     // the GPU upload, so this can be removed and get replaced by a shared buffer
     // for transfers.
    v3 vertices[(CHUNK_W * CHUNK_W * CHUNK_W) / 2 * 6 * 2 * 3];
    usize vertices_count;
};

struct GameState {
    f32 time;
    RandomSeries random_series;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;

    Chunk chunk;

    b32 is_wireframe;
};

// TODO: Many duplicate vertices. Is it easy/possible to use indices here ?
// Also look into switching to greedy meshing.
void naiveMeshChunk(Chunk* chunk) {
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
            chunk->vertices[emitted++] = position + v3 {1, 0, 0};
            chunk->vertices[emitted++] = position + v3 {1, 1, 0};
            chunk->vertices[emitted++] = position + v3 {1, 0, 1};

            chunk->vertices[emitted++] = position + v3 {1, 1, 0};
            chunk->vertices[emitted++] = position + v3 {1, 1, 1};
            chunk->vertices[emitted++] = position + v3 {1, 0, 1};
        }

        if (empty_neg_x) {
            chunk->vertices[emitted++] = position + v3 {0, 0, 0};
            chunk->vertices[emitted++] = position + v3 {0, 0, 1};
            chunk->vertices[emitted++] = position + v3 {0, 1, 0};

            chunk->vertices[emitted++] = position + v3 {0, 1, 0};
            chunk->vertices[emitted++] = position + v3 {0, 0, 1};
            chunk->vertices[emitted++] = position + v3 {0, 1, 1};
        }

        if (empty_pos_y) {
            chunk->vertices[emitted++] = position + v3 {0, 1, 0};
            chunk->vertices[emitted++] = position + v3 {0, 1, 1};
            chunk->vertices[emitted++] = position + v3 {1, 1, 0};

            chunk->vertices[emitted++] = position + v3 {0, 1, 1};
            chunk->vertices[emitted++] = position + v3 {1, 1, 1};
            chunk->vertices[emitted++] = position + v3 {1, 1, 0};
        }

        if (empty_neg_y) {
            chunk->vertices[emitted++] = position;
            chunk->vertices[emitted++] = position + v3 {1, 0, 0};
            chunk->vertices[emitted++] = position + v3 {0, 0, 1};

            chunk->vertices[emitted++] = position + v3 {1, 0, 0};
            chunk->vertices[emitted++] = position + v3 {1, 0, 1};
            chunk->vertices[emitted++] = position + v3 {0, 0, 1};
        }

        if (empty_pos_z) {
            chunk->vertices[emitted++] = position + v3 {0, 0, 1};
            chunk->vertices[emitted++] = position + v3 {1, 0, 1};
            chunk->vertices[emitted++] = position + v3 {1, 1, 1};

            chunk->vertices[emitted++] = position + v3 {0, 0, 1};
            chunk->vertices[emitted++] = position + v3 {1, 1, 1};
            chunk->vertices[emitted++] = position + v3 {0, 1, 1};
        }

        if (empty_neg_z) {
            chunk->vertices[emitted++] = position + v3 {0, 0, 0};
            chunk->vertices[emitted++] = position + v3 {1, 1, 0};
            chunk->vertices[emitted++] = position + v3 {1, 0, 0};

            chunk->vertices[emitted++] = position + v3 {0, 0, 0};
            chunk->vertices[emitted++] = position + v3 {0, 1, 0};
            chunk->vertices[emitted++] = position + v3 {1, 1, 0};
        }
    }

    chunk->vertices_count = emitted;

    ASSERT(chunk->vertices_count <= ARRAY_COUNT(chunk->vertices));
}

b32 raycast_aabb(v3 ray_origin, v3 ray_direction, v3 bb_min, v3 bb_max, v3* out_intersection) {
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

    *out_intersection = ray_origin + ray_direction * tmin;
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

extern "C"
void gameUpdate(f32 dt, PlatformAPI* platform_api, GameMemory* memory, InputState* input) {
    ASSERT(memory->permanent_storage_size >= sizeof(GameState));
    GameState* game_state = (GameState*)memory->permanent_storage;

    if(!memory->is_initialized) {
        game_state->player_position = {0, 5, -5};
        game_state->time = 0;
        game_state->camera_yaw = 3 * PI32 / 2;
        game_state->random_series = 0xC0FFEE; // fixed seed for now

        for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){
            if (randomNextU32(&game_state->random_series) % 2) {
                game_state->chunk.data[i] = 1;
            }
        }

        naiveMeshChunk(&game_state->chunk);
        platform_api->debugUploadMeshBlocking((f32*)game_state->chunk.vertices, game_state->chunk.vertices_count * sizeof(v3));

        memory->is_initialized = true;
    }

    game_state->time += dt;

    f32 sensitivity = 0.01;
    game_state->camera_pitch += input->kb.mouse_delta.y * sensitivity;
    game_state->camera_yaw += input->kb.mouse_delta.x * sensitivity;

    f32 safe_pitch = PI32 / 2.f - 0.1f;
    game_state->camera_pitch = clamp(game_state->camera_pitch, -safe_pitch, safe_pitch);

    v3 camera_forward = {};
    camera_forward.x = cosf(game_state->camera_yaw) * cosf(game_state->camera_pitch);
    camera_forward.y = sinf(game_state->camera_pitch);
    camera_forward.z = sinf(game_state->camera_yaw) * cosf(game_state->camera_pitch);

    v3 camera_right = normalize(cross(camera_forward, {0, 1, 0}));

    f32 speed = 5 * dt;
    if (input->kb.keys[SCANCODE_LSHIFT].is_down) {
        speed *= 2;
    }

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
        game_state->player_position += -camera_forward * speed;
    }
    if(input->kb.keys[SCANCODE_W].is_down) {
        game_state->player_position += camera_forward * speed;
    }

    if (input->kb.keys[SCANCODE_G].is_down && input->kb.keys[SCANCODE_G].transitions == 1) {
        game_state->is_wireframe = !game_state->is_wireframe;
    }

    // NOTE: If the player is not inside the debug chunk, we need to find the closest
    // point along the ray that is inside the chunk in order to begin the voxel traversal.
    b32 should_remove_block = input->kb.keys[SCANCODE_SPACE].is_down;
    v3 traversal_origin;

    v3 chunk_bb_min = {0, 0, 0};
    v3 chunk_bb_max = {CHUNK_W, CHUNK_W, CHUNK_W};

    if (point_inclusion_aabb(game_state->player_position, chunk_bb_min, chunk_bb_max)) {
        traversal_origin = game_state->player_position;
    } else {
        should_remove_block = should_remove_block && raycast_aabb(game_state->player_position, camera_forward, chunk_bb_min, chunk_bb_max, &traversal_origin);
    }

    usize block_idx;
    if (should_remove_block && raycast_chunk_traversal(&game_state->chunk, traversal_origin, camera_forward, &block_idx)) {
        game_state->chunk.data[block_idx] = 0;
        naiveMeshChunk(&game_state->chunk);
        platform_api->debugUploadMeshBlocking((f32*)game_state->chunk.vertices, game_state->chunk.vertices_count * sizeof(v3));
    }

    if (game_state->is_wireframe) {
        platform_api->pushWireframePipeline();
    } else {
        platform_api->pushSolidColorPipeline();
    }

    // NOTE: Remove a random block every frame, just to stress test the meshing + GPU upload.
    //{
    //    game_state->chunk.data[randomNextU32(&game_state->random_series) % ARRAY_COUNT(game_state->chunk.data)] = 0;
    //    naiveMeshChunk(&game_state->chunk);
    //    platform_api->debugUploadMeshBlocking((f32*)game_state->chunk.vertices, game_state->chunk.vertices_count * sizeof(v3));
    //}

    platform_api->pushLookAtCamera(game_state->player_position, game_state->player_position + camera_forward, 90);

    platform_api->pushDebugMesh(v3 {0, 0, 0});
}
