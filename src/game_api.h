#pragma once

#include "common.h"
#include "input.h"
#include "gpu.h"

struct GameMemory {
    bool  is_initialized;
    usize permanent_storage_size;
    void* permanent_storage; // NOTE: guaranteed to be filled with zeros at init
};

//void debugUploadMeshBlocking(f32* data, usize size);

// NOTE: This is not a good abstraction AT ALL, but it's gonna
// get refined over time.
//void pushSolidColorPipeline();
//void pushWireframePipeline();
//void pushDebugMesh(v3 position);
//void pushLookAtCamera(v3 eye, v3 target, f32 fov);

struct PlatformAPI {
    decltype(createUploadBuffer)* createUploadBuffer;
    decltype(createGPUBuffer)* createGPUBuffer;
    decltype(mapUploadBuffer)* mapUploadBuffer;
    decltype(unmapUploadBuffer)* unmapUploadBuffer;
    decltype(blockingUploadToGPUBuffer)* blockingUploadToGPUBuffer;
    decltype(waitForCommandBuffer)* waitForCommandBuffer;
    decltype(sendCommandBufferAndPresent)* sendCommandBufferAndPresent;
    decltype(recordClearCommand)* recordClearCommand;
    decltype(pushConstant)* pushConstant;
    decltype(createShader)* createShader;
    decltype(createPipeline)* createPipeline;
    decltype(setPipeline)* setPipeline;
    decltype(setVertexBuffer)* setVertexBuffer;
    decltype(drawCall)* drawCall;
};

// TODO: should we pass the function pointers every frame ?
extern "C"
void gameUpdate(f32 dt, GPU_Context* gpu_context, PlatformAPI* platform_api, GameMemory* memory, InputState* input);
