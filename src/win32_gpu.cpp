#include "win32_gpu.h"

GPU_Context initD3D12(HWND window_handle, b32 debug_mode) {
    GPU_Context context = {};

    // FIXME: release all the COM objects

    if (debug_mode) {
        HRESULT debug_res = D3D12GetDebugInterface(IID_PPV_ARGS(&context.debug_interface));
        ASSERT(SUCCEEDED(debug_res));
        context.debug_interface->EnableDebugLayer();
    };

    IDXGIFactory4* factory;
    HRESULT factory_res = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    ASSERT(SUCCEEDED(factory_res));

    IDXGIAdapter1* adapters[8] = {};
    u32 adapters_count = 0;
    for (u32 i = 0; i < ARRAY_COUNT(adapters); i++) {
        HRESULT res = factory->EnumAdapters1(i, &adapters[i]);
        if (res == DXGI_ERROR_NOT_FOUND) break;
        adapters_count += 1;
    }

    // FIXME: this should look for the discrete GPU in priority
    i32 hardware_adapter_id = -1;
    for (u32 i = 0; i < adapters_count; i++) {
        DXGI_ADAPTER_DESC1 desc;
        HRESULT desc_res = adapters[i]->GetDesc1(&desc);
        ASSERT(SUCCEEDED(desc_res));

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        hardware_adapter_id = i;
        OutputDebugStringW(desc.Description);
        OutputDebugStringA("\n");
        break;
    }

    ASSERT(hardware_adapter_id >= 0);

    HRESULT creation_res = D3D12CreateDevice(adapters[hardware_adapter_id], D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&context.device));
    ASSERT(SUCCEEDED(creation_res));

    D3D12_COMMAND_QUEUE_DESC graphics_queue_desc = {};
    graphics_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT graphics_queue_res = context.device->CreateCommandQueue(&graphics_queue_desc, IID_PPV_ARGS(&context.graphics_command_queue));
    ASSERT(SUCCEEDED(graphics_queue_res));

    // NOTE: Copy queue stuff
    D3D12_COMMAND_QUEUE_DESC copy_queue_desc = {};
    copy_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    HRESULT copy_queue_res = context.device->CreateCommandQueue(&copy_queue_desc, IID_PPV_ARGS(&context.copy_command_queue));
    ASSERT(SUCCEEDED(copy_queue_res));
    HRESULT copy_command_allocator_res = context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&context.copy_allocator));
    ASSERT(SUCCEEDED(copy_command_allocator_res));
    HRESULT copy_cmd_list_res = context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, context.copy_allocator, NULL, IID_PPV_ARGS(&context.copy_command_list));
    ASSERT(SUCCEEDED(copy_cmd_list_res));
    HRESULT copy_close_res = context.copy_command_list->Close();
    ASSERT(SUCCEEDED(copy_close_res));
    HRESULT copy_fence_res = context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context.copy_fence));
    ASSERT(SUCCEEDED(copy_fence_res));

    context.copy_fence_wait_event = CreateEventA(NULL, FALSE, FALSE, NULL);

    IDXGISwapChain1* legacy_swapchain;
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = FRAMES_IN_FLIGHT;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT swapchain_res = factory->CreateSwapChainForHwnd(
        context.graphics_command_queue,
        window_handle,
        &swapchain_desc,
        NULL,
        NULL,
        &legacy_swapchain
    );
    ASSERT(SUCCEEDED(swapchain_res));
    legacy_swapchain->QueryInterface(IID_PPV_ARGS(&context.swapchain));

    // NOTE: descriptor heap for our render target views
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = FRAMES_IN_FLIGHT;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT rtv_heap_res = context.device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&context.rtv_heap));
    ASSERT(SUCCEEDED(rtv_heap_res));
    context.rtv_descriptor_size = context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // NOTE: fill the heap with our 2 render target views
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_current_descriptor = context.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        // NOTE: We first get a ID3D12Resource (handle for the actual GPU memory of the swapchain)
        // and then create a render target view from that.
        HRESULT swapchain_buffer_res = context.swapchain->GetBuffer(frame_idx, IID_PPV_ARGS(&context.frames[frame_idx].render_target_resource));
        ASSERT(SUCCEEDED(swapchain_buffer_res));

        context.device->CreateRenderTargetView(context.frames[frame_idx].render_target_resource, NULL, rtv_current_descriptor);
        context.frames[frame_idx].render_target_view_descriptor = rtv_current_descriptor;

        rtv_current_descriptor.ptr += context.rtv_descriptor_size;
    }

    // NOTE: kinda the same thing for depth buffers, except we need to create them since they are not managed by the DXGI swapchain object.
    RECT client_rect;
    GetClientRect(window_handle, &client_rect);

    D3D12_RESOURCE_DESC depth_desc = {};
    depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depth_desc.Width = client_rect.right;
    depth_desc.Height = client_rect.bottom;
    depth_desc.DepthOrArraySize = 1;
    depth_desc.MipLevels = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear_value = {};
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        D3D12_HEAP_PROPERTIES depth_heap_props = {};
        depth_heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        depth_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        depth_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        depth_heap_props.CreationNodeMask = 1;
        depth_heap_props.VisibleNodeMask = 1;

        HRESULT heap_creation_result = context.device->CreateCommittedResource(
            &depth_heap_props,
            D3D12_HEAP_FLAG_NONE,
            &depth_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_value,
            IID_PPV_ARGS(&context.frames[frame_idx].depth_target_resource)
        );
        ASSERT(SUCCEEDED(heap_creation_result));
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.NumDescriptors = FRAMES_IN_FLIGHT;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT dsv_heap_res = context.device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&context.dsv_heap));
    ASSERT(SUCCEEDED(dsv_heap_res));
    context.dsv_descriptor_size = context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // NOTE: fill the descriptor heap with our 2 depth buffer views
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_heap_current_descriptor = context.dsv_heap->GetCPUDescriptorHandleForHeapStart();
    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Flags = D3D12_DSV_FLAG_NONE;

        context.device->CreateDepthStencilView(context.frames[frame_idx].depth_target_resource, &dsv_desc, dsv_heap_current_descriptor);
        context.frames[frame_idx].depth_target_view_descriptor = dsv_heap_current_descriptor;

        dsv_heap_current_descriptor.ptr += context.dsv_descriptor_size;
    }

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        HRESULT command_allocator_res = context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.frames[frame_idx].command_allocator));
        ASSERT(SUCCEEDED(command_allocator_res));
    }

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        HRESULT cmd_list_res = context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.frames[frame_idx].command_allocator, NULL, IID_PPV_ARGS(&context.frames[frame_idx].command_list));
        ASSERT(SUCCEEDED(cmd_list_res));
        HRESULT close_res = context.frames[frame_idx].command_list->Close();
        ASSERT(SUCCEEDED(close_res));
    }

    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        HRESULT fence_res = context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context.frames[frame_idx].fence));
        ASSERT(SUCCEEDED(fence_res));

        context.frames[frame_idx].fence_wait_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    }

    context.current_frame_idx = context.swapchain->GetCurrentBackBufferIndex();

    return context;
}

GPU_UploadBuffer* createUploadBuffer(GPU_Context* gpu_context, usize size, Arena* arena) {
    GPU_UploadBuffer* result = pushStruct(arena, GPU_UploadBuffer);

    // NOTE: creates a CPU-side heap that can be used to transfer data to the GPU
    D3D12_HEAP_PROPERTIES upload_heap_props = {};
    upload_heap_props.Type = D3D12_HEAP_TYPE_UPLOAD; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
    upload_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    upload_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    upload_heap_props.CreationNodeMask = 1;
    upload_heap_props.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC upload_heap_resource_desc = {};
    upload_heap_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_heap_resource_desc.Alignment = 0;
    upload_heap_resource_desc.Width = size; // This is the only field that really matters.
    upload_heap_resource_desc.Height = 1;
    upload_heap_resource_desc.DepthOrArraySize = 1;
    upload_heap_resource_desc.MipLevels = 1;
    upload_heap_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    upload_heap_resource_desc.SampleDesc.Count = 1;
    upload_heap_resource_desc.SampleDesc.Quality = 0;
    upload_heap_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_heap_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT upload_buffer_creation_res = gpu_context->device->CreateCommittedResource(
        &upload_heap_props,
        D3D12_HEAP_FLAG_NONE,
        &upload_heap_resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, // this is from the GPU's perspective
        NULL,
        IID_PPV_ARGS(&result->resource)
    );
    ASSERT(SUCCEEDED(upload_buffer_creation_res));

    return result;
}

GPU_Buffer* createGPUBuffer(GPU_Context* gpu_context, usize size, GPU_BufferUsage usage, Arena* arena) {

    GPU_Buffer* result = pushStruct(arena, GPU_Buffer);

    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask = 1;
    heap_props.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC buffer_resource_desc = {};
    buffer_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_resource_desc.Alignment = 0;
    buffer_resource_desc.Width = size;
    buffer_resource_desc.Height = 1;
    buffer_resource_desc.DepthOrArraySize = 1;
    buffer_resource_desc.MipLevels = 1;
    buffer_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    buffer_resource_desc.SampleDesc.Count = 1;
    buffer_resource_desc.SampleDesc.Quality = 0;
    buffer_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT buffer_creation_res = gpu_context->device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &buffer_resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        IID_PPV_ARGS(&result->resource)
    );
    ASSERT(SUCCEEDED(buffer_creation_res));

    switch (usage) {
        case GPU_BUFFER_USAGE_VERTEX: {
            result->usage_state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        } break;
        default: {
            ASSERT(false);
        }
    }

    return result;
}

// NOTE: Passing NULL as the read range would mean we are gonna read the entire
// buffer, but for optimization and simplicity's sake let's say that the user
// should not reading from a mapped upload buffer.
u8* mapUploadBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* buffer) {
    void* mapped;
    D3D12_RANGE read_range = {};

    buffer->resource->Map(0, &read_range, &mapped);

    return (u8*)mapped;
}

void unmapUploadBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* buffer) {
    // NOTE: Passing NULL as the write range signals to the driver that we
    // we wrote the entire buffer.
    buffer->resource->Unmap(0, NULL);
}

void blockingUploadToGPUBuffer(GPU_Context* gpu_context, GPU_UploadBuffer* src_buffer, GPU_Buffer* dst_buffer, usize size) {
    // NOTE: Wait for the previous upload to have completed.
    if (gpu_context->copy_fence->GetCompletedValue() < gpu_context->copy_fence_ready_value) {
        gpu_context->copy_fence->SetEventOnCompletion(gpu_context->copy_fence_ready_value, gpu_context->copy_fence_wait_event);
        WaitForSingleObject(gpu_context->copy_fence_wait_event, INFINITE);
    }

    // NOTE: Prepare the command buffer for recording.
    HRESULT alloc_reset = gpu_context->copy_allocator->Reset();
    ASSERT(SUCCEEDED(alloc_reset));
    HRESULT cmd_reset = gpu_context->copy_command_list->Reset(gpu_context->copy_allocator, NULL);
    ASSERT(SUCCEEDED(cmd_reset));

    // NOTE: Signal to the driver that the destination buffer needs to be in a copy-ready state.
    D3D12_RESOURCE_BARRIER to_copy_dst_barrier = {};
    to_copy_dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_dst_barrier.Transition.pResource = dst_buffer->resource;
    to_copy_dst_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_copy_dst_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    to_copy_dst_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    gpu_context->copy_command_list->ResourceBarrier(1, &to_copy_dst_barrier);

    // NOTE: This is the actual copy command.
    gpu_context->copy_command_list->CopyBufferRegion(dst_buffer->resource, 0, src_buffer->resource, 0, size);

    // NOTE: The buffer is now no longer in the correct usage state and needs to be transitioned before the next usage.
    dst_buffer->in_usage_state = false;

    // NOTE: Close the command buffer and send it to the GPU for execution.
    HRESULT close_res = gpu_context->copy_command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    gpu_context->copy_command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&gpu_context->copy_command_list);

    // NOTE: Signal the copy fence once all commands on the copy queues have been executed.
    gpu_context->copy_command_queue->Signal(gpu_context->copy_fence, ++gpu_context->copy_fence_ready_value);

    // NOTE: Wait for the upload to have completed. This makes waiting at the beginning of the function technically
    // redundant, but eh.
    if (gpu_context->copy_fence->GetCompletedValue() < gpu_context->copy_fence_ready_value) {
        gpu_context->copy_fence->SetEventOnCompletion(gpu_context->copy_fence_ready_value, gpu_context->copy_fence_wait_event);
        WaitForSingleObject(gpu_context->copy_fence_wait_event, INFINITE);
    }
}

GPU_CommandBuffer* waitForCommandBuffer(GPU_Context* gpu_context, Arena* arena) {

    GPU_CommandBuffer* result = pushStruct(arena, GPU_CommandBuffer);

    // NOTE: Wait for the render commands sent the last time we used that backbuffer have
    // been completed, by sleeping until its value gets updated by the signal command we enqueued last time.
    FrameContext* current_frame = &gpu_context->frames[gpu_context->current_frame_idx];
    if(current_frame->fence->GetCompletedValue() < current_frame->fence_ready_value) {
        current_frame->fence->SetEventOnCompletion(current_frame->fence_ready_value, current_frame->fence_wait_event);
        WaitForSingleObject(current_frame->fence_wait_event, INFINITE);
    }

    // NOTE: Reset the frame's command allocator and list, now that they are no longer in use.
    HRESULT alloc_reset = current_frame->command_allocator->Reset();
    ASSERT(SUCCEEDED(alloc_reset));
    HRESULT cmd_reset = current_frame->command_list->Reset(current_frame->command_allocator, NULL);
    ASSERT(SUCCEEDED(cmd_reset));

    // NOTE: Indicate to the driver that the backbuffer needs to be available as a render target.
    D3D12_RESOURCE_BARRIER render_barrier = {};
    render_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    render_barrier.Transition.pResource = current_frame->render_target_resource;
    render_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    render_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    render_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &render_barrier);

    result->command_list = current_frame->command_list;
    return result;
}

void sendCommandBufferAndPresent(GPU_Context* gpu_context, GPU_CommandBuffer* command_buffer) {
    // WARNING: Since the burden of managing the multiple frames in flights is on the platform
    // layer, we don't actually need to take the command buffer here. We already know it's the
    // current frame's command buffer. Maybe this is indicative of a design flaw in the API ?
    // Or maybe we're just future proofing for when we allow the user to manually manage command buffers.

    FrameContext* current_frame = &gpu_context->frames[gpu_context->current_frame_idx];

    // NOTE: The backbuffer should be transitionned to be used for presentation.
    D3D12_RESOURCE_BARRIER present_barrier = {};
    present_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    present_barrier.Transition.pResource = current_frame->render_target_resource;
    present_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    present_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    present_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &present_barrier);

    // NOTE: Close the command list and send it to the GPU for execution.
    HRESULT close_res = command_buffer->command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    gpu_context->graphics_command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_buffer->command_list);

    // TODO: Sort the whole vsync situation out.
    gpu_context->swapchain->Present(1, 0);

    // NOTE: Signal this frame's fence when the graphics commands enqueued until now have completed.
    gpu_context->graphics_command_queue->Signal(current_frame->fence, ++current_frame->fence_ready_value);

    // NOTE: Get the next frame's index
    // TODO: Confirm that this is updated by the call to Present.
    gpu_context->current_frame_idx = gpu_context->swapchain->GetCurrentBackBufferIndex();
}

void recordClearCommand(GPU_Context* gpu_context, GPU_CommandBuffer* command_buffer, f32* clear_color) {
    FrameContext* current_frame = &gpu_context->frames[gpu_context->current_frame_idx];

    command_buffer->command_list->ClearRenderTargetView(current_frame->render_target_view_descriptor, clear_color, 0, NULL);
    command_buffer->command_list->ClearDepthStencilView(current_frame->depth_target_view_descriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
}
