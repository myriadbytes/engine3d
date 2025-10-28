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
    platform_api.pushConstant = pushConstant;
    platform_api.createShader = createShader;
    platform_api.createPipeline = createPipeline;
    platform_api.setPipeline = setPipeline;
    platform_api.setVertexBuffer = setVertexBuffer;
    platform_api.drawCall = drawCall;

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
