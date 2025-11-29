#pragma once

// WARNING: None of this has been optimized at all.
// The matrix multiplying especially is a low hanging
// fruit.

#include <math.h>
#include "common.h"

// NOTE:  OPERATIONS
// They are prefixed with "m" in order
// to not collide with the math.h functions
// until we stop including it.

#define PI32 3.14159265359f

inline i32 mfloor(f32 x) {
    i32 truncated = (i32)x;
    return x < truncated ? (truncated - 1) : truncated;
}

// NOTE: STRUCTURES

template <typename T, usize N>
struct Vector {
    T data[N];  
    
    constexpr T& x() requires (N > 0) { return data[0]; }
    constexpr T& y() requires (N > 1) { return data[1]; }
    constexpr T& z() requires (N > 2) { return data[2]; }
    constexpr T& w() requires (N > 3) { return data[3]; }

    constexpr T& r() requires (N > 0) { return data[0]; }
    constexpr T& g() requires (N > 1) { return data[1]; }
    constexpr T& b() requires (N > 2) { return data[2]; }
    constexpr T& a() requires (N > 3) { return data[3]; }

    constexpr T& u() requires (N > 0) { return data[0]; }
    constexpr T& v() requires (N > 1) { return data[1]; }
};

using v2 = Vector<f32, 2>;
using v3 = Vector<f32, 3>;
using v4 = Vector<f32, 4>;

using v2i = Vector<i32, 2>;
using v3i = Vector<i32, 3>;
using v4i = Vector<i32, 3>;

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

// NOTE: OPERATORS

// NOTE: Most are implemented as constexpr loops. I have checked
// on Godbolt and the loop disappears when using atleast -O2.
// Let's hope the compile time doesn't explode with all these
// templates !

template <typename T, usize N>
constexpr Vector<T, N> operator+ (Vector<T, N> a, Vector<T, N> b) {
    Vector<T, N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = a.data[i] + b.data[i];
    }

    return result;
}

template <typename T, usize N>
constexpr Vector<T, N>& operator+= (Vector<T,N>& a, Vector<T,N> b) {

    for (usize i = 0; i < N; ++i) {
        a.data[i] += b.data[i];
    }

    return a;
}

template <typename T, usize N>
constexpr Vector<T,N> operator- (Vector<T,N> a, Vector<T,N> b) {
    Vector<T,N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = a.data[i] - b.data[i];
    }

    return result;
}

template <typename T, usize N>
constexpr Vector<T,N> operator- (Vector<T,N> a) {
    Vector<T,N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = -a.data[i];
    }

    return result;
}


template <typename T, usize N>
constexpr Vector<T, N> operator* (f32 scalar, Vector<T, N> a) {
    Vector<T, N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = scalar * a.data[i];
    }

    return result;
}

template <typename T, usize N>
constexpr Vector<T, N> operator* (Vector<T, N> a, f32 scalar) {
    return scalar * a;
}

// NOTE: This is an element-wise product,
// for GLSL compatibility.
template <typename T, usize N>
constexpr Vector<T, N> operator* (Vector<T, N> a, Vector<T, N> b) {
    Vector<T, N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = a.data[i] * b.data[i];
    }

    return result;
}

template <typename T, usize N>
constexpr Vector<T, N> operator/ (Vector<T, N> a, f32 scalar) {
    Vector<T, N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = a.data[i] / scalar;
    }

    return result;
}

template <typename T, usize N>
constexpr bool operator== (Vector<T, N> a, Vector<T, N> b) {

    for (usize i = 0; i < N; ++i) {
        if (a.data[i] != b.data[i]) return false;
    }

    return true;
}

constexpr m4 operator*(const m4& a, const m4& b) {
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

// NOTE: FUNCTIONS

template <typename T, usize N>
constexpr f32 dot (Vector<T, N> a, Vector<T, N> b) {
    f32 result = 0;

    for (usize i = 0; i < N; ++i) {
        result += a.data[i] * b.data[i];
    }

    return result;
}

template <typename T, usize N>
constexpr f32 length2 (Vector<T, N> a) {
    return dot(a, a);
}

template <typename T, usize N>
constexpr f32 length (Vector<T, N> a) {
    return sqrtf(length2(a));
}

template <typename T, usize N>
constexpr Vector<T, N> normalize (Vector<T, N> a) {
    return a * (1.0f / length(a));
}

constexpr v3 cross(v3 a, v3 b) {
    v3 result;
    result.x() = a.y() * b.z() - a.z() * b.y();
    result.y() = a.z() * b.x() - a.x() * b.z();
    result.z() = a.x() * b.y() - a.y() * b.x();
    return result;
}

template <typename T, usize N>
constexpr Vector<T, N> abs(Vector<T, N> a) {
    Vector<T, N> result;

    for (usize i = 0; i < N; ++i) {
        result.data[i] = fabsf(a.data[i]);
    }

    return result;
}

constexpr f32 max(f32 a, f32 b) {
    return b > a ? b : a;
}

constexpr f32 min(f32 a, f32 b) {
    return b < a ? b : a;
}

// NOTE: "Columns" here is used in the traditional sense, as you would see in a math textbook.
// The actual memory ordering is irrelevant for usage.
inline m4 makeMatrixFromColumns(v3 a, v3 b, v3 c) {
    return m4 {
        a.x(), a.y(), a.z(), 0.f,
        b.x(), b.y(), b.z(), 0.f,
        c.x(), c.y(), c.z(), 0.f,
        0.f,   0.f,   0.f,   1.f
    };
}

// NOTE: "Rows" here is used in the traditional sense, as you would see in a math textbook.
// The actual memory ordering is irrelevant for usage.
inline m4 makeMatrixFromRows(v3 a, v3 b, v3 c) {
    return m4 {
        a.x(), b.x(), c.x(), 0.f,
        a.y(), b.y(), c.y(), 0.f,
        a.z(), b.z(), c.z(), 0.f,
        0.f,   0.f,   0.f,   1.f
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
    return makeTranslation(a.x(), a.y(), a.z());
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
    return makeScale(a.x(), a.y(), a.z());
}

m4 lookAt(v3 eye, v3 target);
m4 makeProjection(f32 near, f32 far, f32 fov, f32 aspect);

// GENERAL

template< typename T>
constexpr T clamp(T x, T min, T max) {
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
