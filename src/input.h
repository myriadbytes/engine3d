#pragma once

#include "common.h"
#include "maths.h"

// NOTE: https://docs.google.com/spreadsheets/d/1GSj0gKDxyWAecB3SIyEZ2ssPETZkkxn67gdIwL1zFUs

struct KeyboardState {
    Vec2 MouseScreenPos;
};

struct ControllerState {
    Vec2 leftStick;
    Vec2 rightStick;
};

// TODO: support for multiple controllers ?
struct InputState {
    b32 is_analog;

    KeyboardState kb;
    ControllerState ctrl;
};
