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
    // NOTE: Assert that:
    // - There is room for an item to be released
    // - The pointer is indeed from the pool
    ASSERT(pool->free_stack_ptr < pool->free_stack + N);
    ASSERT(item >= pool->slots);
    ASSERT(item < pool->slots + N);

    // TRICKY: Pointer arithmetic here to get the slot index
    // from just the item adress.
    u16 slot = (u16)(item - pool->slots);

    pool->free_stack_ptr++;
    *(pool->free_stack_ptr) = slot;

    pool->nb_allocated--;
}

// BUDDY

// NOTE: This is a good resource, albeit a bit confusing. Most of
// the implementation is taken from there, removing all the optimization
// for now to make it clearer / easier.
// https://jvernay.fr/en/blog/buddy-allocator/implementation/

struct BuddySlotMetadata {
    b8 allocated;
    b8 freelist_valid; // NOTE: Is it safe to read prev_/next_idx ?
    u8 pool_idx;  

    u32 prev_idx;
    u32 next_idx;
};

struct BuddyFreeList {
    u32 head_idx;
    u32 tail_idx;
};

struct BuddyAllocator {
    // NOTE: These are just the parameters of the allocator.
    usize min_alloc_size;
    usize max_alloc_size;
    usize total_size;

    // NOTE: The maximum allocation size is divided by two until it reaches
    // the minimum allocation size. These sizes give us "pools" for potential
    // allocations, e.g:
    // Alloc size: (max) 64 KB -> 32 KB -> 16 KB -> 8 KB (min)
    // ------------------------||-------||-------||----------
    // Pool index:        3        2        1        0
    usize pool_count;
    // NOTE: The min allocation size is just called "atom" for short.
    // If the total memory is 2 MB and the min alloc size is 4 KB, 
    // there are 512 atoms.
    usize atoms_count;

    // NOTE: The info about a slot is store in an array indexed with the
    // atom number. So if the minimum allocation size is 4KB, the info
    // about the slot that begins at 96 KB is in slots_info[24].
    BuddySlotMetadata* slots_meta;
    // NOTE: Each pool has its own free list.
    BuddyFreeList* pool_free_lists;
};

struct BuddyAllocation {
    usize offset;
    usize size;
};

// NOTE: The buddy allocator takes an arena to store its metadata. I could do a
// fully static version where the metadata size is computed at compile-time and
// inline in the struct like the pool, but there are like 3 template parameters
// so it gets a little hairy. Eh. We'll see.
void buddyInitalize(BuddyAllocator* allocator, Arena* metadata_arena, usize min_alloc_size, usize max_alloc_size, usize total_size);

BuddyAllocation buddyAlloc(BuddyAllocator* allocator, usize size);
void buddyFree(BuddyAllocator* allocator, usize offset);
