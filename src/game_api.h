#pragma once

#include "common.h"
#include "input.h"

struct GamePlatformState {
    i32 surface_width;
    i32 surface_height;

    b32 surface_has_been_resized;
    b32 surface_is_minimized;
};

struct GameMemory {
    bool  is_initialized;
    usize permanent_storage_size;
    void* permanent_storage; // NOTE: guaranteed to be filled with zeros at init
};

// TODO: should we pass the function pointers every frame ?
extern "C"
void gameUpdate(f32 dt, GamePlatformState* platform_state, GameMemory* memory, InputState* input);
