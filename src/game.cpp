#include "game_api.h"

struct GameState {
    v3 player_position;
    f32 time;
    f32 camera_pitch;
    f32 camera_yaw;
    RandomSeries random_series;
};

extern "C"
void gameUpdate(f32 dt, PlatformAPI* platform_api, GameMemory* memory, InputState* input) {
    GameState* game_state = (GameState*)memory->permanent_storage;

    if(!memory->is_initialized) {
        game_state->player_position = {0, 5, 8};
        game_state->time = 0;
        game_state->camera_yaw = 3 * PI32 / 2;
        game_state->random_series = 0xC0FFEE; // fixed seed for now
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

    platform_api->pushLookAtCamera(game_state->player_position, game_state->player_position + camera_forward, 90);

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

    // FIXME: sinf precision loss as time goes on
    //f32 orbit_speed = 0.3;
    //f32 orbit_distance = 8;
    //game_state->player_position.x = sinf(game_state->time * orbit_speed) * orbit_distance;
    //game_state->player_position.z = cosf(game_state->time * orbit_speed) * orbit_distance;

    i16 width = 8;
    for (i16 x = -width / 2; x <= width / 2; x++) {
        for (i16 z = -width / 2; z <= width / 2; z++) {
            //f32 height = (f32)x * sinf(game_state->time) * 0.1 + (f32)z * cosf(game_state->time) * 0.1;

            f32 height = randomUnilateral(&game_state->random_series);

            v4 color;
            color.r = ((f32)x + ((f32)width / 2.f)) / (f32)width;
            color.g = 0.1;
            color.b = ((f32)z + ((f32)width / 2.f)) / (f32)width;
            platform_api->pushSolidColorCube(v3 {(f32)x, height, f32(z)}, v3 {1, 1, 1}, color);
        }
    }
}
