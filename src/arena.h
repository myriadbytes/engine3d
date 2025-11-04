#pragma once

#include "common.h"

struct Arena {
    u8* base;
    usize capacity;
    usize used;
};

Arena makeArena(void* base, usize capacity);
void* pushBytes(Arena* arena, usize size);
void* pushZeros(Arena* arena, usize size);
#define pushStruct(arena, type) (type*) pushBytes(arena, sizeof(type))
void clearArena(Arena* arena);
