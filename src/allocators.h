#pragma once

#include "common.h"

// ARENA

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

// POOL

template <typename T, usize N>
struct Pool {
    T slots[N];  
    u16 free_stack[N];
    u16* free_stack_ptr;
    u16 nb_allocated;
};

template <typename T, usize N>
void poolInitialize(Pool<T, N>* pool);

template <typename T, usize N>
T* PoolAcquireItem(Pool<T, N>* pool);

template <typename T, usize N>
void PoolReleaseItem(Pool<T, N>* pool, T* item);
