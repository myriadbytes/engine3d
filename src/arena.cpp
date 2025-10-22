#include "arena.h"

Arena makeArena(void* base, usize capacity) {
    return Arena {(u8*)base, capacity, 0};
}

void* pushBytes(Arena* arena, usize size) {
    ASSERT(arena->used + size < arena->capacity);

    void* memory = (void*)(arena->base + arena->used);
    arena->used += size;

    return memory;
}

void clearArena(Arena* arena) {
    arena->used = 0;
}
