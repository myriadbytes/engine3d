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

// BUDDY

inline u8 buddyFastLog2(usize v) { return v <= 1 ? 0 : 64 - __builtin_clzll(v - 1); }

void buddyInitalize(BuddyAllocator* allocator, Arena* metadata_arena, usize min_alloc_size, usize max_alloc_size, usize total_size) {
    // NOTE: Initialize parameters.
    allocator->min_alloc_size = min_alloc_size;
    allocator->max_alloc_size = max_alloc_size;
    allocator->total_size = total_size;

    allocator->pool_count = 1 + buddyFastLog2(max_alloc_size/min_alloc_size);    
    allocator->atoms_count = total_size / min_alloc_size;

    // NOTE: We are using u32 to index into the slot info array, so we need
    // to check the number of atoms. UINT32_MAX is used as a special value,
    // kind of like NULL (but we can't use 0 because that's a valid index).
    ASSERT(allocator->atoms_count < UINT32_MAX);

    // NOTE: Allocate and initialize the memory for everything.
    allocator->slots_meta = (BuddySlotMetadata*) pushZeros(metadata_arena, allocator->atoms_count * sizeof(BuddySlotMetadata));
    for (u32 slot_idx = 0; slot_idx < allocator->atoms_count; slot_idx++) {
        allocator->slots_meta[slot_idx].prev_idx = UINT32_MAX;
        allocator->slots_meta[slot_idx].next_idx = UINT32_MAX;
    }

    allocator->pool_free_lists = (BuddyFreeList*) pushBytes(metadata_arena, allocator->pool_count * sizeof(BuddyFreeList));
    for (u32 pool_idx = 0; pool_idx < allocator->pool_count; pool_idx++) {
        allocator->pool_free_lists[pool_idx] = {UINT32_MAX, UINT32_MAX};
    }

    // NOTE: Build the free list for the pool of largest slots.
    u32 atoms_in_largest_slot = max_alloc_size / min_alloc_size;
    u32 last_slot_idx = allocator->atoms_count - atoms_in_largest_slot;
    for (u32 slot_start_idx = 0; slot_start_idx <= last_slot_idx; slot_start_idx += atoms_in_largest_slot) {
        allocator->slots_meta[slot_start_idx].freelist_valid = true;
        allocator->slots_meta[slot_start_idx].pool_idx = allocator->pool_count-1;

        allocator->slots_meta[slot_start_idx].prev_idx = (slot_start_idx == 0 ? UINT32_MAX : slot_start_idx-atoms_in_largest_slot);
        allocator->slots_meta[slot_start_idx].next_idx = (slot_start_idx == last_slot_idx ? UINT32_MAX : slot_start_idx+atoms_in_largest_slot);
    }

    allocator->pool_free_lists[allocator->pool_count - 1].head_idx = 0;
    allocator->pool_free_lists[allocator->pool_count - 1].tail_idx = last_slot_idx;
}

void buddyUtilsAddHeadToFreeList(BuddyAllocator* allocator, u32 slot_idx) {
    BuddySlotMetadata* slot = &allocator->slots_meta[slot_idx];
    BuddyFreeList* free_list = &allocator->pool_free_lists[slot->pool_idx];

    // NOTE: Assert that this slot is not already part of a free list.
    ASSERT(slot->prev_idx == UINT32_MAX);
    ASSERT(slot->next_idx == UINT32_MAX);

    u32 old_head_idx = free_list->head_idx;
    free_list->head_idx = slot_idx;

    // NOTE: If the pool was previously empty, then the slot is both the
    // head and the tail.
    if (old_head_idx == UINT32_MAX) {
        ASSERT(free_list->tail_idx == UINT32_MAX);
        free_list->tail_idx = slot_idx;
    }
    // NOTE: If not, then we need to link the new head and the old one.
    else {
        slot->next_idx = old_head_idx;
        allocator->slots_meta[old_head_idx].prev_idx = slot_idx;
    }

    slot->freelist_valid = true;
}

void buddyUtilsRemoveFromFreeList(BuddyAllocator* allocator, u32 slot_idx) {
    BuddySlotMetadata* slot = &allocator->slots_meta[slot_idx];
    BuddyFreeList* free_list = &allocator->pool_free_lists[slot->pool_idx];
    ASSERT(slot->freelist_valid);

    // NOTE: The slot is the first in its free list.
    if (slot->prev_idx == UINT32_MAX) {
        ASSERT(free_list->head_idx == slot_idx);

        free_list->head_idx = slot->next_idx;
    } else {
        allocator->slots_meta[slot->prev_idx].next_idx = slot->next_idx;
    }

    // NOTE: The slot is the last in its free list.
    if (slot->next_idx == UINT32_MAX) {
        ASSERT(free_list->tail_idx == slot_idx);

        free_list->tail_idx = slot->prev_idx;
    } else {
        allocator->slots_meta[slot->next_idx].prev_idx = slot->prev_idx;
    }

    slot->prev_idx = UINT32_MAX;
    slot->next_idx = UINT32_MAX;
    slot->freelist_valid = false;
}

BuddyAllocation buddyAlloc(BuddyAllocator* allocator, usize size) {

    if (size > allocator->max_alloc_size) return {0, 0};
    if (size < allocator->min_alloc_size) size = allocator->min_alloc_size;

    u8 desired_pool_idx = buddyFastLog2(size) - buddyFastLog2(allocator->min_alloc_size);

    // NOTE: Start from the desired pool, and go up in allocation
    // sizes until there is a slot available.
    u8 available_pool_idx = desired_pool_idx;
    u32 slot_idx = UINT32_MAX;
    while (available_pool_idx < allocator->pool_count) {
        slot_idx = allocator->pool_free_lists[available_pool_idx].head_idx;
        if (slot_idx != UINT32_MAX) break;

        available_pool_idx++;
    }

    // NOTE: We didn't find any slot in any free list. OOM !
    if (slot_idx == UINT32_MAX) return {0, 0};

    // NOTE: Remove the slot we found from its free list.
    BuddySlotMetadata* slot = &allocator->slots_meta[slot_idx];
    ASSERT(!slot->allocated);
    ASSERT(slot->freelist_valid);
    buddyUtilsRemoveFromFreeList(allocator, slot_idx);

    // NOTE: If the slot we found is in a larger pool, we need to
    // subdivide it.
    u8 pool_idx = available_pool_idx;
    while (pool_idx > desired_pool_idx) {
        
        // NOTE: We now consider the slot from the perspective
        // of the next pool.
        pool_idx--;

        // NOTE: Using a xor trick, we get the index of the buddy.
        // Here we are keeping the left buddy for further subdividing
        // and adding the right buddy to the free list of that pool,
        // but this trick also works to get the left buddy index from a
        // right buddy index since xor toggles the bit.
        u32 buddy_idx = slot_idx ^ (1 << pool_idx);

        BuddySlotMetadata* buddy_slot = &allocator->slots_meta[buddy_idx];
        ASSERT(!buddy_slot->allocated);
        ASSERT(!buddy_slot->freelist_valid);

        buddy_slot->pool_idx = pool_idx;
        buddyUtilsAddHeadToFreeList(allocator, buddy_idx);
    }

    // NOTE: The (right) buddy got added to its pool's free list each time,
    // so now we're left with a single left buddy that needs some metadata.
    slot->allocated = true;
    slot->pool_idx = pool_idx;

    BuddyAllocation result = {};
    result.offset = slot_idx * allocator->min_alloc_size;
    result.size = 1 << (buddyFastLog2(allocator->min_alloc_size) + pool_idx);

    return result;
}

void buddyFree(BuddyAllocator* allocator, usize offset) {
    ASSERT(offset < allocator->total_size);

    u32 slot_idx = offset / allocator->min_alloc_size;
    BuddySlotMetadata* slot = &allocator->slots_meta[slot_idx];

    // NOTE: The allocated slot should not be part of a free list,
    // because it is... well.. not free.
    ASSERT(slot->allocated);
    ASSERT(!slot->freelist_valid);

    // NOTE: If this slot's buddy is free, we can merge them and move up
    // to the next bigger pool, repeating while the merged slot's buddy
    // is free.
    slot->allocated = false;
    u8 pool_idx = slot->pool_idx;
    while (pool_idx < allocator->pool_count - 1) {
        // NOTE: Same bitwise trick as in the insertion, see the comment there.
        u32 buddy_idx = slot_idx ^ (1 << pool_idx);
        BuddySlotMetadata* buddy_slot = &allocator->slots_meta[buddy_idx];

        // NOTE: We cannot merge if:
        // - The buddy is not in a free list.
        // - The buddy is subdivided and used in a smaller allocation pool.
        if (!buddy_slot->freelist_valid || buddy_slot->pool_idx < pool_idx) {
            break;
        }

        ASSERT(!buddy_slot->allocated);
        ASSERT(buddy_slot->pool_idx == pool_idx);

        // NOTE: Remove the buddy about to be merged from its free list, since
        // the merged buddies are gonna be added to the next pool's free list.
        buddyUtilsRemoveFromFreeList(allocator, buddy_idx);

        // NOTE: Update the slot idx to be the idx of the left buddy.
        slot_idx = slot_idx < buddy_idx ? slot_idx : buddy_idx;

        pool_idx++;
    }

    // Add the new merged (or not) block to the right free list.
    slot = &allocator->slots_meta[slot_idx];
    ASSERT(!slot->allocated);
    slot->pool_idx = pool_idx;
    buddyUtilsAddHeadToFreeList(allocator, slot_idx);
}

usize buddyMeasure(BuddyAllocator* allocator) {
    usize free_space = 0;   

    usize pool_slot_size = allocator->min_alloc_size;
    usize pool_idx = 0;

    while (pool_idx < allocator->pool_count) {
        u32 head = allocator->pool_free_lists[pool_idx].head_idx;
        u32 tail = allocator->pool_free_lists[pool_idx].tail_idx;

        // NOTE: Nothing free in that pool, so it is either
        // full or not yet created by subdividing a bigger pool.
        if (head == UINT32_MAX) {
            pool_idx++;
            pool_slot_size *= 2;
            continue;
        };

        u32 slot_idx = head;
        while (slot_idx <= tail) {
            BuddySlotMetadata& slot = allocator->slots_meta[slot_idx]; 
            ASSERT(slot.freelist_valid && !slot.allocated);

            free_space += pool_slot_size;
            slot_idx = slot.next_idx;
        }

        pool_idx++;
        pool_slot_size *= 2;
    }

    return allocator->total_size - free_space;
}
