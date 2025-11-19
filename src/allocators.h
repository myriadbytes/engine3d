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

struct BuddyAllocationResult {
    usize offset;
    usize size;
};


// NOTE: The buddy allocator takes an arena to store its metadata. I could do a
// fully static version where the metadata size is computed at compile-time and
// inline in the struct like the pool, but there are like 3 template parameters
// so it gets a little hairy. Eh. We'll see.
void buddyInitalize(BuddyAllocator* allocator, Arena* metadata_arena, usize min_alloc_size, usize max_alloc_size, usize total_size);

BuddyAllocationResult buddyAlloc(BuddyAllocator* allocator, usize size);
void buddyFree(BuddyAllocator* allocator, usize offset);
