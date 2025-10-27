#define NOMINMAX
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <strsafe.h>
#include "../firstparty/Microsoft/include/GameInput.h"

using namespace GameInput::v3;

#include "common.h"
#include "input.h"
#include "game_api.h"
#include "win32_gpu.h"

global b32 global_running = false;

// NOTE: due to the way Microsoft's GameInput works, we need to keep
// more state around than what we want to present to the user
struct WindowsInputState {
    InputState input_state;
    i64 mouse_accumulated_x;
    i64 mouse_accumulated_y;
};

/*
struct D3D12RenderPipeline {
    ID3D12RootSignature* root_signature;
    ID3D12PipelineState* pipeline_state;
};

struct D3D12VertexBuffer {
    ID3D12Resource* buffer;
    D3D12_VERTEX_BUFFER_VIEW view;
    u32 count;
};

D3D12RenderPipeline initDebugChunkPipeline(D3D12Context* d3d_context, b32 wireframe, b32 backface_culling) {
    D3D12RenderPipeline result = {};

    // NOTE: root signature with :
    // - vec4 color
    // - mat4 model
    // - mat4 camera (proj * view)
    D3D12_ROOT_PARAMETER root_parameters[3] = {};
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[0].Constants.Num32BitValues = 4;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[1].Constants.Num32BitValues = 16;
    root_parameters[1].Constants.ShaderRegister = 1;
    root_parameters[1].Constants.RegisterSpace = 0;

    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[2].Constants.Num32BitValues = 16;
    root_parameters[2].Constants.ShaderRegister = 2;
    root_parameters[2].Constants.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
    root_signature_desc.NumParameters = ARRAY_COUNT(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* serialized_signature;
    ID3DBlob* serialize_err;
    D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_signature, &serialize_err);
    ASSERT(serialize_err == NULL);
    HRESULT signature_res = d3d_context->device->CreateRootSignature(0, serialized_signature->GetBufferPointer(), serialized_signature->GetBufferSize(), IID_PPV_ARGS(&result.root_signature));
    ASSERT(SUCCEEDED(signature_res));

    // NOTE: shaders
    ID3DBlob* vertex_shader;
    ID3DBlob* pixel_shader;

    ID3DBlob* vertex_shader_err;
    ID3DBlob* pixel_shader_err;

    // FIXME : write a function to query the exe's directory instead of forcing a specific CWD
    HRESULT vertex_res = D3DCompileFromFile(L".\\shaders\\debug_chunk.hlsl", NULL, NULL, "VSMain", "vs_5_0", 0, 0, &vertex_shader, &vertex_shader_err);
    if (vertex_shader_err) {
        OutputDebugStringA((char*)vertex_shader_err->GetBufferPointer());
    }
    ASSERT(SUCCEEDED(vertex_res));

    HRESULT pixel_res = D3DCompileFromFile(L".\\shaders\\debug_chunk.hlsl", NULL, NULL, "PSMain", "ps_5_0", 0, 0, &pixel_shader, &pixel_shader_err);
    if (pixel_shader_err) {
        OutputDebugStringA((char*)pixel_shader_err->GetBufferPointer());
    }
    ASSERT(SUCCEEDED(pixel_res));

    // NOTE : pipeline stages config
    D3D12_INPUT_ELEMENT_DESC input_descriptions[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    rasterizer_desc.CullMode = backface_culling ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    rasterizer_desc.FrontCounterClockwise = true;
    rasterizer_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizer_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizer_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.MultisampleEnable = FALSE;
    rasterizer_desc.AntialiasedLineEnable = FALSE;
    rasterizer_desc.ForcedSampleCount = 0;
    rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depth_desc = {};
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depth_desc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.InputLayout.pInputElementDescs = input_descriptions;
    pipeline_desc.InputLayout.NumElements = ARRAY_COUNT(input_descriptions);

    pipeline_desc.pRootSignature = result.root_signature;

    pipeline_desc.VS.pShaderBytecode = vertex_shader->GetBufferPointer();
    pipeline_desc.VS.BytecodeLength = vertex_shader->GetBufferSize();
    pipeline_desc.PS.pShaderBytecode = pixel_shader->GetBufferPointer();
    pipeline_desc.PS.BytecodeLength = pixel_shader->GetBufferSize();

    pipeline_desc.RasterizerState = rasterizer_desc;
    pipeline_desc.BlendState = blend_desc;

    pipeline_desc.DepthStencilState = depth_desc;
    pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    pipeline_desc.SampleMask = UINT_MAX;
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 1;
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline_desc.SampleDesc.Count = 1;

    HRESULT pipeline_res = d3d_context->device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&result.pipeline_state));
    ASSERT(SUCCEEDED(pipeline_res));

    return result;
}

void renderFrame(Arena* draw_orders_arena, HWND window, D3D12Context* d3d_context, FrameContext* current_frame, D3D12RenderPipeline* solid_pipeline, D3D12RenderPipeline* wireframe_pipeline) {

    // NOTE: reset the frame's command allocator and list, now that they are no
    // longer in use
    HRESULT alloc_reset = current_frame->command_allocator->Reset();
    ASSERT(SUCCEEDED(alloc_reset));
    HRESULT cmd_reset = current_frame->command_list->Reset(current_frame->command_allocator, NULL);
    ASSERT(SUCCEEDED(cmd_reset));

    // indicate that the backbuffer needs to be available as a render target
    D3D12_RESOURCE_BARRIER render_barrier = {};
    render_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    render_barrier.Transition.pResource = d3d_context->render_target_resources[d3d_context->current_frame_idx];
    render_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    render_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    render_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &render_barrier);

    // set pipeline state
    // TODO: handle swapchain resize
    D3D12_VIEWPORT viewport = {};
    RECT clientRect;
    GetClientRect(window, &clientRect);
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = clientRect.right - clientRect.left;
    viewport.Height = clientRect.bottom - clientRect.top;
    viewport.MinDepth = 0.0;
    viewport.MaxDepth = 1.0;

    D3D12_RECT scissor = {};
    scissor.top = viewport.TopLeftY;
    scissor.bottom = viewport.Height;
    scissor.left = viewport.TopLeftX;
    scissor.right = viewport.Width;

    current_frame->command_list->SetPipelineState(solid_pipeline->pipeline_state);
    current_frame->command_list->SetGraphicsRootSignature(solid_pipeline->root_signature);
    current_frame->command_list->RSSetViewports(1, &viewport);
    current_frame->command_list->RSSetScissorRects(1, &scissor);

    // NOTE: get the descriptor for backbuffer clearing
    // TODO: maybe put that in the frame context directly ?
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view;
    render_target_view.ptr = d3d_context->rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + d3d_context->rtv_descriptor_size * d3d_context->current_frame_idx;
    D3D12_CPU_DESCRIPTOR_HANDLE depth_buffer_view;
    depth_buffer_view.ptr = d3d_context->dsv_heap->GetCPUDescriptorHandleForHeapStart().ptr + d3d_context->dsv_descriptor_size * d3d_context->current_frame_idx;

    // NOTE: clear and render
    f32 clear_color[] = {0.1, 0.1, 0.2, 1.0};
    current_frame->command_list->ClearRenderTargetView(render_target_view, clear_color, 0, NULL);
    current_frame->command_list->ClearDepthStencilView(depth_buffer_view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

    current_frame->command_list->OMSetRenderTargets(1, &render_target_view, FALSE, &depth_buffer_view);
    current_frame->command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (u32 i = 0; i < draw_orders_arena->used / sizeof(DrawOrder); i++) {
        DrawOrder* order = &((DrawOrder*)(draw_orders_arena->base))[i];

        switch (order->type) {
            case DRAW_ORDER_SOLID_PIPELINE: {
                current_frame->command_list->SetPipelineState(solid_pipeline->pipeline_state);
                current_frame->command_list->SetGraphicsRootSignature(solid_pipeline->root_signature);
            } break;
            case DRAW_ORDER_WIREFRAME_PIPELINE: {
                current_frame->command_list->SetPipelineState(wireframe_pipeline->pipeline_state);
                current_frame->command_list->SetGraphicsRootSignature(wireframe_pipeline->root_signature);
            } break;
            case DRAW_ORDER_LOOK_AT: {
                m4 view = lookAt(order->look_at.eye, order->look_at.target);
                m4 proj = makeProjection(0.1, 1000, order->look_at.fov);
                m4 combined = proj * view;
                current_frame->command_list->SetGraphicsRoot32BitConstants(2, 16, combined.data, 0);
            } break;
            case DRAW_ORDER_DEBUG_MESH: {
                current_frame->command_list->IASetVertexBuffers(0, 1, &global_debug_mesh_vertex_buffer.view);
                v4 color = {1, 1, 1, 1};
                current_frame->command_list->SetGraphicsRoot32BitConstants(0, 4, &color, 0);

                m4 translation = makeTranslation(order->debug_mesh.position);
                m4 scale = makeScale(1, 1, 1);
                m4 combined = translation * scale;
                current_frame->command_list->SetGraphicsRoot32BitConstants(1, 16, combined.data, 0);
                current_frame->command_list->DrawInstanced(global_debug_mesh_vertex_buffer.count, 1, 0, 0);
            } break;
            default:
                ASSERT(false);
        }
    }

    // NOTE: the backbuffer will now be prepared to be used for presentation
    D3D12_RESOURCE_BARRIER present_barrier = {};
    present_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    present_barrier.Transition.pResource = d3d_context->render_target_resources[d3d_context->current_frame_idx];
    present_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    present_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    present_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &present_barrier);

    // NOTE: close the command list and send it to the GPU for execution
    HRESULT close_res = current_frame->command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    d3d_context->command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&current_frame->command_list);

    // TODO: sort the whole vsync situation
    d3d_context->swapchain->Present(1, 0);

    // NOTE: signal this frame's fence when the commands enqueued until now have completed
    d3d_context->command_queue->Signal(current_frame->fence, ++current_frame->fence_ready_value);

    // NOTE: get the next frame's index
    // TODO: confirm that this is updated by the call to Present
    d3d_context->current_frame_idx = d3d_context->swapchain->GetCurrentBackBufferIndex();
}

void debugUploadMeshBlocking(f32* data, usize size) {

    if (!global_debug_mesh_vertex_buffer.buffer) {
        // NOTE: Create VRAM buffer of worst-case size so it can be reused every time we
        // want to update that single debug mesh.
        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask = 1;
        heap_props.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vertex_buffer_resource_desc = {};
        vertex_buffer_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vertex_buffer_resource_desc.Alignment = 0;
        vertex_buffer_resource_desc.Width = (16 * 16 * 16) / 2 * 6 * 2 * 6 * sizeof(v3); // FIXME: magic numbers here
        vertex_buffer_resource_desc.Height = 1;
        vertex_buffer_resource_desc.DepthOrArraySize = 1;
        vertex_buffer_resource_desc.MipLevels = 1;
        vertex_buffer_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        vertex_buffer_resource_desc.SampleDesc.Count = 1;
        vertex_buffer_resource_desc.SampleDesc.Quality = 0;
        vertex_buffer_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vertex_buffer_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT vbuffer_creation_res = global_d3d_context->device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &vertex_buffer_resource_desc,
            D3D12_RESOURCE_STATE_COMMON,
            NULL,
            IID_PPV_ARGS(&global_debug_mesh_vertex_buffer.buffer)
        );
        ASSERT(SUCCEEDED(vbuffer_creation_res));
    }

    // NOTE: CPU-side heap used during upload
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

    ID3D12Resource* upload_buffer;
    HRESULT upload_buffer_creation_res = global_d3d_context->device->CreateCommittedResource(
        &upload_heap_props,
        D3D12_HEAP_FLAG_NONE,
        &upload_heap_resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, // this is from the GPU's perspective
        NULL,
        IID_PPV_ARGS(&upload_buffer)
    );
    ASSERT(SUCCEEDED(upload_buffer_creation_res));

    // NOTE: Copy the vertex data to the upload buffer. Passing NULL as the read range
    // would mean we are gonna read the entire buffer, but we not reading any of it.
    void* mapped;
    D3D12_RANGE read_range = {};
    upload_buffer->Map(0, &read_range, &mapped);
    for (usize i = 0; i < size; i++) {
        ((u8*)mapped)[i] = ((u8*)data)[i];
    }
    upload_buffer->Unmap(0, NULL); // Passing NULL as the write range because we wrote the entire buffer.

    // NOTE: This is horrible, but use the current frame's command buffer to begin the transfer.
    // WARNING: This only works because we're waiting for the queued up commands to be completed
    // before calling gameUpdate, which we shouldn't do anyways. Use a separate command queue for uploads, maybe ?
    FrameContext* current_frame = &global_d3d_context->frames[global_d3d_context->current_frame_idx];

    HRESULT alloc_reset = current_frame->command_allocator->Reset();
    ASSERT(SUCCEEDED(alloc_reset));
    HRESULT cmd_reset = current_frame->command_list->Reset(current_frame->command_allocator, NULL);
    ASSERT(SUCCEEDED(cmd_reset));

    D3D12_RESOURCE_BARRIER to_copy_dest_barrier = {};
    to_copy_dest_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_dest_barrier.Transition.pResource = global_debug_mesh_vertex_buffer.buffer;
    to_copy_dest_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_copy_dest_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    to_copy_dest_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &to_copy_dest_barrier);

    current_frame->command_list->CopyBufferRegion(global_debug_mesh_vertex_buffer.buffer, 0, upload_buffer, 0, size);

    D3D12_RESOURCE_BARRIER to_vertex_buffer_barrier = {};
    to_vertex_buffer_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_vertex_buffer_barrier.Transition.pResource = global_debug_mesh_vertex_buffer.buffer;
    to_vertex_buffer_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    to_vertex_buffer_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    to_vertex_buffer_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &to_vertex_buffer_barrier);

    // NOTE: close the command list and send it to the GPU for execution
    HRESULT close_res = current_frame->command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    global_d3d_context->command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&current_frame->command_list);

    // FIXME: obviously stalling the whole program during GPU upload is a bad idea
    global_d3d_context->command_queue->Signal(current_frame->fence, ++current_frame->fence_ready_value);
    if(current_frame->fence->GetCompletedValue() < current_frame->fence_ready_value) {
        current_frame->fence->SetEventOnCompletion(current_frame->fence_ready_value, current_frame->fence_wait_event);
        WaitForSingleObject(current_frame->fence_wait_event, INFINITE);
    }

    upload_buffer->Release();

    // FIXME: magic numbers
    global_debug_mesh_vertex_buffer.view.BufferLocation = global_debug_mesh_vertex_buffer.buffer->GetGPUVirtualAddress();
    global_debug_mesh_vertex_buffer.view.StrideInBytes = 6 * sizeof(f32);
    global_debug_mesh_vertex_buffer.view.SizeInBytes = size;
    global_debug_mesh_vertex_buffer.count = size / (6 * sizeof(f32));
}
*/

struct TimingInfo {
    i64 timestamp;
    i64 timestamp_frequency;

    f32 last_frame_to_frame_seconds;
};

TimingInfo initTimingInfo() {
    TimingInfo result = {};

    LARGE_INTEGER ctr;
    LARGE_INTEGER freq;
    QueryPerformanceCounter(&ctr);
    QueryPerformanceFrequency(&freq);

    result.timestamp = ctr.QuadPart;
    result.timestamp_frequency = freq.QuadPart;

    return result;
}

void measureTimingInfo(TimingInfo* info) {
    i64 last = info->timestamp;
    i64 now;

    LARGE_INTEGER ctr;
    QueryPerformanceCounter(&ctr);
    now = ctr.QuadPart;

    info->timestamp = now;

    i64 frame_to_frame_cycles = now - last;
    info->last_frame_to_frame_seconds = frame_to_frame_cycles / (f32)info->timestamp_frequency;
}

void printTimingInfo(TimingInfo* info) {
    // TODO: write my own logging / formatting subsystem
    char print_buffer[256];
    StringCbPrintfA(print_buffer, ARRAY_COUNT(print_buffer), "Frame-to-frame: %.02f (ms)\n", info->last_frame_to_frame_seconds * 1000.f);

    OutputDebugStringA(print_buffer);
}

void pollGameInput(HWND window, IGameInput* game_input, WindowsInputState* previous_input_state, WindowsInputState* current_input_state) {

    // NOTE: Clear the current input state since we are polling the whole state every frame.
    *current_input_state = {};

    IGameInputReading* kb_reading;
    HRESULT kb_reading_res = game_input->GetCurrentReading(GameInputKindKeyboard, nullptr, &kb_reading);

    if (SUCCEEDED(kb_reading_res)) {
        current_input_state->input_state.is_analog = false;

        // NOTE: That stack array has room for up to 16 different keys pressed
        // at the same time, but I think most keyboards do not support that much
        // in the first place.
        GameInputKeyState key_states[16];
        u32 key_states_count = kb_reading->GetKeyCount();
        ASSERT(key_states_count < ARRAY_COUNT(key_states));
        kb_reading->GetKeyState(key_states_count, key_states);

        for (u32 i = 0; i < key_states_count; i++) {
            GameInputKeyState key = key_states[i];

            ASSERT(key.scanCode < SCANCODE_COUNT);
            current_input_state->input_state.kb.keys[key.scanCode].is_down = true;
            current_input_state->input_state.kb.keys[key.scanCode].transitions = (current_input_state->input_state.kb.keys[key.scanCode].is_down != previous_input_state->input_state.kb.keys[key.scanCode].is_down);
        }

        kb_reading->Release();
    }

    IGameInputReading* mouse_reading;
    HRESULT mouse_reading_res = game_input->GetCurrentReading(GameInputKindMouse, nullptr, &mouse_reading);

    if (SUCCEEDED(mouse_reading_res)) {
        current_input_state->input_state.is_analog = false;

        GameInputMouseState mouse_state;
        mouse_reading->GetMouseState(&mouse_state);

        // TODO: normalized mouse coordinates relative to the window
        // (0, 0) -> top left
        // (1, 1) -> bottom right
        // TODO: should this be clamped ?
        //RECT window_rect;
        //GetWindowRect(window, &window_rect);
        //f32 rel_mouse_x = (f32)(mouse_state.absolutePositionX - window_rect.left) / (f32) (window_rect.right - window_rect.left);
        //f32 rel_mouse_y = (f32)(mouse_state.absolutePositionY - window_rect.top) / (f32) (window_rect.bottom - window_rect.top);
        //current_input_state->kb.mouse_screen_pos = v2 {rel_mouse_x, rel_mouse_y};

        // NOTE: mouse movement delta
        // TODO: do we want it in pixels ? or normalized ? meaning should a smaller window have more sensitivity ?
        current_input_state->mouse_accumulated_x = mouse_state.positionX;
        current_input_state->mouse_accumulated_y = mouse_state.positionY;
        i64 delta_x = current_input_state->mouse_accumulated_x - previous_input_state->mouse_accumulated_x;
        i64 delta_y = current_input_state->mouse_accumulated_y - previous_input_state->mouse_accumulated_y;
        current_input_state->input_state.kb.mouse_delta = v2 {(f32) delta_x, (f32) -delta_y};

        mouse_reading->Release();
    }

    IGameInputReading* pad_reading;
    HRESULT pad_reading_res = game_input->GetCurrentReading(GameInputKindGamepad, nullptr, &pad_reading);

    if (SUCCEEDED(pad_reading_res)) {
        // TODO: this is not enough to tell if the input should be analog or keyboard
        // since we have a reading every frame when a controller is plugged in no matter if the user actually touched his controller or not
        // so the current behavior is that the input is considered analog if there is a gamepad connected
        // this could be fixed by using the event-driven GameInput API? or doing a diff with polling
        current_input_state->input_state.is_analog = true;

        GameInputGamepadState pad_state;
        pad_reading->GetGamepadState(&pad_state);

        current_input_state->input_state.ctrl.a.is_down = (pad_state.buttons & GameInputGamepadA) == GameInputGamepadA;
        current_input_state->input_state.ctrl.a.transitions = (current_input_state->input_state.ctrl.a.is_down != previous_input_state->input_state.ctrl.a.is_down);

        current_input_state->input_state.ctrl.b.is_down = (pad_state.buttons & GameInputGamepadB) == GameInputGamepadB;
        current_input_state->input_state.ctrl.b.transitions = (current_input_state->input_state.ctrl.b.is_down != previous_input_state->input_state.ctrl.b.is_down);

        current_input_state->input_state.ctrl.x.is_down = (pad_state.buttons & GameInputGamepadX) == GameInputGamepadX;
        current_input_state->input_state.ctrl.x.transitions = (current_input_state->input_state.ctrl.x.is_down != previous_input_state->input_state.ctrl.x.is_down);

        current_input_state->input_state.ctrl.y.is_down = (pad_state.buttons & GameInputGamepadY) == GameInputGamepadY;
        current_input_state->input_state.ctrl.y.transitions = (current_input_state->input_state.ctrl.y.is_down != previous_input_state->input_state.ctrl.y.is_down);

        current_input_state->input_state.ctrl.lb.is_down = (pad_state.buttons & GameInputGamepadRightShoulder) == GameInputGamepadRightShoulder;
        current_input_state->input_state.ctrl.lb.transitions = (current_input_state->input_state.ctrl.lb.is_down != previous_input_state->input_state.ctrl.lb.is_down);

        current_input_state->input_state.ctrl.rb.is_down = (pad_state.buttons & GameInputGamepadLeftShoulder) == GameInputGamepadLeftShoulder;
        current_input_state->input_state.ctrl.rb.transitions = (current_input_state->input_state.ctrl.rb.is_down != previous_input_state->input_state.ctrl.rb.is_down);

        // TODO: deadzone handling
        current_input_state->input_state.ctrl.left_stick = v2 {pad_state.leftThumbstickX, pad_state.leftThumbstickY};
        current_input_state->input_state.ctrl.right_stick = v2 {pad_state.rightThumbstickX, pad_state.rightThumbstickY};

        pad_reading->Release();
    }
}

typedef decltype(&gameUpdate) GameUpdatePtr;

struct GameCode {
    bool is_valid;
    HMODULE dll_handle;
    FILETIME write_time;

    GameUpdatePtr game_update;
};

FILETIME getFileLastWriteTime(const char* filename) {
    // TODO: how to handle failure ?
    WIN32_FILE_ATTRIBUTE_DATA file_data = {};
    GetFileAttributesExA(filename, GetFileExInfoStandard, &file_data);
    return file_data.ftLastWriteTime;
}

void loadGameCode(GameCode* game_code, const char* src_dll_name) {
    *game_code = {};
    game_code->write_time = getFileLastWriteTime(src_dll_name);

    const char* tmp_dll_name = "game_tmp.dll";
    CopyFile(src_dll_name, tmp_dll_name, false);

    game_code->dll_handle = LoadLibraryA(tmp_dll_name);

    if (game_code->dll_handle) {
        game_code->game_update = (GameUpdatePtr)GetProcAddress(game_code->dll_handle, "gameUpdate");
        if (game_code->game_update != NULL) {
            game_code->is_valid = true;
        } else {
            game_code->is_valid = false;
        }
    } else {
        game_code->is_valid = false;
    }
}

void unloadGameCode(GameCode* game_code) {
    if (game_code->dll_handle) {
        FreeLibrary(game_code->dll_handle);
    }
    *game_code = {};
}

// NOTE: The WIN32 callback model is annoying, so the only thing this one does
// for now is to apply default behavior and call PostQuitMessage in case of WM_DESTROY
// event. This allows a blocking GetMessage loop to exit gracefully instead of keeping
// the process running after the window is destroyed. I am using PeekMessage, but it is
// better to be foolproof just in case.
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        } break;

        default:
            return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    // NOTE: create and register the window class
    WNDCLASSA window_class = {};
    window_class.hInstance = hInstance;
    window_class.lpszClassName = "Win32 Window Class";
    window_class.lpfnWndProc = WindowProc;
    RegisterClassA(&window_class);

    HWND window = CreateWindowExA(
        /* behavior */ 0,
        window_class.lpszClassName,
        "WIN32 Window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        /* position & size */ CW_USEDEFAULT, CW_USEDEFAULT, 800, 800,
        /* parent window */ NULL,
        /* menu */ NULL,
        hInstance,
        /* userdata ptr */ NULL
    );

    if (window == NULL) {
        // TODO: logging
        return 1;
    }

    global_running = true;

    GPU_Context d3d_context = initD3D12(window, true);
    //D3D12RenderPipeline solid_color_pipeline = initDebugChunkPipeline(&d3d_context, false, true);
    //D3D12RenderPipeline wireframe_pipeline = initDebugChunkPipeline(&d3d_context, true, false);

    TimingInfo timing_info = initTimingInfo();
    GameMemory game_memory = {};
    game_memory.permanent_storage_size = MEGABYTES(64);
    game_memory.permanent_storage = VirtualAlloc((void*)TERABYTES(2), game_memory.permanent_storage_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ASSERT(game_memory.permanent_storage != NULL);

    GameCode game_code = {};

    PlatformAPI platform_api = {};
    platform_api.createUploadBuffer = createUploadBuffer;
    platform_api.createGPUBuffer = createGPUBuffer;
    platform_api.mapUploadBuffer = mapUploadBuffer;
    platform_api.unmapUploadBuffer = unmapUploadBuffer;
    platform_api.blockingUploadToGPUBuffer = blockingUploadToGPUBuffer;
    platform_api.waitForCommandBuffer = waitForCommandBuffer;
    platform_api.sendCommandBufferAndPresent = sendCommandBufferAndPresent;
    platform_api.recordClearCommand = recordClearCommand;

    // NOTE: game input double-buffering
    // i think casey does this to prepare for asynchronous event
    // collection in a separate thread
    WindowsInputState input_states[2] = {};
    WindowsInputState* current_input_state = &input_states[0];
    WindowsInputState* previous_input_state = &input_states[1];

    IGameInput* microsoft_game_input_interface;
    HRESULT ginput_create_res = GameInputCreate(&microsoft_game_input_interface);
    ASSERT(SUCCEEDED(ginput_create_res));

    while (global_running) {

        // NOTE: measure and print the time the previous frame took
        measureTimingInfo(&timing_info);
        printTimingInfo(&timing_info);

        // NOTE: reload game code
        const char* dll_name = "game.dll";
        FILETIME dll_time = getFileLastWriteTime(dll_name);
        if (CompareFileTime(&dll_time, &game_code.write_time) != 0) {
            unloadGameCode(&game_code);
            loadGameCode(&game_code, dll_name);
        }

        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) {
                global_running = false;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        pollGameInput(window, microsoft_game_input_interface, previous_input_state, current_input_state);

        if (game_code.is_valid) {
            // TODO: compute the actual dt
            game_code.game_update(1.f/60.f, &d3d_context, &platform_api, &game_memory, &current_input_state->input_state);
        }

        // NOTE: Swap the user input buffers
        WindowsInputState* tmp = current_input_state;
        current_input_state = previous_input_state;
        previous_input_state = tmp;
    }

    return 0;
}
