#include "allocators.h"

// ARENA 

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

// POOL

template <typename T, usize N>
void poolInitialize(Pool<T, N>* pool) {
    // TODO: Maybe zero out the memory ?

    // NOTE: We are using u16 for the slot indices, so check that
    // this will not cause troubles.
    ASSERT(N < UINT16_MAX);

    pool->nb_allocated = 0;

    // NOTE: Fill the free stack with all the indices.
    for (int i = 0; i < N; i++) {
        pool->free_stack[i] = N - i - 1;
    }

    // NOTE: The first time we want to get a chunk, we'll read off from
    // the end of the free slots stack and decrement the pointer.
    pool->free_stack_ptr = pool->free_stack + (N - 1);
}

template <typename T, usize N>
T* PoolAcquireItem(Pool<T, N>* pool) {
    ASSERT(pool->free_stack_ptr >= pool->free_stack);

    u16 slot = *(pool->free_stack_ptr);

    pool->free_stack_ptr--;
    pool->nb_allocated++;

    return pool->slots + slot;
}

template <typename T, usize N>
void PoolReleaseItem(Pool<T, N>* pool, T* item) {
    ASSERT(pool->free_stack_ptr < pool->free_stack + N);

    // TRICKY: Pointer arithmetic here to get the slot index
    // from just the item adress.
    u16 slot = (u16)(item - pool->slots);

    pool->free_stack_ptr++;
    *(pool->free_stack_ptr) = slot;

    pool->nb_allocated--;
}

