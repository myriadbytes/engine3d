#include "game_api.h"

struct GameState {
    f32 clear_color[4];
};

extern "C"
f32* gameUpdate(f32 dt, GameMemory* memory, InputState* input) {
    GameState* game_state = (GameState*)memory->permanent_storage;

    if(!memory->is_initialized) {
        memory->is_initialized = true;
    }

    f32 digital_color[4] = {input->kb.mouse_screen_pos.x,  input->kb.mouse_screen_pos.y, 0.0, 1};
    f32 analog_color[4] = {input->ctrl.right_stick.x, input->ctrl.right_stick.y, 0.0, 1};

    if (input->is_analog) {
        game_state->clear_color[0] = analog_color[0];
        game_state->clear_color[1] = analog_color[1];
        game_state->clear_color[2] = analog_color[2];
        game_state->clear_color[3] = analog_color[3];
    } else {
        game_state->clear_color[0] = digital_color[0];
        game_state->clear_color[1] = digital_color[1];
        game_state->clear_color[2] = digital_color[2];
        game_state->clear_color[3] = digital_color[3];
    }

    return game_state->clear_color;
}
