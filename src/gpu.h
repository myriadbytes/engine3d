#pragma once

#include "arena.h"

// NOTE: This API is just a first draft to get going and probably has a lot of issues,
// but it's going to get refined over time.

struct GPU_Context;
struct GPU_UploadBuffer;
struct GPU_Buffer;
struct GPU_CommandBuffer;
struct GPU_Shader;
struct GPU_Pipeline;

// FIXME: SDL_GPU doesn't use inline root parameter constants in the D3D12,
// so it's probably difficult to port this model to other APIs.
struct GPU_RootConstant {
    u32 slot;
    u32 size;
};

struct GPU_VertexAttribute {
    u32 offset;
    u32 size;
};

enum GPU_BufferUsage {
    GPU_BUFFER_USAGE_VERTEX,
};

enum GPU_ShaderType {
    GPU_SHADER_TYPE_VERTEX,
    GPU_SHADER_TYPE_FRAGMENT,
};

GPU_UploadBuffer* createUploadBuffer(GPU_Context* gpu_context, usize size, Arena* arena);
GPU_Buffer* createGPUBuffer(GPU_Context* gpu_context, usize size, GPU_BufferUsage usage, Arena* arena);
GPU_Shader* createShader(GPU_Context* gpu_context, char* path, GPU_ShaderType type, Arena* arena);
// TODO: Make a creation info struct instead of passing a thousand parameters.
GPU_Pipeline* createPipeline(GPU_Context* gpu_context, GPU_RootConstant* root_constants, u32 num_root_constants, GPU_VertexAttribute* vertex_attributes, u32 num_vertex_attributes, GPU_Shader* vertex_shader, GPU_Shader* fragment_shader, b32 backface_culling, b32 wireframe, Arena* arena);

u8* mapUploadBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* buffer);
void unmapUploadBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* buffer);
void blockingUploadToGPUBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* src_buffer, GPU_Buffer* dst_buffer, usize size);

// TODO: We might eventually add copy command buffers, and then we will need to rename the current command buffers
// to GraphicsCommandBuffer or something.
GPU_CommandBuffer* waitForCommandBuffer(GPU_Context* gpu_context, Arena* arena);
void sendCommandBufferAndPresent(GPU_Context* gpu_context, GPU_CommandBuffer* command_buffer);

void recordClearCommand(GPU_Context* gpu_context, GPU_CommandBuffer* command_buffer, f32* clear_color);
void pushConstant(GPU_CommandBuffer* command_buffer, u32 slot, void* data, usize size);
void setPipeline(GPU_CommandBuffer* command_buffer, GPU_Pipeline* pipeline);
void setVertexBuffer(GPU_CommandBuffer* command_buffer, GPU_Buffer* vertex_buffer);
void drawCall(GPU_CommandBuffer* command_buffer, usize vertex_count);
