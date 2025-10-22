#include "game_api.h"

struct GameState {
    v3 player_position;
    f32 time;
};

extern "C"
void gameUpdate(f32 dt, PlatformAPI* platform_api, GameMemory* memory, InputState* input) {
    GameState* game_state = (GameState*)memory->permanent_storage;

    if(!memory->is_initialized) {
        game_state->player_position = {0, 5, 0};
        game_state->time = 0;
        memory->is_initialized = true;
    }

    game_state->time += dt;

    f32 vertical_speed = 0.05;
    if(input->kb.keys[SCANCODE_Q].is_down) {
        game_state->player_position.y -= vertical_speed;
    }
    if(input->kb.keys[SCANCODE_E].is_down) {
        game_state->player_position.y += vertical_speed;
    }

    // FIXME: sinf precision loss as time goes on
    f32 orbit_speed = 0.3;
    f32 orbit_distance = 10;
    game_state->player_position.x = sinf(game_state->time * orbit_speed) * orbit_distance;
    game_state->player_position.z = cosf(game_state->time * orbit_speed) * orbit_distance;

    platform_api->pushLookAtCamera(game_state->player_position, v3 {0, 0, 0}, 90);

    i16 width = 10;
    for (i16 x = -width / 2; x <= width / 2; x++) {
        for (i16 z = -width / 2; z <= width / 2; z++) {
            v4 color = {(f32)(x + width / 2) / width, 0.1, (f32)(z + width / 2) / width, 1};
            f32 height = (f32)x * sinf(game_state->time) * 0.2 + (f32)z * cosf(game_state->time) * 0.4;
            platform_api->pushSolidColorCube(v3 {(f32)x, height, f32(z)}, v3 {1, 1, 1}, color);
        }
    }
}
