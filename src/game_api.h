#pragma once

#include "common.h"
#include "input.h"

struct GameMemory {
    bool  is_initialized;
    usize permanent_storage_size;
    void* permanent_storage; // NOTE: guaranteed to be filled with zeros at init
};

// TODO: should we pass the function pointers every frame ?
extern "C"
void gameUpdate(f32 dt, GameMemory* memory, InputState* input);
