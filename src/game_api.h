#pragma once

#include "common.h"
#include "input.h"

struct GameMemory {
    bool  is_initialized;
    usize permanent_storage_size;
    void* permanent_storage; // NOTE: guaranteed to be filled with zeros at init
};

void pushSolidColorPipeline();
void pushWireframePipeline();
void pushDebugCube(v3 position, v3 scale, v4 color);
void pushLookAtCamera(v3 eye, v3 target, f32 fov);

struct PlatformAPI {
    decltype(pushDebugCube)* pushDebugCube;
    decltype(pushLookAtCamera)* pushLookAtCamera;
    decltype(pushSolidColorPipeline)* pushSolidColorPipeline;
    decltype(pushWireframePipeline)* pushWireframePipeline;
};

struct TestMatrices {
    m4 model;
    m4 camera;
};

// TODO: should we pass the function pointers every frame ?
extern "C"
void gameUpdate(f32 dt, PlatformAPI* platform_api, GameMemory* memory, InputState* input);
