#pragma once

#include "common.h"
#include "renderer.h"
#include "input.h"

struct GameMemory {
    bool  is_initialized;
    usize permanent_storage_size;
    void* permanent_storage; // NOTE: guaranteed to be filled with zeros at init
};

struct PlatformAPI {
    decltype(pushSolidColorCube)* pushSolidColorCube;
};

struct TestMatrices {
    m4 model;
    m4 camera;
};

// TODO: should we pass the function pointers every frame ?
extern "C"
TestMatrices gameUpdate(f32 dt, PlatformAPI* platform_api, GameMemory* memory, InputState* input);
