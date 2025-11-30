#pragma once

#include "common.h"

// HASHMAP

template <typename V, typename K>
struct HashmapEntry {
    usize hash;

    K key;
    V value;

    b32 is_occupied;
    usize home_distance;
};

template <typename V, typename K, usize N>
struct Hashmap {
    HashmapEntry<V, K> entries[N];
    usize (*hashingFunction)(K);
    usize nb_occupied;
};

template <typename V, typename K, usize N>
void hashmapInitialize(Hashmap<V, K, N>* hashmap, usize (*hashingFunction)(K)) {
    hashmap->hashingFunction = hashingFunction;
}

template <typename V, typename K, usize N>
void hashmapInsert(Hashmap<V, K, N>* hashmap, K key, V value) {
    // NOTE: The strategy used here is called "robin-hood" hashing.

    ASSERT(!hashmapContains(hashmap, key));

    usize hash = hashmap->hashingFunction(key);
    usize idx = hash % N;

    HashmapEntry<V, K> to_insert_entry = {};
    to_insert_entry.hash = hash;
    to_insert_entry.key = key;
    to_insert_entry.value = value;
    to_insert_entry.is_occupied = true;
    to_insert_entry.home_distance = 0;

    while (true) {
        HashmapEntry<V, K>* iter_entry = &hashmap->entries[idx];

        // NOTE: We found an empty slot. Insert the entry to be inserted.
        if (!iter_entry->is_occupied) {
            *iter_entry = to_insert_entry;
            hashmap->nb_occupied++;
            break;
        }

        // NOTE: We found an entry that is closer to home than the
        // entry to be inserted. The new entry gets that spot,
        // and the entry to be inserted is now the entry that was
        // at that spot.
        if (iter_entry->home_distance < to_insert_entry.home_distance) {
            HashmapEntry<V, K> tmp = *iter_entry;
            *iter_entry = to_insert_entry;
            to_insert_entry = tmp;
        }

        // NOTE: Move forward in the buckets, looping around if necessary.
        idx = (idx + 1) % N;
        to_insert_entry.home_distance++;
    }
}

template <typename V, typename K, usize N>
void hashmapRemove(Hashmap<V, K, N>* hashmap, K key) {
    // NOTE: The strategy used here is called "backward-shift deletion" and
    // is common with robin-hood hashing.

    usize hash = hashmap->hashingFunction(key);
    usize idx = hash % N;
    u32 lookup_home_dist = 0;

    // NOTE: Find the entry to delete.
    while (true) {
        HashmapEntry<V, K>* iter_entry = &hashmap->entries[idx];

        if ((!iter_entry->is_occupied) || (lookup_home_dist > iter_entry->home_distance)) {
            // NOTE: I think that trying to delete a value that is not inside the
            // hashmap indicates a logic error in some other code.
            ASSERT(false);
        };

        // NOTE: WE found the it ! Yay !
        if (iter_entry->key == key) {
            break;
        }

        // NOTE: Keep looking forward.
        idx = (idx + 1) % N;
        lookup_home_dist++;
    }

    // NOTE: Delete the entry.
    hashmap->entries[idx] = {};
    hashmap->nb_occupied--;

    // NOTE: Shift all subsequent entries to prevent any hole from forming
    // and to bring the entries closer to their "home" bucket.
    usize backshift_idx = idx;
    while (true) {
        HashmapEntry<V, K>* iter_entry = &hashmap->entries[backshift_idx];
        HashmapEntry<V, K>* next_entry = &hashmap->entries[(backshift_idx + 1) % N];

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
        backshift_idx = (backshift_idx + 1) % N;
    }
}

template <typename V, typename K, usize N>
b32 hashmapContains(Hashmap<V, K, N>* hashmap, K key) {
    usize hash = hashmap->hashingFunction(key);
    usize idx = hash % N;
    u32 lookup_home_dist = 0;

    while (true) {
        HashmapEntry<V, K>* iter_entry = &hashmap->entries[idx];

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
        if (iter_entry->key == key) {
            return true;
        }

        // NOTE: Keep looking forward.
        idx = (idx + 1) % N;
        lookup_home_dist++;
    }
   
    return false;
}

template <typename V, typename K, usize N>
V hashmapGet(Hashmap<V, K, N>* hashmap, K key) {
    usize hash = hashmap->hashingFunction(key);
    usize idx = hash % N;
    u32 lookup_home_dist = 0;

    V default_value = {};

    while (true) {
        HashmapEntry<V, K>* iter_entry = &hashmap->entries[idx];

        // NOTE: We have iterated on the whole cluster, arrived at
        // an empty cell, and didn't find the entry.
        if (!iter_entry->is_occupied) {
            return default_value;
        };

        // NOTE: A nice property of Robin-Hood hashing is that we
        // know that a possible new entry would have displaced others
        // if its home distance gets greater. So if we arrive at this
        // case we know that the entry was never inserted.
        if (lookup_home_dist > iter_entry->home_distance) {
            return default_value;
        };

        // NOTE: WE found the it ! Yay !
        if (iter_entry->key == key) {
            return iter_entry->value;
        }

        // NOTE: Keep looking forward.
        idx = (idx + 1) % N;
        lookup_home_dist++;
    }
   
    return default_value;
}
