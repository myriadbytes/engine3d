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

    if (game_state->is_wireframe) {
        platform_api->pushWireframePipeline();
    } else {
        platform_api->pushSolidColorPipeline();
    }

    // TODO: this is just to test if mesh updating works
    {
        naiveMeshChunk(&game_state->chunk);
        platform_api->debugUploadMeshBlocking((f32*)game_state->chunk.vertices, game_state->chunk.vertices_count * sizeof(v3));
    }

    platform_api->pushLookAtCamera(game_state->player_position, game_state->player_position + camera_forward, 90);

    platform_api->pushDebugMesh(v3 {0, 0, 0});
}
