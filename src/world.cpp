#include "world.h"

/*
// NOTE: ChatGPT wrote that. I hope it's a good hash.
usize chunkPositionHash(v3i chunk_position) {
    usize hash = 0;
    hash ^= (usize)(chunk_position.x * 73856093);
    hash ^= (usize)(chunk_position.y * 19349663);
    hash ^= (usize)(chunk_position.z * 83492791);
    return hash;
}

Chunk* getChunkByIndex(WorldHashMap* world, usize idx) {
    ASSERT(idx < HASHMAP_SIZE);

    if (world->entries[idx].is_occupied) {
        ASSERT(world->entries[idx].key == world->entries[idx].value->chunk_position);
        return world->entries[idx].value;
    } else {
        return NULL;
    }
}

// NOTE: This is just a hashmap "contains" function.
b32 isChunkInWorld(WorldHashMap* world, v3i chunk_position) {
    usize hash = chunkPositionHash(chunk_position);
    usize idx = hash % HASHMAP_SIZE;
    u32 lookup_home_dist = 0;

    while (true) {
        WorldEntry* iter_entry = &world->entries[idx];

        // NOTE: We have iterated on the whole cluster, arrived at
        // an empty cell, and didn't find the entry.
        if (!iter_entry->is_occupied) {
            return false;
        };

        // NOTE: A nice property of Robin-Hood hashing is that we
        // know that a possible new entry would have displaced others
        // if its home distance gets greater. So if we arrive at this
        // case we know that the entry was never inserted.
        if (lookup_home_dist > iter_entry->home_distance) {
            return false;
        };

        // NOTE: WE found the it ! Yay !
        if (iter_entry->key == chunk_position) {
            return true;
        }

        // NOTE: Keep looking forward.
        idx = (idx + 1) % HASHMAP_SIZE;
        lookup_home_dist++;
    }
}

// NOTE: This is called "robin-hood" hashing.
void worldInsert(WorldHashMap* world, v3i chunk_position, Chunk* chunk) {
    ASSERT(!isChunkInWorld(world, chunk_position));

    usize hash = chunkPositionHash(chunk_position);
    usize idx = hash % HASHMAP_SIZE;

    WorldEntry to_insert_entry = {};
    to_insert_entry.hash = hash;
    to_insert_entry.key = chunk_position;
    to_insert_entry.value = chunk;
    to_insert_entry.is_occupied = true;
    to_insert_entry.home_distance = 0;

    // TODO: Error when trying to insert at a chunk position where a chunk
    // is already loaded.
    while (true) {
        WorldEntry* iter_entry = &world->entries[idx];

        // NOTE: We found an empty slot. Insert the entry to be inserted.
        if (!iter_entry->is_occupied) {
            *iter_entry = to_insert_entry;
            world->nb_occupied++;
            break;
        }

        // NOTE: We found an entry that is closer to home than the
        // entry to be inserted. The new entry gets that spot,
        // and the entry to be inserted is now the entry that was
        // at that spot.
        if (iter_entry->home_distance < to_insert_entry.home_distance) {
            WorldEntry tmp = *iter_entry;
            *iter_entry = to_insert_entry;
            to_insert_entry = tmp;
        }

        // NOTE: Move forward in the buckets, looping around if necessary.
        idx = (idx + 1) % HASHMAP_SIZE;
        to_insert_entry.home_distance++;
    }
}

// NOTE: This is called "backward-shift deletion".
void worldDelete(WorldHashMap* world, v3i chunk_position) {

    usize hash = chunkPositionHash(chunk_position);
    usize idx = hash % HASHMAP_SIZE;
    u32 lookup_home_dist = 0;

    // NOTE: Find the entry to delete.
    while (true) {
        WorldEntry* iter_entry = &world->entries[idx];

        if ((!iter_entry->is_occupied) || (lookup_home_dist > iter_entry->home_distance)) {
            // NOTE: I think that trying to delete a chunk that is not loaded
            // indicates a logic error in some other code.
            ASSERT(false);
        };

        // NOTE: WE found the it ! Yay !
        if (iter_entry->key == chunk_position) {
            break;
        }

        // NOTE: Keep looking forward.
        idx = (idx + 1) % HASHMAP_SIZE;
        lookup_home_dist++;
    }

    // NOTE: Delete the entry.
    world->entries[idx] = {};
    world->nb_occupied--;

    // NOTE: Shift all subsequent entries to prevent any hole from forming
    // and to bring the entries closer to their "home" bucket.
    usize backshift_idx = idx;
    while (true) {
        WorldEntry* iter_entry = &world->entries[backshift_idx];
        WorldEntry* next_entry = &world->entries[(backshift_idx + 1) % HASHMAP_SIZE];

        // NOTE: Stop when:
        // - We're at the end of the cluster
        // - The next entry is already where it should be.
        if (!next_entry->is_occupied) return;
        if (next_entry->home_distance == 0) return;

        // NOTE: Shift the next entry back, and clear its old bucket in
        // case this is the last shift : we don't want any duplicate entries.
        *iter_entry = *next_entry;
        iter_entry->home_distance--;
        *next_entry = {};

        // NOTE: Continue to iterate forward.
        backshift_idx = (backshift_idx + 1) % HASHMAP_SIZE;
    }
}

*/
