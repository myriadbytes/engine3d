#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <strsafe.h>
#include "Microsoft/include/GameInput.h"

using namespace GameInput::v3;

#include "common.h"
#include "input.h"

/*
 * TO-DO LIST:
 * - Swapchain resizing
 * - Keyboard & controller input processing (either using raw WM_KEYDOWN & XInput or the "new" Windows GameInput)
 * - Sound output (WASAPI ?)
 * - Code hot-reloading
 * - Save states (like an emulator)
 * - Looped input recording & playback
 * - Fully featured matrix math library
 * - High level retro 3D artstyle renderer API
 * - Logging
 * - UI & Text Rendering
 */

b32 global_running = false;

struct D3D12FrameContext {
    ID3D12CommandAllocator* command_allocator;
    ID3D12GraphicsCommandList* command_list;
    ID3D12Fence* fence;
    HANDLE fence_wait_event;
    u64 fence_ready_value;
};

constexpr u32 FRAMES_IN_FLIGHT = 2;
struct D3D12Context {
    ID3D12Debug* debug_interface;
    ID3D12Device* device;
    ID3D12CommandQueue* command_queue;
    IDXGISwapChain3* swapchain;
    ID3D12DescriptorHeap* rtv_heap;
    u32 rtv_descriptor_size;
    ID3D12Resource* render_target_resources[FRAMES_IN_FLIGHT];
    D3D12FrameContext frames[FRAMES_IN_FLIGHT];
    u32 current_frame_idx;
};

D3D12Context initD3D12(HWND window_handle) {
    D3D12Context context = {};

    // FIXME: release all the COM objects
    HRESULT debug_res = D3D12GetDebugInterface(IID_PPV_ARGS(&context.debug_interface));
    ASSERT(SUCCEEDED(debug_res));
    context.debug_interface->EnableDebugLayer();

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

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT queue_res = context.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&context.command_queue));
    ASSERT(SUCCEEDED(queue_res));

    IDXGISwapChain1* legacy_swapchain;
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = FRAMES_IN_FLIGHT;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT swapchain_res = factory->CreateSwapChainForHwnd(
        context.command_queue,
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
    D3D12_CPU_DESCRIPTOR_HANDLE heap_ptr = context.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (u32 frame_idx = 0; frame_idx < FRAMES_IN_FLIGHT; frame_idx++) {
        // NOTE: we first get a ID3D12Resource (handle for the actual GPU memory of the swapchain)
        // and then create a render target view from that
        HRESULT swapchain_buffer_res = context.swapchain->GetBuffer(frame_idx, IID_PPV_ARGS(&context.render_target_resources[frame_idx]));
        ASSERT(SUCCEEDED(swapchain_buffer_res));
        context.device->CreateRenderTargetView(context.render_target_resources[frame_idx], NULL, heap_ptr);
        heap_ptr.ptr += context.rtv_descriptor_size;
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

void renderFrame(D3D12Context* d3d_context, D3D12FrameContext* current_frame, f32* clear_color) {
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

    // NOTE: get the descriptor for backbuffer clearing
    // TODO: maybe put that in the frame context directly ?
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view;
    render_target_view.ptr = d3d_context->rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + d3d_context->rtv_descriptor_size * d3d_context->current_frame_idx;

    // NOTE: clear and render
    current_frame->command_list->ClearRenderTargetView(render_target_view, clear_color, 0, NULL);

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

void printReading(IGameInputDevice* device, IGameInputReading* reading) {
    // TODO: handle other readings than keyboard
    GameInputKeyState states[16];
    u32 count = reading->GetKeyCount();
    ASSERT(count < ARRAY_COUNT(states));
    reading->GetKeyState(count, states);

    //const GameInputDeviceInfo* info;
    //device->GetDeviceInfo(&info);
    //char dev_print_buffer[256];
    //StringCbPrintfA(dev_print_buffer, 256, "Device: %s\n", info->displayName);
    //OutputDebugStringA(dev_print_buffer);

    for (u32 i = 0; i < count; i++) {
        //char key_print_buffer[256];
        //StringCbPrintfA(key_print_buffer, 256, "Virtual Key: %c\n", states[i].virtualKey);
        //OutputDebugStringA(key_print_buffer);
    }
}

void pollGameInput(HWND window, IGameInput* game_input, InputState* previous_input_state, InputState* current_input_state) {

    *current_input_state = {};

    IGameInputReading* kb_reading;
    HRESULT kb_reading_res = game_input->GetCurrentReading(GameInputKindKeyboard, nullptr, &kb_reading);

    if (SUCCEEDED(kb_reading_res)) {
        current_input_state->is_analog = false;

        kb_reading->Release();
    }

    IGameInputReading* mouse_reading;
    HRESULT mouse_reading_res = game_input->GetCurrentReading(GameInputKindMouse, nullptr, &mouse_reading);

    if (SUCCEEDED(mouse_reading_res)) {
        current_input_state->is_analog = false;

        GameInputMouseState mouse_state;
        mouse_reading->GetMouseState(&mouse_state);

        // NOTE: normalized mouse coordinates relative to the window
        // (0, 0) -> top left
        // (1, 1) -> bottom right
        // TODO: should this be clamped ?
        RECT window_rect;
        GetWindowRect(window, &window_rect);
        f32 rel_mouse_x = (f32)(mouse_state.absolutePositionX - window_rect.left) / (f32) (window_rect.right - window_rect.left);
        f32 rel_mouse_y = (f32)(mouse_state.absolutePositionY - window_rect.top) / (f32) (window_rect.bottom - window_rect.top);

        //char print_buffer[256];
        //StringCbPrintfA(print_buffer, 256, "MouseX: %.02f, MouseY: %.02f\n",
        //    rel_mouse_x, rel_mouse_y);
        //OutputDebugStringA(print_buffer);

        current_input_state->kb.MouseScreenPos = Vec2 {rel_mouse_x, rel_mouse_y};

        mouse_reading->Release();
    }

    IGameInputReading* pad_reading;
    HRESULT pad_reading_res = game_input->GetCurrentReading(GameInputKindGamepad, nullptr, &pad_reading);

    if (SUCCEEDED(pad_reading_res)) {
        current_input_state->is_analog = true;

        GameInputGamepadState pad_state;
        pad_reading->GetGamepadState(&pad_state);

        // TODO: deadzone handling
        current_input_state->ctrl.leftStick = Vec2 {pad_state.leftThumbstickX, pad_state.leftThumbstickY};
        current_input_state->ctrl.rightStick = Vec2 {pad_state.rightThumbstickX, pad_state.rightThumbstickY};

        pad_reading->Release();
    }
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
        /* position & size */ CW_USEDEFAULT, CW_USEDEFAULT, 600, 600,
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

    D3D12Context d3d_context = initD3D12(window);
    TimingInfo timing_info = initTimingInfo();

    // NOTE: game input double-buffering
    // i think casey does this to prepare for asynchronous event
    // collection in a separate thread
    InputState input_states[2] = {};
    InputState* current_input_state = &input_states[0];
    InputState* previous_input_state = &input_states[1];

    IGameInput* microsoft_game_input_interface;
    HRESULT ginput_create_res = GameInputCreate(&microsoft_game_input_interface);
    ASSERT(SUCCEEDED(ginput_create_res));

    while (global_running) {

        // NOTE: measure and print the time the previous frame took
        measureTimingInfo(&timing_info);
        //printTimingInfo(&timing_info);

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

        // NOTE: wait for the backbuffer to be ready, by sleeping until its value
        // gets updated by the signal command we enqueued last time we rendered
        // to that framebuffer
        D3D12FrameContext* current_frame = &d3d_context.frames[d3d_context.current_frame_idx];
        if(current_frame->fence->GetCompletedValue() < current_frame->fence_ready_value) {
            current_frame->fence->SetEventOnCompletion(current_frame->fence_ready_value, current_frame->fence_wait_event);
            WaitForSingleObject(current_frame->fence_wait_event, INFINITE);
        }

        f32 clear_color[8] = {current_input_state->kb.MouseScreenPos.x, current_input_state->kb.MouseScreenPos.y, 0.0, 1};
        renderFrame(&d3d_context, current_frame, clear_color);

        // NOTE: Swap the user input buffers
        InputState* tmp = current_input_state;
        current_input_state = previous_input_state;
        previous_input_state = tmp;
    }

    return 0;
}
