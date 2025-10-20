#pragma once

#include "common.h"
#include "input.h"

struct GameMemory {
    bool  is_initialized;
    usize permanent_storage_size;
    void* permanent_storage; // NOTE: guaranteed to be filled with zeros at init
};

extern "C"
f32* gameUpdate(f32 dt, GameMemory* memory, InputState* input);
