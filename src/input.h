#pragma once

#include "common.h"
#include "maths.h"

// NOTE: these are based on the scancodes that Windows reports
// https://docs.google.com/spreadsheets/d/1GSj0gKDxyWAecB3SIyEZ2ssPETZkkxn67gdIwL1zFUs
enum Scancode {
    SCANCODE_ESC = 0x1,
    SCANCODE_0 = 0x2,
    SCANCODE_1 = 0x3,
    SCANCODE_2 = 0x4,
    SCANCODE_3 = 0x5,
    SCANCODE_4 = 0x6,
    SCANCODE_5 = 0x7,
    SCANCODE_6 = 0x8,
    SCANCODE_7 = 0x9,
    SCANCODE_8 = 0xA,
    SCANCODE_9 = 0xB,
    SCANCODE_TAB = 0xF,
    SCANCODE_Q = 0x10,
    SCANCODE_W = 0x11,
    SCANCODE_E = 0x12,
    SCANCODE_R = 0x13,
    SCANCODE_T = 0x14,
    SCANCODE_Y = 0x15,
    SCANCODE_U = 0x16,
    SCANCODE_I = 0x17,
    SCANCODE_O = 0x18,
    SCANCODE_P = 0x19,
    SCANCODE_A = 0x1E,
    SCANCODE_S = 0x1F,
    SCANCODE_D = 0x20,
    SCANCODE_F = 0x21,
    SCANCODE_G = 0x22,
    SCANCODE_H = 0x23,
    SCANCODE_J = 0x24,
    SCANCODE_K = 0x25,
    SCANCODE_L = 0x26,
    SCANCODE_LSHIFT = 0x2A,
    SCANCODE_Z = 0x2C,
    SCANCODE_X = 0x2D,
    SCANCODE_C = 0x2E,
    SCANCODE_V = 0x2F,
    SCANCODE_B = 0x30,
    SCANCODE_N = 0x31,
    SCANCODE_M = 0x32,
    SCANCODE_RSHIFT = 0x36,
    SCANCODE_SPACE = 0x39,
    SCANCODE_COUNT = 0x1FF,
};

struct ButtonState {
    b8 is_down;
    u8 transitions;
};

// NOTE: The keys array is indexed by scancodes because i feel like
// it is more common to want to know if a key "position" has been pressed
// (e.g. for WASD movement) than if the key pressed has the letter W or Z written on it.
// However you sometimes want the exact letter (e.g. I for inventory) so i need
// to write a keycodeToScancode function eventually
struct KeyboardState {
    v2 mouse_delta;
    ButtonState keys[SCANCODE_COUNT];
};

struct ControllerState {
    union {
        ButtonState buttons[6];
        struct {
            ButtonState a;
            ButtonState b;
            ButtonState x;
            ButtonState y;
            ButtonState rb;
            ButtonState lb;
        };
    };
    v2 left_stick;
    v2 right_stick;
};

// TODO: support for multiple controllers ?
struct InputState {
    b32 is_analog;

    KeyboardState kb;
    ControllerState ctrl;
};
