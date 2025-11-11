#include <d3d12.h>
#include <d3dcommon.h>
#include <debugapi.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <strsafe.h>

#include "common.h"
#include "game_api.h"
#include "maths.h"
#include "noise.h"
#include "img.h"
#include "world.h"

constexpr usize FRAMES_IN_FLIGHT = 2;

struct D3DFrameContext {
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

struct D3DContext {
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

    D3DFrameContext frames[FRAMES_IN_FLIGHT];
    u32 current_frame_idx;
};

struct D3DPipeline {
    ID3D12RootSignature* root_signature;
    ID3D12PipelineState* pipeline_state;
};

D3DContext initializeD3D12(b32 debug_mode) {
    // WARNING: This is error prone, but it's annoying to have to pass the HWND
    // to the game layer just for that.
    HWND window_handle = FindWindowA("Voxel Game Window Class", nullptr);

    D3DContext context = {};
    context.window = window_handle;

    // FIXME: release all the COM objects.

    if (debug_mode) {
        HRESULT debug_res = D3D12GetDebugInterface(IID_PPV_ARGS(&context.debug_interface));
        ASSERT(SUCCEEDED(debug_res));
        context.debug_interface->EnableDebugLayer();
    };

    // NOTE: The factory is used to query for the right GPU device.
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

    // NOTE: Create the graphics queue.
    D3D12_COMMAND_QUEUE_DESC graphics_queue_desc = {};
    graphics_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT graphics_queue_res = context.device->CreateCommandQueue(&graphics_queue_desc, IID_PPV_ARGS(&context.graphics_command_queue));
    ASSERT(SUCCEEDED(graphics_queue_res));

    // NOTE: Create the copy queue, and all the command stuff since
    // this queue is global and not (yet) per-frame.
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

    // NOTE: Get the swapchain from DXGI.
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

    // NOTE: Create a descriptor heap for our render target views.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = FRAMES_IN_FLIGHT;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT rtv_heap_res = context.device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&context.rtv_heap));
    ASSERT(SUCCEEDED(rtv_heap_res));
    context.rtv_descriptor_size = context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // NOTE: Fill the heap with our 2 render target views.
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

    // NOTE: Kinda the same thing for depth buffers, except we need to create them since they are not managed by DXGI.
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

    // NOTE: Create a descriptor heap for our depth/stencil views.
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.NumDescriptors = FRAMES_IN_FLIGHT;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT dsv_heap_res = context.device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&context.dsv_heap));
    ASSERT(SUCCEEDED(dsv_heap_res));
    context.dsv_descriptor_size = context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // NOTE: Fill the descriptor heap with our 2 depth buffer views.
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

    // NOTE: For each frame in flight, create a command allocator, list, and a fence to wait on.
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

    // NOTE: Update the current frame index.
    context.current_frame_idx = context.swapchain->GetCurrentBackBufferIndex();

    return context;
}

// TODO: This should handle errors gracefully. Maybe a default shader enbedded in the exe ?
void readAndCompileShaders(const char* path, ID3DBlob** vertex_shader, ID3DBlob** fragment_shader) {

    // FIXME : write a function to query the exe's directory instead of forcing a specific CWD
    wchar_t windows_path[128];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, windows_path, ARRAY_COUNT(windows_path));

    ID3DBlob* vertex_shader_compilation_err;
    HRESULT vertex_shader_err = D3DCompileFromFile(windows_path, NULL, NULL, "VSMain", "vs_5_0", 0, 0, vertex_shader, &vertex_shader_compilation_err);
    if (vertex_shader_compilation_err) {
        OutputDebugStringA((char*)vertex_shader_compilation_err->GetBufferPointer());
    }
    ASSERT(SUCCEEDED(vertex_shader_err));

    ID3DBlob* fragment_shader_compilation_err;
    HRESULT fragment_shader_err = D3DCompileFromFile(windows_path, NULL, NULL, "PSMain", "ps_5_0", 0, 0, fragment_shader, &fragment_shader_compilation_err);
    if (fragment_shader_compilation_err) {
        OutputDebugStringA((char*)fragment_shader_compilation_err->GetBufferPointer());
    }
    ASSERT(SUCCEEDED(fragment_shader_err));
}

void createChunkRenderPipelines(D3DContext* d3d_context, D3DPipeline* chunk_pipeline, D3DPipeline* wireframe_pipeline) {
        ID3DBlob* chunk_vertex_shader;
        ID3DBlob* chunk_fragment_shader;
        readAndCompileShaders("./shaders/debug_chunk.hlsl", &chunk_vertex_shader, &chunk_fragment_shader);

        ID3DBlob* wireframe_vertex_shader;
        ID3DBlob* wireframe_fragment_shader;
        readAndCompileShaders("./shaders/solid_color.hlsl", &wireframe_vertex_shader, &wireframe_fragment_shader);

        // NOTE: Root signature with 2 constants :
        // - Model matrix
        // - Fused perspective * view matrix
        D3D12_ROOT_PARAMETER chunk_root_parameters[2];
        chunk_root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        chunk_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        chunk_root_parameters[0].Constants.Num32BitValues = 16;
        chunk_root_parameters[0].Constants.ShaderRegister = 0;
        chunk_root_parameters[0].Constants.RegisterSpace = 0;
        chunk_root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        chunk_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        chunk_root_parameters[1].Constants.Num32BitValues = 16;
        chunk_root_parameters[1].Constants.ShaderRegister = 1;
        chunk_root_parameters[1].Constants.RegisterSpace = 0;

        D3D12_ROOT_SIGNATURE_DESC chunk_root_signature_desc = {};
        chunk_root_signature_desc.NumParameters = ARRAY_COUNT(chunk_root_parameters);
        chunk_root_signature_desc.pParameters = chunk_root_parameters;
        chunk_root_signature_desc.NumStaticSamplers = 0;
        chunk_root_signature_desc.pStaticSamplers = NULL;
        chunk_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* chunk_serialized_signature;
        ID3DBlob* chunk_serialize_err;
        D3D12SerializeRootSignature(&chunk_root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &chunk_serialized_signature, &chunk_serialize_err);
        if (chunk_serialize_err) {
            OutputDebugStringA((char*)chunk_serialize_err->GetBufferPointer());
        }
        ASSERT(chunk_serialize_err == NULL);
        HRESULT chunk_signature_res = d3d_context->device->CreateRootSignature(0, chunk_serialized_signature->GetBufferPointer(), chunk_serialized_signature->GetBufferSize(), IID_PPV_ARGS(&chunk_pipeline->root_signature));
        ASSERT(SUCCEEDED(chunk_signature_res));

        // NOTE: Root signature with 3 constants :
        // - Model matrix
        // - Fused perspective * view matrix
        // - Wireframe color
        D3D12_ROOT_PARAMETER wireframe_root_parameters[3];
        wireframe_root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        wireframe_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        wireframe_root_parameters[0].Constants.Num32BitValues = 16;
        wireframe_root_parameters[0].Constants.ShaderRegister = 0;
        wireframe_root_parameters[0].Constants.RegisterSpace = 0;
        wireframe_root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        wireframe_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        wireframe_root_parameters[1].Constants.Num32BitValues = 16;
        wireframe_root_parameters[1].Constants.ShaderRegister = 1;
        wireframe_root_parameters[1].Constants.RegisterSpace = 0;
        wireframe_root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        wireframe_root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        wireframe_root_parameters[2].Constants.Num32BitValues = 4;
        wireframe_root_parameters[2].Constants.ShaderRegister = 2;
        wireframe_root_parameters[2].Constants.RegisterSpace = 0;

        D3D12_ROOT_SIGNATURE_DESC wireframe_root_signature_desc = {};
        wireframe_root_signature_desc.NumParameters = ARRAY_COUNT(wireframe_root_parameters);
        wireframe_root_signature_desc.pParameters = wireframe_root_parameters;
        wireframe_root_signature_desc.NumStaticSamplers = 0;
        wireframe_root_signature_desc.pStaticSamplers = NULL;
        wireframe_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* wireframe_serialized_signature;
        ID3DBlob* wireframe_serialize_err;
        D3D12SerializeRootSignature(&wireframe_root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &wireframe_serialized_signature, &wireframe_serialize_err);
        if (wireframe_serialize_err) {
            OutputDebugStringA((char*)wireframe_serialize_err->GetBufferPointer());
        }
        ASSERT(wireframe_serialize_err == NULL);
        HRESULT wireframe_signature_res = d3d_context->device->CreateRootSignature(0, wireframe_serialized_signature->GetBufferPointer(), wireframe_serialized_signature->GetBufferSize(), IID_PPV_ARGS(&wireframe_pipeline->root_signature));
        ASSERT(SUCCEEDED(wireframe_signature_res));

        // NOTE: The normal render pipeline and the wireframe pipeline use the same
        // vertex buffers, so the same input stage config.
        D3D12_INPUT_ELEMENT_DESC shared_input_descriptions[2];
        shared_input_descriptions[0].SemanticName = "POSITION";
        shared_input_descriptions[0].SemanticIndex = 0;
        shared_input_descriptions[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
        shared_input_descriptions[0].InputSlot = 0;
        shared_input_descriptions[0].AlignedByteOffset = 0;
        shared_input_descriptions[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        shared_input_descriptions[0].InstanceDataStepRate = 0;

        shared_input_descriptions[1].SemanticName = "NORMAL";
        shared_input_descriptions[1].SemanticIndex = 0;
        shared_input_descriptions[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
        shared_input_descriptions[1].InputSlot = 0;
        shared_input_descriptions[1].AlignedByteOffset = 3 * sizeof(f32);
        shared_input_descriptions[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        shared_input_descriptions[1].InstanceDataStepRate = 0;

        D3D12_RASTERIZER_DESC chunk_rasterizer_desc = {};
        chunk_rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
        chunk_rasterizer_desc.CullMode = D3D12_CULL_MODE_BACK;
        chunk_rasterizer_desc.FrontCounterClockwise = true;
        chunk_rasterizer_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        chunk_rasterizer_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        chunk_rasterizer_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        chunk_rasterizer_desc.DepthClipEnable = TRUE;
        chunk_rasterizer_desc.MultisampleEnable = FALSE;
        chunk_rasterizer_desc.AntialiasedLineEnable = FALSE;
        chunk_rasterizer_desc.ForcedSampleCount = 0;
        chunk_rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_RASTERIZER_DESC wireframe_rasterizer_desc = {};
        wireframe_rasterizer_desc.FillMode = D3D12_FILL_MODE_WIREFRAME;
        wireframe_rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;
        wireframe_rasterizer_desc.FrontCounterClockwise = true;
        wireframe_rasterizer_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        wireframe_rasterizer_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        wireframe_rasterizer_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        wireframe_rasterizer_desc.DepthClipEnable = TRUE;
        wireframe_rasterizer_desc.MultisampleEnable = FALSE;
        wireframe_rasterizer_desc.AntialiasedLineEnable = FALSE;
        wireframe_rasterizer_desc.ForcedSampleCount = 0;
        wireframe_rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_BLEND_DESC shared_blend_desc = {};
        shared_blend_desc.AlphaToCoverageEnable = FALSE;
        shared_blend_desc.IndependentBlendEnable = FALSE;
        shared_blend_desc.RenderTarget[0].BlendEnable = FALSE;
        shared_blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
        shared_blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        shared_blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        shared_blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        shared_blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        shared_blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        shared_blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        shared_blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        shared_blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_DEPTH_STENCIL_DESC shared_depth_desc = {};
        shared_depth_desc.DepthEnable = TRUE;
        shared_depth_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        shared_depth_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        shared_depth_desc.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC chunk_pipeline_desc = {};
        chunk_pipeline_desc.InputLayout.pInputElementDescs = shared_input_descriptions;
        chunk_pipeline_desc.InputLayout.NumElements = ARRAY_COUNT(shared_input_descriptions);
        chunk_pipeline_desc.pRootSignature = chunk_pipeline->root_signature;
        chunk_pipeline_desc.VS.pShaderBytecode = chunk_vertex_shader->GetBufferPointer();
        chunk_pipeline_desc.VS.BytecodeLength = chunk_vertex_shader->GetBufferSize();
        chunk_pipeline_desc.PS.pShaderBytecode = chunk_fragment_shader->GetBufferPointer();
        chunk_pipeline_desc.PS.BytecodeLength = chunk_fragment_shader->GetBufferSize();
        chunk_pipeline_desc.RasterizerState = chunk_rasterizer_desc;
        chunk_pipeline_desc.BlendState = shared_blend_desc;
        chunk_pipeline_desc.DepthStencilState = shared_depth_desc;
        chunk_pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        chunk_pipeline_desc.SampleMask = UINT_MAX;
        chunk_pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        chunk_pipeline_desc.NumRenderTargets = 1;
        chunk_pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        chunk_pipeline_desc.SampleDesc.Count = 1;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframe_pipeline_desc = {};
        wireframe_pipeline_desc.InputLayout.pInputElementDescs = shared_input_descriptions;
        wireframe_pipeline_desc.InputLayout.NumElements = ARRAY_COUNT(shared_input_descriptions);
        wireframe_pipeline_desc.pRootSignature = wireframe_pipeline->root_signature;
        wireframe_pipeline_desc.VS.pShaderBytecode = wireframe_vertex_shader->GetBufferPointer();
        wireframe_pipeline_desc.VS.BytecodeLength = wireframe_vertex_shader->GetBufferSize();
        wireframe_pipeline_desc.PS.pShaderBytecode = wireframe_fragment_shader->GetBufferPointer();
        wireframe_pipeline_desc.PS.BytecodeLength = wireframe_fragment_shader->GetBufferSize();
        wireframe_pipeline_desc.RasterizerState = wireframe_rasterizer_desc;
        wireframe_pipeline_desc.BlendState = shared_blend_desc;
        wireframe_pipeline_desc.DepthStencilState = shared_depth_desc;
        wireframe_pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        wireframe_pipeline_desc.SampleMask = UINT_MAX;
        wireframe_pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        wireframe_pipeline_desc.NumRenderTargets = 1;
        wireframe_pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        wireframe_pipeline_desc.SampleDesc.Count = 1;

        HRESULT chunk_pipeline_res = d3d_context->device->CreateGraphicsPipelineState(&chunk_pipeline_desc, IID_PPV_ARGS(&chunk_pipeline->pipeline_state));
        ASSERT(SUCCEEDED(chunk_pipeline_res));
        HRESULT wireframe_pipeline_res = d3d_context->device->CreateGraphicsPipelineState(&wireframe_pipeline_desc, IID_PPV_ARGS(&wireframe_pipeline->pipeline_state));
        ASSERT(SUCCEEDED(wireframe_pipeline_res));

        chunk_vertex_shader->Release();
        chunk_fragment_shader->Release();
        wireframe_vertex_shader->Release();
        wireframe_fragment_shader->Release();

        chunk_serialized_signature->Release();
        wireframe_serialized_signature->Release();
}

void blockingUploadToGPUBuffer(D3DContext* d3d_context, ID3D12Resource* src_buffer, ID3D12Resource* dst_buffer, usize size) {
    // NOTE: Wait for the previous upload to have completed.
    if (d3d_context->copy_fence->GetCompletedValue() < d3d_context->copy_fence_ready_value) {
        d3d_context->copy_fence->SetEventOnCompletion(d3d_context->copy_fence_ready_value, d3d_context->copy_fence_wait_event);
        WaitForSingleObject(d3d_context->copy_fence_wait_event, INFINITE);
    }

    // NOTE: Prepare the command buffer for recording.
    HRESULT alloc_reset = d3d_context->copy_allocator->Reset();
    ASSERT(SUCCEEDED(alloc_reset));
    HRESULT cmd_reset = d3d_context->copy_command_list->Reset(d3d_context->copy_allocator, NULL);
    ASSERT(SUCCEEDED(cmd_reset));

    // NOTE: Signal to the driver that the destination buffer needs to be in a copy-ready state.
    D3D12_RESOURCE_BARRIER to_copy_dst_barrier = {};
    to_copy_dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_dst_barrier.Transition.pResource = dst_buffer;
    to_copy_dst_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_copy_dst_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    to_copy_dst_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    d3d_context->copy_command_list->ResourceBarrier(1, &to_copy_dst_barrier);

    // NOTE: This is the actual copy command.
    d3d_context->copy_command_list->CopyBufferRegion(dst_buffer, 0, src_buffer, 0, size);

    // NOTE: Close the command buffer and send it to the GPU for execution.
    HRESULT close_res = d3d_context->copy_command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    d3d_context->copy_command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&d3d_context->copy_command_list);

    // NOTE: Signal the copy fence once all commands on the copy queues have been executed.
    d3d_context->copy_command_queue->Signal(d3d_context->copy_fence, ++d3d_context->copy_fence_ready_value);

    // NOTE: Wait for the upload to have completed. This makes waiting at the beginning of the function technically
    // redundant, but eh.
    if (d3d_context->copy_fence->GetCompletedValue() < d3d_context->copy_fence_ready_value) {
        d3d_context->copy_fence->SetEventOnCompletion(d3d_context->copy_fence_ready_value, d3d_context->copy_fence_wait_event);
        WaitForSingleObject(d3d_context->copy_fence_wait_event, INFINITE);
    }
}

// FIXME: This only works if the texture is already in the copy_dst state. I need to handle GPU uploads better.
void blockingTextureUpload(D3DContext* d3d_context, ID3D12Resource* src_buffer, ID3D12Resource* dst_texture, u32 width, u32 height) {
    // NOTE: Wait for the previous upload to have completed.
    if (d3d_context->copy_fence->GetCompletedValue() < d3d_context->copy_fence_ready_value) {
        d3d_context->copy_fence->SetEventOnCompletion(d3d_context->copy_fence_ready_value, d3d_context->copy_fence_wait_event);
        WaitForSingleObject(d3d_context->copy_fence_wait_event, INFINITE);
    }

    // NOTE: Prepare the command buffer for recording.
    HRESULT alloc_reset = d3d_context->copy_allocator->Reset();
    ASSERT(SUCCEEDED(alloc_reset));
    HRESULT cmd_reset = d3d_context->copy_command_list->Reset(d3d_context->copy_allocator, NULL);
    ASSERT(SUCCEEDED(cmd_reset));

    // NOTE: Source and dest texture info.
    D3D12_TEXTURE_COPY_LOCATION dst_location = {};
    dst_location.pResource = dst_texture;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src_location = {};
    src_location.pResource = src_buffer;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src_location.PlacedFootprint.Footprint.Width = width;
    src_location.PlacedFootprint.Footprint.Height = height;
    src_location.PlacedFootprint.Footprint.Depth = 1;
    src_location.PlacedFootprint.Footprint.RowPitch = width * 4;

    // NOTE: This is the actual copy command.
    d3d_context->copy_command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, NULL);

    // NOTE: Close the command buffer and send it to the GPU for execution.
    HRESULT close_res = d3d_context->copy_command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    d3d_context->copy_command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&d3d_context->copy_command_list);

    // NOTE: Signal the copy fence once all commands on the copy queues have been executed.
    d3d_context->copy_command_queue->Signal(d3d_context->copy_fence, ++d3d_context->copy_fence_ready_value);

    // NOTE: Wait for the upload to have completed. This makes waiting at the beginning of the function technically
    // redundant, but eh.
    if (d3d_context->copy_fence->GetCompletedValue() < d3d_context->copy_fence_ready_value) {
        d3d_context->copy_fence->SetEventOnCompletion(d3d_context->copy_fence_ready_value, d3d_context->copy_fence_wait_event);
        WaitForSingleObject(d3d_context->copy_fence_wait_event, INFINITE);
    }
}

struct TextRenderer {
    D3DPipeline render_pipeline;
    ID3D12Resource* font_texture_buffer;
    ID3D12DescriptorHeap* font_texture_descriptor_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE font_texture_descriptor;
    b32 texture_ready;
};

void initializeTextRendering(D3DContext* d3d_context, TextRenderer* to_init, Arena* scratch) {
        // NOTE: Create the pipeline.
        ID3DBlob* text_vertex_shader;
        ID3DBlob* text_fragment_shader;
        readAndCompileShaders("./shaders/bitmap_text.hlsl", &text_vertex_shader, &text_fragment_shader);

        // NOTE: Root signature with 1 descriptor table containing the
        // bitmap font descriptor, a transform matrix and the ascii code.
        D3D12_DESCRIPTOR_RANGE range;
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER text_pipeline_root_parameters[3];
        text_pipeline_root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        text_pipeline_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        text_pipeline_root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        text_pipeline_root_parameters[0].DescriptorTable.pDescriptorRanges = &range;

        text_pipeline_root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        text_pipeline_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        text_pipeline_root_parameters[1].Constants.Num32BitValues = 16;
        text_pipeline_root_parameters[1].Constants.ShaderRegister = 0;
        text_pipeline_root_parameters[1].Constants.RegisterSpace = 0;

        text_pipeline_root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        text_pipeline_root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        text_pipeline_root_parameters[2].Constants.Num32BitValues = 1;
        text_pipeline_root_parameters[2].Constants.ShaderRegister = 1;
        text_pipeline_root_parameters[2].Constants.RegisterSpace = 0;

        D3D12_STATIC_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.MipLODBias = 0;
        sampler_desc.MaxAnisotropy = 1;
        sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        sampler_desc.MinLOD = 0;
        sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
        sampler_desc.ShaderRegister = 0;
        sampler_desc.RegisterSpace = 0;
        sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
        root_signature_desc.NumParameters = ARRAY_COUNT(text_pipeline_root_parameters);
        root_signature_desc.pParameters = text_pipeline_root_parameters;
        root_signature_desc.NumStaticSamplers = 1;
        root_signature_desc.pStaticSamplers = &sampler_desc;
        root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* serialized_signature;
        ID3DBlob* serialize_err;
        HRESULT serialize_res = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_signature, &serialize_err);
        if (serialize_err) {
            OutputDebugStringA((char*)serialize_err->GetBufferPointer());
        }
        ASSERT(SUCCEEDED(serialize_res));
        ASSERT(serialize_err == NULL);
        HRESULT signature_res = d3d_context->device->CreateRootSignature(0, serialized_signature->GetBufferPointer(), serialized_signature->GetBufferSize(), IID_PPV_ARGS(&to_init->render_pipeline.root_signature));
        ASSERT(SUCCEEDED(signature_res));

        D3D12_RASTERIZER_DESC rasterizer_desc = {};
        rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer_desc.CullMode = D3D12_CULL_MODE_BACK;
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
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
        blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_DEPTH_STENCIL_DESC depth_desc = {};
        depth_desc.DepthEnable = FALSE;
        depth_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        depth_desc.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
        pipeline_desc.InputLayout.pInputElementDescs = NULL;
        pipeline_desc.InputLayout.NumElements = 0;
        pipeline_desc.pRootSignature = to_init->render_pipeline.root_signature;
        pipeline_desc.VS.pShaderBytecode = text_vertex_shader->GetBufferPointer();
        pipeline_desc.VS.BytecodeLength = text_vertex_shader->GetBufferSize();
        pipeline_desc.PS.pShaderBytecode = text_fragment_shader->GetBufferPointer();
        pipeline_desc.PS.BytecodeLength = text_fragment_shader->GetBufferSize();
        pipeline_desc.RasterizerState = rasterizer_desc;
        pipeline_desc.BlendState = blend_desc;
        pipeline_desc.DepthStencilState = depth_desc;
        pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc.Count = 1;

        HRESULT pipeline_res = d3d_context->device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&to_init->render_pipeline.pipeline_state));
        ASSERT(SUCCEEDED(pipeline_res));

        text_vertex_shader->Release();
        text_fragment_shader->Release();
        serialized_signature->Release();

        // NOTE: Upload the texture and create the related objects.
        // - Texture buffer
        // - Upload buffer (released at the end of the function)
        // - Descriptor heap
        // - Texture descriptor inside
        u32 bitmap_width, bitmap_height;
        u8* bitmap_font = read_image(".\\assets\\monogram-bitmap.png", &bitmap_width, &bitmap_height, scratch, scratch);

        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask = 1;
        heap_props.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC texture_buffer_resource_desc = {};
        texture_buffer_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_buffer_resource_desc.Alignment = 0;
        texture_buffer_resource_desc.Width = bitmap_width;
        texture_buffer_resource_desc.Height = bitmap_height;
        texture_buffer_resource_desc.DepthOrArraySize = 1;
        texture_buffer_resource_desc.MipLevels = 1;
        texture_buffer_resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_buffer_resource_desc.SampleDesc.Count = 1;
        texture_buffer_resource_desc.SampleDesc.Quality = 0;
        texture_buffer_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texture_buffer_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT texture_buffer_creation_res = d3d_context->device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &texture_buffer_resource_desc,
            D3D12_RESOURCE_STATE_COMMON,
            NULL,
            IID_PPV_ARGS(&to_init->font_texture_buffer)
        );
        ASSERT(SUCCEEDED(texture_buffer_creation_res));

        D3D12_HEAP_PROPERTIES upload_heap_props = {};
        upload_heap_props.Type = D3D12_HEAP_TYPE_UPLOAD; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
        upload_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        upload_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        upload_heap_props.CreationNodeMask = 1;
        upload_heap_props.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC upload_heap_resource_desc = {};
        upload_heap_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_heap_resource_desc.Alignment = 0;
        upload_heap_resource_desc.Width = bitmap_width * bitmap_height * 4; // This is the only field that really matters.
        upload_heap_resource_desc.Height = 1;
        upload_heap_resource_desc.DepthOrArraySize = 1;
        upload_heap_resource_desc.MipLevels = 1;
        upload_heap_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        upload_heap_resource_desc.SampleDesc.Count = 1;
        upload_heap_resource_desc.SampleDesc.Quality = 0;
        upload_heap_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        upload_heap_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ID3D12Resource* upload_buffer;
        HRESULT upload_buffer_creation_res = d3d_context->device->CreateCommittedResource(
            &upload_heap_props,
            D3D12_HEAP_FLAG_NONE,
            &upload_heap_resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, // this is from the GPU's perspective
            NULL,
            IID_PPV_ARGS(&upload_buffer)
        );
        ASSERT(SUCCEEDED(upload_buffer_creation_res));

        D3D12_RANGE read_range = {};
        u8* mapped_upload_buffer;
        upload_buffer->Map(0, &read_range, (void**)&mapped_upload_buffer);

        for (int i = 0; i < bitmap_width * bitmap_height * 4; i++) {
            mapped_upload_buffer[i] = bitmap_font[i];
        }

        upload_buffer->Unmap(0, NULL);

        blockingTextureUpload(d3d_context, upload_buffer, to_init->font_texture_buffer, bitmap_width, bitmap_height);
        to_init->texture_ready = false;
        upload_buffer->Release();

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Important!

        HRESULT descriptor_heap_res = d3d_context->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&to_init->font_texture_descriptor_heap));
        ASSERT(SUCCEEDED(descriptor_heap_res));
        // u32 srv_descriptor_size = d3d_context->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = texture_buffer_resource_desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srv_handle = to_init->font_texture_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

        d3d_context->device->CreateShaderResourceView(to_init->font_texture_buffer, &srvDesc, srv_handle);
}

// TODO: Many duplicate vertices. Is it easy/possible to use indices here ?
// TODO: Currently the chunk doesn't look into neighboring chunks. This means there are generated
// triangles between solid blocks on two different chunks.
// TODO: Look into switching to greedy meshing.
void generateNaiveChunkMesh(Chunk* chunk, ChunkVertex* out_vertices, usize* out_generated_vertex_count) {
    usize emitted = 0;
    for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){

        usize x = (i % CHUNK_W);
        usize y = (i / CHUNK_W) % (CHUNK_W);
        usize z = (i / (CHUNK_W * CHUNK_W));

        if (!chunk->data[i]) continue;

        b32 empty_pos_x = true;
        b32 empty_neg_x = true;
        b32 empty_pos_y = true;
        b32 empty_neg_y = true;
        b32 empty_pos_z = true;
        b32 empty_neg_z = true;

        if (x < (CHUNK_W - 1)) {
            empty_pos_x = !chunk->data[i + 1];
        }
        if (x > 0) {
            empty_neg_x = !chunk->data[i - 1];
        }

        if (y < (CHUNK_W - 1)) {
            empty_pos_y = !chunk->data[i + CHUNK_W];
        }
        if (y > 0) {
            empty_neg_y = !chunk->data[i - CHUNK_W];
        }

        if (z < (CHUNK_W - 1)) {
            empty_pos_z = !chunk->data[i + CHUNK_W * CHUNK_W];
        }
        if (z > 0) {
            empty_neg_z = !chunk->data[i - CHUNK_W * CHUNK_W];
        }

        v3 position = {(f32)x, (f32)y, (f32)z};

        if (empty_pos_x) {
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};
        }

        if (empty_neg_x) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {-1, 0, 0}};
        }

        if (empty_pos_y) {
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};
        }

        if (empty_neg_y) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

        }

        if (empty_pos_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 0, 1}};
        }

        if (empty_neg_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, 0, -1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 0, -1}};
        }
    }

    *out_generated_vertex_count = emitted;
}

b32 raycast_aabb(v3 ray_origin, v3 ray_direction, v3 bb_min, v3 bb_max, f32* out_t) {
    // NOTE: Original algorithm from here:
    // https://tavianator.com/2011/ray_box.html
    f32 tmin = -INFINITY;
    f32 tmax = INFINITY;

    // TODO: The original blog post suggests using SSE2's min and max instructions
    // as an easy optimization.

    if (ray_direction.x != 0.0f) {
        f32 tx1 = (bb_min.x - ray_origin.x)/ray_direction.x;
        f32 tx2 = (bb_max.x - ray_origin.x)/ray_direction.x;

        tmin = max(tmin, min(tx1, tx2));
        tmax = min(tmax, max(tx1, tx2));
    }

    if (ray_direction.y != 0.0f) {
        f32 ty1 = (bb_min.y - ray_origin.y)/ray_direction.y;
        f32 ty2 = (bb_max.y - ray_origin.y)/ray_direction.y;

        tmin = max(tmin, min(ty1, ty2));
        tmax = min(tmax, max(ty1, ty2));
    }

    if (ray_direction.z != 0.0f) {
        f32 tz1 = (bb_min.z - ray_origin.z)/ray_direction.z;
        f32 tz2 = (bb_max.z - ray_origin.z)/ray_direction.z;

        tmin = max(tmin, min(tz1, tz2));
        tmax = min(tmax, max(tz1, tz2));
    }

    if (tmax < tmin || tmin <= 0.0f) {
        return false;
    }

    *out_t = tmin;
    return true;
}

inline b32 point_inclusion_aabb(v3 point, v3 bb_min, v3 bb_max) {
    return point.x >= bb_min.x && point.x <= bb_max.x
        && point.y >= bb_min.y && point.y <= bb_max.y
        && point.z >= bb_min.z && point.z <= bb_max.z;
}

// NOTE: Classic Amanatides & Woo algorithm.
// http://www.cse.yorku.ca/~amana/research/grid.pdf
// The traversal origin needs to be on the boundary or inside the chunk already,
// and in chunk-relative coordinates. That first part should not be a problem in
// the final game since the whole world will be filled with chunks, but for now
// we raycast with the single debug chunk before calling this function.
b32 raycast_chunk_traversal(Chunk* chunk, v3 traversal_origin, v3 traversal_direction, usize* out_i) {

    // FIXME: This assertion to check if the origin is in chunk-relative coordinates sometimes fires
    // because one of the components is -0.000001~. This could just be expected precision issues with
    // the chunk bounding box raycast function.
    // ASSERT(point_inclusion_aabb(traversal_origin, v3 {0, 0, 0}, v3 {CHUNK_W, CHUNK_W, CHUNK_W}));

    i32 x, y, z;
    x = (i32)(traversal_origin.x);
    y = (i32)(traversal_origin.y);
    z = (i32)(traversal_origin.z);

    // NOTE: If the ray origin is on a positive boundary, the truncation
    // will create x/y/z = 16 ("first block of neighboring chunk") instead
    // of x/y/z = 15 ("last block of this chunk") so we need to correct this.
    if (traversal_origin.x == (f32)CHUNK_W) x--;
    if (traversal_origin.y == (f32)CHUNK_W) y--;
    if (traversal_origin.z == (f32)CHUNK_W) z--;

    i32 step_x = traversal_direction.x > 0.0 ? 1 : -1;
    i32 step_y = traversal_direction.y > 0.0 ? 1 : -1;
    i32 step_z = traversal_direction.z > 0.0 ? 1 : -1;

    f32 t_max_x = ((step_x > 0.0) ? ((f32)x + 1 - traversal_origin.x) : (traversal_origin.x - (f32)x)) / fabsf(traversal_direction.x);
    f32 t_max_y = ((step_y > 0.0) ? ((f32)y + 1 - traversal_origin.y) : (traversal_origin.y - (f32)y)) / fabsf(traversal_direction.y);
    f32 t_max_z = ((step_z > 0.0) ? ((f32)z + 1 - traversal_origin.z) : (traversal_origin.z - (f32)z)) / fabsf(traversal_direction.z);

    f32 t_delta_x = 1.0 / fabs(traversal_direction.x);
    f32 t_delta_y = 1.0 / fabs(traversal_direction.y);
    f32 t_delta_z = 1.0 / fabs(traversal_direction.z);

    while (x >= 0 && x < CHUNK_W && y >= 0 && y < CHUNK_W && z >= 0 && z < CHUNK_W) {
        if (chunk->data[x + CHUNK_W * y + CHUNK_W * CHUNK_W * z]) {
            *out_i = x + CHUNK_W * y + CHUNK_W * CHUNK_W * z;
            return true;
        }

        if (t_max_x < t_max_y) {
            if (t_max_x < t_max_z) {
                x += step_x;
                t_max_x += t_delta_x;
            } else {
                z += step_z;
                t_max_z += t_delta_z;
            }
        } else {
            if (t_max_y < t_max_z) {
                y += step_y;
                t_max_y += t_delta_y;
            } else {
                z += step_z;
                t_max_z += t_delta_z;
            }
        }
    }

    return false;
}

void refreshChunk(D3DContext* d3d_context, Chunk* chunk) {

    // NOTE: generate directly in the mapped upload buffer
    ChunkVertex* buffer;
    D3D12_RANGE read_range = {};
    // NOTE: Passing NULL as the read range would mean we are gonna read the entire
    // buffer, but for optimization and simplicity's sake let's say that the user
    // should not reading from a mapped upload buffer.
    chunk->upload_buffer->Map(0, &read_range, (void**)&buffer);

    generateNaiveChunkMesh(chunk, buffer, &chunk->vertices_count);

    // NOTE: Passing NULL as the write range signals to the driver that we
    // we wrote the entire buffer.
    chunk->upload_buffer->Unmap(0, NULL);

    // TODO: add a way to check that the number of generated vertices is not bigger than the upload and vertex buffers
    blockingUploadToGPUBuffer(d3d_context, chunk->upload_buffer, chunk->vertex_buffer, chunk->vertices_count * sizeof(ChunkVertex));

    // NOTE: After the upload, the buffer is in COPY_DEST state, so it needs
    // to be transitioned back into a vertex buffer.
    chunk->vbo_ready = false;
}

void waitForGPU(D3DContext* d3d_context) {
    D3DFrameContext* current_frame = &d3d_context->frames[d3d_context->current_frame_idx];

    d3d_context->graphics_command_queue->Signal(current_frame->fence, ++current_frame->fence_ready_value);
    if(current_frame->fence->GetCompletedValue() < current_frame->fence_ready_value) {
        current_frame->fence->SetEventOnCompletion(current_frame->fence_ready_value, current_frame->fence_wait_event);
        WaitForSingleObject(current_frame->fence_wait_event, INFINITE);
    }

    d3d_context->copy_command_queue->Signal(d3d_context->copy_fence, ++d3d_context->copy_fence_ready_value);
    if (d3d_context->copy_fence->GetCompletedValue() < d3d_context->copy_fence_ready_value) {
        d3d_context->copy_fence->SetEventOnCompletion(d3d_context->copy_fence_ready_value, d3d_context->copy_fence_wait_event);
        WaitForSingleObject(d3d_context->copy_fence_wait_event, INFINITE);
    }
}

// TODO: Switch away from null-terminated strings.
// NOTE: The debug text is drawn on a terminal-like grid, using a monospace font.
void drawDebugTextOnScreen(TextRenderer* text_renderer, ID3D12GraphicsCommandList* command_list, const char* text, u32 start_row, u32 start_col) {
    // NOTE: Setup pipeline and texture binding.
    if (!text_renderer->texture_ready) {
        D3D12_RESOURCE_BARRIER to_shader_res_barrier = {};
        to_shader_res_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_shader_res_barrier.Transition.pResource = text_renderer->font_texture_buffer;
        to_shader_res_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        to_shader_res_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        to_shader_res_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &to_shader_res_barrier);

        text_renderer->texture_ready = true;
    }
    command_list->SetPipelineState(text_renderer->render_pipeline.pipeline_state);
    command_list->SetGraphicsRootSignature(text_renderer->render_pipeline.root_signature);
    command_list->SetDescriptorHeaps(1, &text_renderer->font_texture_descriptor_heap);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_texture_handle = text_renderer->font_texture_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    command_list->SetGraphicsRootDescriptorTable(0, gpu_texture_handle);

    // NOTE: https://datagoblin.itch.io/monogram
    // The bitmap font is 96x96 and has 16x8 chars, so the individual
    // characters are 6x12.
    // The vertical layout is :
    // - 2px ascender on some chars
    // - 5px for all chars
    // - 2px descender on some chars
    // - 3px padding on the bottom
    // And the horizontal layout is :
    // - 1px padding on the left
    // - 5px for all chars
    // So when laying out chars on a grid, there is already a horizontal
    // space between them because of the 1px left padding, and a big line
    // space of 3 pixels. Most chars are also vertically centered, due to the
    // ascender/descender pair.
    constexpr f32 char_ratio = 6.f / 12.f;
    constexpr f32 char_scale = 0.04f; // TODO: Make this configurable ?
    constexpr f32 char_width = (char_ratio * char_scale) * 2;
    constexpr f32 char_height = (char_scale) * 2;

    // NOTE: The shader produce a quad that covers the whole screen.
    // We need to make it a quad of the right proportions and
    // located in the first slot.
    m4 quad_setup_matrix =
        makeTranslation((f32)start_col * char_width, (f32)start_row * -char_height, 0)
        * makeTranslation(-(1 - char_width), 1 - char_height / 2, 0)
        * makeScale(char_width/2, char_height/2, 0);

    i32 row = start_row;
    i32 col = start_col;
    for (int char_i = 0; text[char_i] != 0; char_i++) {
        u32 char_ascii_codepoint = (u32)text[char_i];
        if (char_ascii_codepoint == '\n') {
            row = start_row;
            col++;
            continue;
        }

        m4 char_translate = makeTranslation((f32)row * char_width, (f32)col * -char_height , 0);
        m4 char_matrix = char_translate * quad_setup_matrix;

        // TODO: Implement text wrapping here.
        row++;

        command_list->SetGraphicsRoot32BitConstants(1, 16, char_matrix.data, 0);
        command_list->SetGraphicsRoot32BitConstants(2, 1, &char_ascii_codepoint, 0);
        command_list->DrawInstanced(6, 1, 0, 0);
    }
}

struct GameState {
    f32 time;
    RandomSeries random_series;
    SimplexTable* simplex_table;

    D3DContext d3d_context;

    f32 camera_pitch;
    f32 camera_yaw;
    v3 player_position;
    v3 camera_forward;
    b32 orbit_mode;

    ChunkMemoryPool chunk_pool;
    WorldHashMap world;

    D3DPipeline chunk_render_pipeline;
    D3DPipeline wireframe_render_pipeline;

    TextRenderer text_renderer;

    b32 is_wireframe;

    u8 static_arena_memory[MEGABYTES(2)];
    Arena static_arena;

    u8 frame_arena_memory[MEGABYTES(2)];
    Arena frame_arena;
};

extern "C"
void gameUpdate(f32 dt, GameMemory* memory, InputState* input) {
    ASSERT(memory->permanent_storage_size >= sizeof(GameState));
    GameState* game_state = (GameState*)memory->permanent_storage;

    // INITIALIZATION
    if(!memory->is_initialized) {
        game_state->static_arena.base = game_state->static_arena_memory;
        game_state->static_arena.capacity = ARRAY_COUNT(game_state->static_arena_memory);

        game_state->frame_arena.base = game_state->frame_arena_memory;
        game_state->frame_arena.capacity = ARRAY_COUNT(game_state->frame_arena_memory);

        game_state->d3d_context = initializeD3D12(true);

        initChunkMemoryPool(&game_state->chunk_pool, game_state->d3d_context.device);
        game_state->world.nb_empty = HASHMAP_SIZE;
        game_state->world.nb_occupied = 0;
        game_state->world.nb_reusable = 0;

        game_state->player_position = {110, 40, 110};
        game_state->orbit_mode = false;
        game_state->time = 0;
        game_state->camera_pitch = -1 * PI32 / 6;
        game_state->camera_yaw = 1 * PI32 / 3;
        game_state->random_series = 0xC0FFEE; // fixed seed for now
        game_state->simplex_table = simplex_table_from_seed(0xC0FFEE, &game_state->static_arena);

        createChunkRenderPipelines(&game_state->d3d_context, &game_state->chunk_render_pipeline, &game_state->wireframe_render_pipeline);

        initializeTextRendering(&game_state->d3d_context, &game_state->text_renderer, &game_state->frame_arena);

        memory->is_initialized = true;
    }

    // SIMULATION
    game_state->time += dt;

    f32 mouse_sensitivity = 0.01;
    f32 stick_sensitivity = 0.05;

    game_state->camera_pitch += input->kb.mouse_delta.y * mouse_sensitivity;
    game_state->camera_yaw += input->kb.mouse_delta.x * mouse_sensitivity;

    game_state->camera_pitch += input->ctrl.right_stick.y * stick_sensitivity;
    game_state->camera_yaw += input->ctrl.right_stick.x * stick_sensitivity;

    f32 safe_pitch = PI32 / 2.f - 0.05f;
    game_state->camera_pitch = clamp(game_state->camera_pitch, -safe_pitch, safe_pitch);

    game_state->camera_forward.x = cosf(game_state->camera_yaw) * cosf(game_state->camera_pitch);
    game_state->camera_forward.y = sinf(game_state->camera_pitch);
    game_state->camera_forward.z = sinf(game_state->camera_yaw) * cosf(game_state->camera_pitch);

    v3 camera_right = normalize(cross(game_state->camera_forward, {0, 1, 0}));

    f32 speed = 20 * dt;
    if (input->kb.keys[SCANCODE_LSHIFT].is_down || input->ctrl.x.is_down) {
        speed *= 5;
    }

    // TODO: Nothing is normalized, so the player moves faster in diagonal directions.
    // Let's just say it's a Quake reference.
    // Also you can move twice as fast by using a keyboard and a controller at the same time.
    // That's for speedrunners.

    if(input->kb.keys[SCANCODE_Q].is_down || input->ctrl.lb.is_down) {
        game_state->player_position.y -= speed;
    }
    if(input->kb.keys[SCANCODE_E].is_down || input->ctrl.rb.is_down) {
        game_state->player_position.y += speed;
    }
    if(input->kb.keys[SCANCODE_A].is_down) {
        game_state->player_position += -camera_right * speed;
    }
    if(input->kb.keys[SCANCODE_D].is_down) {
        game_state->player_position += camera_right * speed;
    }
    if(input->kb.keys[SCANCODE_S].is_down) {
        game_state->player_position += -game_state->camera_forward * speed;
    }
    if(input->kb.keys[SCANCODE_W].is_down) {
        game_state->player_position += game_state->camera_forward * speed;
    }

    game_state->player_position += input->ctrl.left_stick.x * camera_right * speed;
    game_state->player_position += input->ctrl.left_stick.y * game_state->camera_forward * speed;

    if (input->kb.keys[SCANCODE_G].is_down && input->kb.keys[SCANCODE_G].transitions == 1) {
        game_state->is_wireframe = !game_state->is_wireframe;
    }

    if (input->kb.keys[SCANCODE_O].is_down && input->kb.keys[SCANCODE_O].transitions == 1) {
        game_state->orbit_mode = !game_state->orbit_mode;
    }

    // FIXME: The code for destroying the block the player is looking at is pretty convoluted.
    // There has to be a better way, maybe recursive ? But that has drawbacks too.
    if (input->kb.keys[SCANCODE_SPACE].is_down || input->ctrl.a.is_down) {

        usize chunk_to_traverse = 0;
        bool found_chunk = false;
        v3 traversal_origin;

        // NOTE: If the player is inside a chunk already, we don't have to do a raycast the first time.
        // This would be the default case once the world is entirely filled with chunks, but not for now.
        for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {
            Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
            if (!chunk) continue;

            v3 chunk_world_pos = chunkToWorldPos(chunk->chunk_position);
            v3 chunk_bb_min = {(f32)(chunk_world_pos.x), (f32)(chunk_world_pos.y), (f32)(chunk_world_pos.z)};
            v3 chunk_bb_max = {(f32)(chunk_world_pos.x+CHUNK_W), (f32)(chunk_world_pos.y+CHUNK_W), (f32)(chunk_world_pos.z+CHUNK_W)};

            if (point_inclusion_aabb(game_state->player_position, chunk_bb_min, chunk_bb_max)) {
                chunk_to_traverse = chunk_idx;
                traversal_origin = game_state->player_position;
                found_chunk = true;
                break;
            }
        }

        f32 minimum_t = 0.0;
        // TODO: We should have a termination condition here just in case. Maybe based on minimum_t ?
        while (true) {

            // NOTE: If a chunk to traverse was not set during init, meaning the player is not inside a chunk,
            // we need to look for one. We increment the minimum_t variable every time we intersect with a chunk
            // so that each iteration traverses the next chunk along the line of sight, until a block is found.
            f32 closest_t_during_search = INFINITY;
            if (!found_chunk) {
                for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {
                    Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
                    if (!chunk) continue;

                    v3 chunk_world_pos = chunkToWorldPos(chunk->chunk_position);
                    v3 chunk_bb_min = {(f32)(chunk_world_pos.x), (f32)(chunk_world_pos.y), (f32)(chunk_world_pos.z)};
                    v3 chunk_bb_max = {(f32)(chunk_world_pos.x+CHUNK_W), (f32)(chunk_world_pos.y+CHUNK_W), (f32)(chunk_world_pos.z+CHUNK_W)};

                    f32 hit_t;
                    b32 did_hit = raycast_aabb(game_state->player_position, game_state->camera_forward, chunk_bb_min, chunk_bb_max, &hit_t);

                    if (did_hit && hit_t < closest_t_during_search && hit_t > minimum_t) {
                        closest_t_during_search = hit_t;
                        chunk_to_traverse = chunk_idx;
                        found_chunk = true;
                    }
                }

                if (found_chunk) {
                    // NOTE: Update the minimum_t so that if no block is found in this chunk, we can look into the
                    // next furthest chunk next iteration.
                    traversal_origin = game_state->player_position + game_state->camera_forward * closest_t_during_search;
                    minimum_t = closest_t_during_search;
                }
            }

            if (found_chunk) {
                Chunk* chunk = getChunkByIndex(&game_state->world, chunk_to_traverse);

                v3 chunk_to_traverse_origin = chunkToWorldPos(chunk->chunk_position);
                v3 intersection_relative = traversal_origin - chunk_to_traverse_origin;

                usize block_idx;
                if (raycast_chunk_traversal(chunk, intersection_relative, game_state->camera_forward, &block_idx)) {
                    chunk->data[block_idx] = 0;
                    // FIXME: This is a quick temporary fix. Without that, we could modify the vertex buffer while it is used to render the previous
                    // frame. I would need profiling to know if it's THAT bad. A possible solution would be to decouple mesh generation and upload,
                    // i.e. generate the mesh but only upload it after the previous frame is rendered. Perhaps in a separate thread.
                    waitForGPU(&game_state->d3d_context);
                    refreshChunk(&game_state->d3d_context, chunk);
                    break;
                }

                // NOTE: This is confusing but I can't think of a better place to put it.
                // When we arrive here, it means the raycast found a chunk but that it didn't have any solid block
                // along the line. So we need to find a new one during the next iteration.
                found_chunk = false;
            } else {
                break;
            }
        }
    }

    // NOTE: Unload chunks too far from the player
    for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {
        Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
        if (!chunk) continue;

        v3 chunk_to_unload_world_pos = chunkToWorldPos(chunk->chunk_position);
        v3 chunk_to_unload_center_pos = chunk_to_unload_world_pos + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

        v3 player_chunk_center_pos = chunkToWorldPos(worldPosToChunk(game_state->player_position)) + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

        f32 dist = length(player_chunk_center_pos - chunk_to_unload_center_pos);
        if (dist > (f32)LOAD_RADIUS * CHUNK_W) {
            // NOTE: This chunk is too far away and needs to be unloaded.     
            
            worldDelete(&game_state->world, chunk->chunk_position);
            releaseChunkMemoryToPool(&game_state->chunk_pool, chunk);
        }
    }

    // NOTE: Load new chunks as needed
    v3i player_chunk_pos = worldPosToChunk(game_state->player_position);
    for (i32 x = player_chunk_pos.x - LOAD_RADIUS; x <= player_chunk_pos.x + LOAD_RADIUS; x++) {
        for (i32 y = player_chunk_pos.y - LOAD_RADIUS; y <= player_chunk_pos.y + LOAD_RADIUS; y++) {
            for (i32 z = player_chunk_pos.z - LOAD_RADIUS; z <= player_chunk_pos.z + LOAD_RADIUS; z++) {

                v3i chunk_to_load_pos = v3i {x, y, z};
                v3 chunk_to_load_world_pos = chunkToWorldPos(chunk_to_load_pos);
                v3 chunk_to_load_center_pos = chunk_to_load_world_pos + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

                v3 player_chunk_center_pos = chunkToWorldPos(worldPosToChunk(game_state->player_position)) + v3 {(f32)CHUNK_W / 2, (f32)CHUNK_W / 2, (f32)CHUNK_W / 2};

                if (length(player_chunk_center_pos - chunk_to_load_center_pos) > (f32)LOAD_RADIUS * CHUNK_W) continue; 

                if (!isChunkInWorld(&game_state->world, chunk_to_load_pos)) {
                    Chunk* chunk = acquireChunkMemoryFromPool(&game_state->chunk_pool);
                    chunk->chunk_position = chunk_to_load_pos;

                    for(usize block_idx = 0; block_idx < CHUNK_W * CHUNK_W * CHUNK_W; block_idx++){
                        u32 block_x = chunk_to_load_pos.x * CHUNK_W + (block_idx % CHUNK_W);
                        u32 block_y = chunk_to_load_pos.y * CHUNK_W + (block_idx / CHUNK_W) % (CHUNK_W);
                        u32 block_z = chunk_to_load_pos.z * CHUNK_W + (block_idx / (CHUNK_W * CHUNK_W));

                        // TODO: This needs to be parameterized and put into a function.
                        // The fancy name is "fractal brownian motion", but it's just summing
                        // noise layers with reducing intensity and increasing frequency.
                        f32 space_scaling_factor = 0.01;
                        f32 height_intensity = 32.0;
                        f32 height = 0;
                        for (i32 octave = 0; octave < 3; octave ++) {
                            height += ((simplex_noise_2d(game_state->simplex_table, (f32)block_x * space_scaling_factor, (f32) block_z * space_scaling_factor) + 1.f) / 2.f) * height_intensity;
                            space_scaling_factor *= 2.f;
                            height_intensity /= 2.f;
                        }

                        if (block_y <= height) {
                            chunk->data[block_idx] = 1;
                        } else {
                            chunk->data[block_idx] = 0;
                        }
                    }

                    refreshChunk(&game_state->d3d_context, chunk);
                    worldInsert(&game_state->world, chunk_to_load_pos, chunk);
                }
            }
        }
    }

    // RENDERING

    // NOTE: Wait for the render commands sent the last time we used that backbuffer have
    // been completed, by sleeping until its value gets updated by the signal command we enqueued last time.
    D3DFrameContext* current_frame = &game_state->d3d_context.frames[game_state->d3d_context.current_frame_idx];
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

    // NOTE: Clear the backbuffer.
    f32 clear_color[] = {0.1, 0.1, 0.2, 1};
    current_frame->command_list->ClearRenderTargetView(current_frame->render_target_view_descriptor, clear_color, 0, NULL);
    current_frame->command_list->ClearDepthStencilView(current_frame->depth_target_view_descriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

    // NOTE: Set the shared graphics pipeline stuff.
    RECT clientRect;
    GetClientRect(game_state->d3d_context.window, &clientRect);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = clientRect.right;
    viewport.Height = clientRect.bottom;
    viewport.MinDepth = 0.0;
    viewport.MaxDepth = 1.0;

    D3D12_RECT scissor = {};
    scissor.top = viewport.TopLeftY;
    scissor.bottom = viewport.Height;
    scissor.left = viewport.TopLeftX;
    scissor.right = viewport.Width;

    current_frame->command_list->RSSetViewports(1, &viewport);
    current_frame->command_list->RSSetScissorRects(1, &scissor);

    current_frame->command_list->OMSetRenderTargets(1, &current_frame->render_target_view_descriptor, FALSE, &current_frame->depth_target_view_descriptor);
    current_frame->command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // NOTE: Draw the chunks.
    if (game_state->is_wireframe) {
        current_frame->command_list->SetPipelineState(game_state->wireframe_render_pipeline.pipeline_state);
        current_frame->command_list->SetGraphicsRootSignature(game_state->wireframe_render_pipeline.root_signature);

        v4 wireframe_color = {1, 1, 1, 1};
        current_frame->command_list->SetGraphicsRoot32BitConstants(2, 4, wireframe_color.data, 0);
    } else {
        current_frame->command_list->SetPipelineState(game_state->chunk_render_pipeline.pipeline_state);
        current_frame->command_list->SetGraphicsRootSignature(game_state->chunk_render_pipeline.root_signature);
    }

    m4 fused_perspective_view = makeProjection(0.1, 1000, 90) * lookAt(game_state->player_position, game_state->player_position + game_state->camera_forward);
    current_frame->command_list->SetGraphicsRoot32BitConstants(1, 16, fused_perspective_view.data, 0);

    for (usize chunk_idx = 0; chunk_idx < HASHMAP_SIZE; chunk_idx++) {

        Chunk* chunk = getChunkByIndex(&game_state->world, chunk_idx);
        if (!chunk) continue;

        m4 chunk_model = makeTranslation(chunkToWorldPos(chunk->chunk_position));
        current_frame->command_list->SetGraphicsRoot32BitConstants(0, 16, chunk_model.data, 0);

        // NOTE: If the chunk has just been updated, it needs to be transitioned
        // back into a vertex buffer, from a copy destination buffer.
        if (!chunk->vbo_ready) {
            D3D12_RESOURCE_BARRIER to_vertex_buffer_barrier = {};
            to_vertex_buffer_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_vertex_buffer_barrier.Transition.pResource = chunk->vertex_buffer;
            to_vertex_buffer_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            to_vertex_buffer_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            to_vertex_buffer_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            current_frame->command_list->ResourceBarrier(1, &to_vertex_buffer_barrier);

            chunk->vbo_ready = true;
        }

        D3D12_VERTEX_BUFFER_VIEW vb_view;
        vb_view.BufferLocation = chunk->vertex_buffer->GetGPUVirtualAddress();
        vb_view.StrideInBytes = sizeof(ChunkVertex);
        vb_view.SizeInBytes = chunk->vertices_count * sizeof(ChunkVertex);

        current_frame->command_list->IASetVertexBuffers(0, 1, &vb_view);

        current_frame->command_list->DrawInstanced(chunk->vertices_count, 1, 0, 0);
    }

    // NOTE: Text rendering test.
    char debug_text_string[256];
    v3i chunk_position = worldPosToChunk(game_state->player_position);
    StringCbPrintf(debug_text_string, ARRAY_COUNT(debug_text_string),
                   "Pos: %.2f, %.2f, %.2f\nChunk: %d, %d, %d\n\nEmpty: %d %s\nOccupied: %d\nReusable: %d\n\nPool: %d/%d",
                   game_state->player_position.x,
                   game_state->player_position.y,
                   game_state->player_position.z,
                   chunk_position.x,
                   chunk_position.y,
                   chunk_position.z,
                   game_state->world.nb_empty,
                   game_state->world.nb_empty < 25 ? "!!!" : "",
                   game_state->world.nb_occupied,
                   game_state->world.nb_reusable,
                   CHUNK_POOL_SIZE - (game_state->chunk_pool.free_slots_stack_ptr - game_state->chunk_pool.free_slots),
                   CHUNK_POOL_SIZE
    );
    drawDebugTextOnScreen(&game_state->text_renderer, current_frame->command_list, debug_text_string, 0, 0);

    // NOTE: The backbuffer should be transitionned to be used for presentation.
    D3D12_RESOURCE_BARRIER present_barrier = {};
    present_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    present_barrier.Transition.pResource = current_frame->render_target_resource;
    present_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    present_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    present_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    current_frame->command_list->ResourceBarrier(1, &present_barrier);

    // NOTE: Close the command list and send it to the GPU for execution.
    HRESULT close_res = current_frame->command_list->Close();
    ASSERT(SUCCEEDED(close_res));
    game_state->d3d_context.graphics_command_queue->ExecuteCommandLists(1, (ID3D12CommandList**)&current_frame->command_list);

    // TODO: Sort the whole vsync situation out.
    game_state->d3d_context.swapchain->Present(1, 0);

    // NOTE: Signal this frame's fence when the graphics commands enqueued until now have completed.
    game_state->d3d_context.graphics_command_queue->Signal(current_frame->fence, ++current_frame->fence_ready_value);

    // NOTE: Get the next frame's index
    // TODO: Confirm that this is updated by the call to Present.
    game_state->d3d_context.current_frame_idx = game_state->d3d_context.swapchain->GetCurrentBackBufferIndex();

    clearArena(&game_state->frame_arena);
}
