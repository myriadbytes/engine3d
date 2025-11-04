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

void* pushZeros(Arena* arena, usize size) {
    void* memory = pushBytes(arena, size);

    for (usize i = 0; i < size; i++) {
        ((u8*)memory)[i] = 0;
    }

    return memory;
}

void clearArena(Arena* arena) {
    arena->used = 0;
}
