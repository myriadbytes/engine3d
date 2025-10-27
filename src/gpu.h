#pragma once

#include "arena.h"

struct GPU_Context;
struct GPU_UploadBuffer;
struct GPU_Buffer;
struct GPU_CommandBuffer;

enum GPU_BufferUsage {
    GPU_BUFFER_USAGE_VERTEX,
};

GPU_UploadBuffer* createUploadBuffer(GPU_Context* gpu_context, usize size, Arena* arena);
GPU_Buffer* createGPUBuffer(GPU_Context* gpu_context, usize size, GPU_BufferUsage usage, Arena* arena);

u8* mapUploadBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* buffer);
void unmapUploadBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* buffer);
void blockingUploadToGPUBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* src_buffer, GPU_Buffer* dst_buffer, usize size);

// TODO: We might eventually add copy command buffers, and then we will need to rename the current command buffers
// to GraphicsCommandBuffer or something.
GPU_CommandBuffer* waitForCommandBuffer(GPU_Context* gpu_context, Arena* arena);
void sendCommandBufferAndPresent(GPU_Context* gpu_context, GPU_CommandBuffer* command_buffer);

void recordClearCommand(GPU_Context* gpu_context, GPU_CommandBuffer* command_buffer, f32* clear_color);
