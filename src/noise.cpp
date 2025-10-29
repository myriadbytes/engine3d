#include "noise.h"

// NOTE: This article is great to understand how simplex noise works, and has a reference implementation.
// https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/simplexnoise.pdf

// NOTE: The code in this file is copied pretty closely from Sebastien Rombauts' C++ implementation,
// who seems to have put a little more thought into performance when compared to the article implementation.
// https://github.com/SRombauts/SimplexNoise/blob/master/src/SimplexNoise.cpp

// TODO: OpenSimplex2 does not seem to use a precomputed permutation table, and
// the exposed functions additionally take in a seed. Maybe this is something that can
// be added to the classic simplex ?
// https://github.com/KdotJPG/OpenSimplex2/

constexpr u8 permutations[256] = {
    151, 160, 137, 91, 90, 15,
    131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23,
    190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33,
    88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166,
    77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244,
    102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196,
    135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123,
    5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42,
    223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
    129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228,
    251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107,
    49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254,
    138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
};

inline u8 perm_hash(u32 i) {
    return permutations[(u8)i];
}

inline i32 fastfloor(f32 x) {
    i32 i = (i32)x;
    return x < i ? (i - 1) : i;
}

// NOTE: This is too clever for me, but it basically maps the
// hash into a "random" gradient vector, and does the dot product
// between it and the direction vector (x, y).
f32 grad(i32 hash, f32 x, f32 y) {
    const i32 h = hash & 0x3F;
    const f32 u = h < 4 ? x : y;
    const f32 v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

f32 simplex_noise_2d(f32 x, f32 y) {
    f32 n0, n1, n2;

    // NOTE: Factors to skew a simplex grid into a regular grid.
    // These constants actually come from the math on how to stretch
    // a triangle grid over a square grid.
    // For example, F2 = 0.5*(sqrt(3.0)-1.0).
    constexpr f32 F2 = 0.366025403f;
    constexpr f32 G2 = 0.211324865f;

    // NOTE: Skew the simplex input space onto a square grid.
    // Once that is done, you can just use floor to find out which simplex cell you are on.
    const f32 s = (x + y) * F2;
    const f32 xs = x + s;
    const f32 ys = y + s;
    const i32 i = fastfloor(xs);
    const i32 j = fastfloor(ys);

    // NOTE: Unskew the floored coordinates back onto "simplex space".
    // You can then compute the offset into the simplex cell using the difference
    // between the starting x and y and those "floored" x and y.
    const f32 t = (f32)(i + j) * G2;
    const f32 X0 = i - t;
    const f32 Y0 = j - t;
    const f32 x0 = x - X0;
    const f32 y0 = y - Y0;

    // NOTE: A 2D simplex is an equilateral triangle, so when you stretch
    // it over the square grid there are actually two simplices covering one
    // grid square. So we need to figure out which one we're in.
    // i1 and j1 are offsets to use for the second vertex of the simplex.
    // The first is always +(0,0) and the last +(1,1), but the second one depends
    // on which simplex we are in.
    i32 i1, j1;
    if (x0 > y0) {
        i1 = 1;
        j1 = 0;
    } else {
        i1 = 0;
        j1 = 1;
    }

    // NOTE: Now we need to compute the offsets to those other simplex vertices.
    // We already had the offset to the first as x0,y0 so we just compute the two
    // remaining ones, using the offsets in straight grid space (i1, j1) computed
    // previously and G2 as a factor to tranform between simplex space and staight
    // grid space.
    const f32 x1 = x0 - i1 + G2;
    const f32 y1 = y0 - j1 + G2;
    const f32 x2 = x0 - 1.0f + 2.0f * G2;
    const f32 y2 = y0 - 1.0f + 2.0f * G2;

    // NOTE: Use the pseudo-hash lookup table to get a "random" value per simplex corner.
    const i32 gi0 = perm_hash(i + perm_hash(j));
    const i32 gi1 = perm_hash(i + i1 + perm_hash(j + j1));
    const i32 gi2 = perm_hash(i + 1 + perm_hash(j + 1));

    // NOTE: Calculate the contribution from the first corner.
    float t0 = 0.5f - x0*x0 - y0*y0;
    if (t0 < 0.0f) {
        n0 = 0.0f;
    } else {
        t0 *= t0;
        n0 = t0 * t0 * grad(gi0, x0, y0);
    }

    // NOTE: Calculate the contribution from the second corner.
    float t1 = 0.5f - x1*x1 - y1*y1;
    if (t1 < 0.0f) {
        n1 = 0.0f;
    } else {
        t1 *= t1;
        n1 = t1 * t1 * grad(gi1, x1, y1);
    }

    // NOTE: Calculate the contribution from the third corner.
    float t2 = 0.5f - x2*x2 - y2*y2;
    if (t2 < 0.0f) {
        n2 = 0.0f;
    } else {
        t2 *= t2;
        n2 = t2 * t2 * grad(gi2, x2, y2);
    }

    // NOTE: The scaling factor creates a value between -1 and 1.
    return 45.23065f * (n0 + n1 + n2);
}
