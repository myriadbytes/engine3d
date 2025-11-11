#pragma once

// NOTE: none of this is optimized at all

#include <math.h>
#include "common.h"

// MATH OPERATIONS
// NOTE: They are prefixed with "m" in order
// to not collide with the math.h functions
// until we stop including it.

#define PI32 3.14159265359f

inline i32 mfloor(f32 x) {
    i32 truncated = (i32)x;
    return x < truncated ? (truncated - 1) : truncated;
}

// MATRIX STRUCTURES

struct v2 {
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

struct v3 {
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

struct v4 {
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

struct v2i {
    union {
        struct {
            i32 x, y;
        };
        struct {
            i32 u, v;
        };
        i32 data[2];
    };
};

struct v3i {
    union {
        struct {
            i32 x, y, z;
        };
        struct {
            i32 r, g, b;
        };
        i32 data[3];
    };
};

struct v4i {
    union {
        struct {
            i32 x, y, z, w;
        };
        struct {
            i32 r, g, b, a;
        };
        i32 data[4];
    };
};

struct m4 {
    f32 data[16];

    static inline m4 identity() {
        return m4 {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
    }
};

// MATRIX OPERATORS

inline v2 operator+ (v2 a, v2 b) {
    v2 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    return result;
}

inline v3 operator+ (v3 a, v3 b) {
    v3 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    return result;
}

inline v4 operator+ (v4 a, v4 b) {
    v4 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    result.w = a.w + b.w;
    return result;
}

inline v2& operator+= (v2& a, v2 b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

inline v3 operator+= (v3& a, v3 b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

inline v4 operator+= (v4& a, v4 b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    a.w += b.w;
    return a;
}

inline v2 operator- (v2 a, v2 b) {
    v2 result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    return result;
}

inline v3 operator- (v3 a, v3 b) {
    v3 result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    result.z = a.z - b.z;
    return result;
}

inline v4 operator- (v4 a, v4 b) {
    v4 result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    result.z = a.z - b.z;
    result.w = a.w - b.w;
    return result;
}

inline v2 operator- (v2 a) {
    v2 result;
    result.x = -a.x;
    result.y = -a.y;
    return result;
}

inline v3 operator- (v3 a) {
    v3 result;
    result.x = -a.x;
    result.y = -a.y;
    result.z = -a.z;
    return result;
}

inline v4 operator- (v4 a) {
    v4 result;
    result.x = -a.x;
    result.y = -a.y;
    result.z = -a.z;
    result.w = -a.w;
    return result;
}

inline v2 operator* (f32 scalar, v2 a) {
    v2 result;
    result.x = a.x * scalar;
    result.y = a.y * scalar;
    return result;
}

inline v2 operator* (v2 a, f32 scalar) {
    return scalar * a;
}

inline v3 operator* (f32 scalar, v3 a) {
    v3 result;
    result.x = a.x * scalar;
    result.y = a.y * scalar;
    result.z = a.z * scalar;
    return result;
}

inline v3 operator* (v3 a, f32 scalar) {
    return scalar * a;
}

inline v3 operator* (v3 a, v3 b) {
    // element-wise product, like in GLSL
    v3 result;
    result.x = a.x * b.x;
    result.y = a.y * b.y;
    result.z = a.z * b.z;
    return result;
}

inline v4 operator* (f32 scalar, v4 a) {
    v4 result;
    result.x = a.x * scalar;
    result.y = a.y * scalar;
    result.z = a.z * scalar;
    result.w = a.w * scalar;
    return result;
}

inline v4 operator* (v4 a, f32 scalar) {
    return scalar * a;
}

inline m4 operator*(m4 a, m4 b) {
    m4 result;

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

inline v2 operator/ (v2 a, f32 scalar) {
    v2 result;
    result.x = a.x * scalar;
    result.y = a.y * scalar;
    return result;
}

inline v3 operator/ (v3 a, f32 scalar) {
    v3 result;
    result.x = a.x / scalar;
    result.y = a.y / scalar;
    result.z = a.z / scalar;
    return result;
}

inline v4 operator/ (v4 a, f32 scalar) {
    v4 result;
    result.x = a.x / scalar;
    result.y = a.y / scalar;
    result.z = a.z / scalar;
    result.w = a.w / scalar;
    return result;
}

inline bool operator== (v3i& a, v3i& b) {
    return a.x == b.x
    &&     a.y == b.y
    &&     a.z == b.z;   
}
// MATRIX FUNCTIONS

inline f32 dot(v3 a, v3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline f32 dot(v2 a, v2 b) {
    return a.x * b.x + a.y * b.y;
}

inline f32 lengthSquared(v3 a) {
    return dot(a, a);
}

inline f32 lengthSquared(v2 a) {
    return dot(a, a);
}

inline f32 length(v3 a) {
    return sqrtf(lengthSquared(a));
}

inline f32 length(v2 a) {
    return sqrtf(lengthSquared(a));
}

inline v3 normalize(v3 a) {
    return a * (1.0f / length(a));
}

inline v2 normalize(v2 a) {
    return a * (1.0f / length(a));
}

inline v3 cross(v3 a, v3 b) {
    v3 result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

inline v3 abs(v3 a) {
    v3 result;
    result.x = fabsf(a.x);
    result.y = fabsf(a.y);
    result.z = fabsf(a.z);
    return result;
}

inline f32 max(f32 a, f32 b) {
    return b > a ? b : a;
}

inline f32 min(f32 a, f32 b) {
    return b < a ? b : a;
}

// NOTE: "Columns" here is used in the traditional sense, as you would see in a math textbook.
// The actual memory ordering is irrelevant for usage.
inline m4 makeMatrixFromColumns(v3 a, v3 b, v3 c) {
    return m4 {
        a.x, a.y, a.z, 0.f,
        b.x, b.y, b.z, 0.f,
        c.x, c.y, c.z, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
}

// NOTE: "Rows" here is used in the traditional sense, as you would see in a math textbook.
// The actual memory ordering is irrelevant for usage.
inline m4 makeMatrixFromRows(v3 a, v3 b, v3 c) {
    return m4 {
        a.x, b.x, c.x, 0.f,
        a.y, b.y, c.y, 0.f,
        a.z, b.z, c.z, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
}

inline m4 makeTranslation(f32 x, f32 y, f32 z) {
    return m4 {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
          x,   y,   z, 1.f,
    };
}

inline m4 makeTranslation(v3 a) {
    return makeTranslation(a.x, a.y, a.z);
}

inline m4 makeScale(f32 x, f32 y, f32 z) {
    return m4 {
          x, 0.f, 0.f, 0.f,
        0.f,   y, 0.f, 0.f,
        0.f, 0.f,   z, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
}

inline m4 makeScale(v3 a) {
    return makeScale(a.x, a.y, a.z);
}

m4 lookAt(v3 eye, v3 target);
m4 makeProjection(f32 near, f32 far, f32 fov);

// GENERAL

inline f32 clamp(f32 x, f32 min, f32 max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

// RNG

// NOTE: this is a standard xorshift64* algorithm
// https://en.wikipedia.org/wiki/Xorshift#xorshift*

typedef u64 RandomSeries;

inline u32 randomNextU32(RandomSeries* series) {
    *series = *series ^ (*series >> 12);
    *series = *series ^ (*series << 25);
    *series = *series ^ (*series >> 27);

    return (*series * 0x2545F4914F6CDD1DULL) >> 32;
}

// NOTE: random float between 0 and 1
inline f32 randomUnilateral(RandomSeries* series) {
    u32 sample = randomNextU32(series);

    return (1.f / (f32)UINT32_MAX) * (f32)sample;
}
