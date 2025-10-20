#pragma once

// NOTE: none of this is optimized at all

#include "common.h"

struct Vec2 {
    union {
        struct {
            f32 x, y;
        };
        struct {
            f32 u, v;
        };
        f32 data[2];
    };
};

inline Vec2 operator+ (Vec2 a, Vec2 b) {
    Vec2 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    return result;
}

inline Vec2& operator+= (Vec2& a, Vec2 b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

inline Vec2 operator- (Vec2 a, Vec2 b) {
    Vec2 result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    return result;
}

inline Vec2 operator- (Vec2 a) {
    Vec2 result;
    result.x = -a.x;
    result.y = -a.y;
    return result;
}

inline Vec2 operator* (f32 x, Vec2 a) {
    Vec2 result;
    result.x = a.x * x;
    result.y = a.y * x;
    return result;
}

inline Vec2 operator* (Vec2 a, f32 x) {
    Vec2 result;
    result.x = a.x * x;
    result.y = a.y * x;
    return result;
}

struct Vec3 {
    union {
        struct {
            f32 x, y, z;
        };
        struct {
            f32 r, g, b;
        };
        f32 data[3];
    };
};

struct Vec4 {
    union {
        struct {
            f32 x, y, z, w;
        };
        struct {
            f32 r, g, b, a;
        };
        f32 data[4];
    };
};

struct Mat4 {
    f32 data[16];
};

inline Mat4 operator*(Mat4 a, Mat4 b) {
    Mat4 result{};

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.data[k * 4 + row] * b.data[col * 4 + k];
            }
            result.data[col * 4 + row] = sum;
        }
    }

    return result;
}
