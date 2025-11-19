#pragma once

#include "common.h"

struct SimplexTable {
    u8 permutations[256];
};

void simplex_table_from_seed(SimplexTable* to_init, u64 seed);

f32 simplex_noise_2d(SimplexTable* simplex_table, f32 x, f32 y);
