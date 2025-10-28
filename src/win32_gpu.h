#pragma once

#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#include "gpu.h"

constexpr usize FRAMES_IN_FLIGHT = 2;

struct FrameContext {
    ID3D12CommandAllocator* command_allocator;
    ID3D12GraphicsCommandList* command_list;
    ID3D12Resource* render_target_resource;
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view_descriptor;
    ID3D12Resource* depth_target_resource;
    D3D12_CPU_DESCRIPTOR_HANDLE depth_target_view_descriptor;
    ID3D12Fence* fence;
    HANDLE fence_wait_event;
    u64 fence_ready_value;
};

struct GPU_Context {
    HWND window;
    ID3D12Debug* debug_interface;

    ID3D12Device* device;
    ID3D12CommandQueue* graphics_command_queue;

    ID3D12CommandQueue* copy_command_queue;
    ID3D12CommandAllocator* copy_allocator;
    ID3D12GraphicsCommandList* copy_command_list;
    ID3D12Fence* copy_fence;
    HANDLE copy_fence_wait_event;
    u64 copy_fence_ready_value;

    IDXGISwapChain3* swapchain;

    ID3D12DescriptorHeap* rtv_heap;
    u32 rtv_descriptor_size;

    ID3D12DescriptorHeap* dsv_heap;
    u32 dsv_descriptor_size;

    FrameContext frames[FRAMES_IN_FLIGHT];
    u32 current_frame_idx;
};

struct GPU_UploadBuffer {
    ID3D12Resource* resource;
};

struct GPU_Buffer {
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES usage_state;
    b32 in_usage_state;
    usize size;
};

struct GPU_CommandBuffer {
    ID3D12GraphicsCommandList* command_list;
    GPU_Pipeline* bound_pipeline;
};

struct GPU_Shader {
    ID3DBlob* shader_blob;
};

struct GPU_Pipeline {
    ID3D12RootSignature* root_signature;
    ID3D12PipelineState* pipeline_state;

    usize vertex_stride;
};

GPU_Context initD3D12(HWND window_handle, b32 debug_mode);
