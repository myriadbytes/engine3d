#pragma once

#include "common.h"
#include "arena.h"

struct SimplexTable {
    u8 permutations[256];
};

SimplexTable* simplex_table_from_seed(u64 seed, Arena* arena);

f32 simplex_noise_2d(SimplexTable* simplex_table, f32 x, f32 y);
