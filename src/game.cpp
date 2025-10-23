#include "game_api.h"

constexpr usize CHUNK_W = 16;

struct GameState {
    f32 time;
    RandomSeries random_series;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;

    u32 chunk[CHUNK_W * CHUNK_W * CHUNK_W];

    b32 is_wireframe;
};

extern "C"
void gameUpdate(f32 dt, PlatformAPI* platform_api, GameMemory* memory, InputState* input) {
    ASSERT(memory->permanent_storage_size >= sizeof(GameState));
    GameState* game_state = (GameState*)memory->permanent_storage;

    if(!memory->is_initialized) {
        game_state->player_position = {0, 5, 8};
        game_state->time = 0;
        game_state->camera_yaw = 3 * PI32 / 2;
        game_state->random_series = 0xC0FFEE; // fixed seed for now

        for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){
            if (randomNextU32(&game_state->random_series) % 2) {
                game_state->chunk[i] = 1;
            }
        }

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
    platform_api->pushLookAtCamera(game_state->player_position, game_state->player_position + camera_forward, 90);

    for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){
        if (!game_state->chunk[i]) continue;

        usize y = (i / (CHUNK_W * CHUNK_W));
        usize z = (i % (CHUNK_W * CHUNK_W)) / CHUNK_W;
        usize x = (i % CHUNK_W);

        v3 position = {(f32)x, (f32)y, (f32)z};
        f32 y_color = (f32)y / CHUNK_W;
        f32 z_color = (f32)z / CHUNK_W;
        f32 x_color = (f32)x / CHUNK_W;
        v4 color = {x_color, y_color, z_color, 1.0};

        platform_api->pushDebugCube(position, v3 {1, 1, 1}, color);
    }
}
