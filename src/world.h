#pragma once

#include <d3d12.h>

#include "common.h"
#include "maths.h"

constexpr i32 CHUNK_W = 16;

inline v3i worldPosToChunk(v3 world_pos) {
    v3 chunk_pos = world_pos / CHUNK_W;

    return v3i {
        mfloor(chunk_pos.x),
        mfloor(chunk_pos.y),
        mfloor(chunk_pos.z),
    };
}

inline v3 chunkToWorldPos(v3i chunk_pos) {
    return v3 {
        (f32)(chunk_pos.x * CHUNK_W),
        (f32)(chunk_pos.y * CHUNK_W),
        (f32)(chunk_pos.z * CHUNK_W)
    };
}

// NOTE: How many chunks to load around the player. A radius of 1 would mean
// 5 chunks in a diamon pattern, the center one being the chunk the player is
// inside.
constexpr i32 LOAD_RADIUS = 4;

// NOTE: We'll allocate a pool of chunks at startup, so that there is no memory
// allocation / VRAM buffer creation at runtime. When we want to load a new chunk,
// we'll just get an unused one from the pool. Even tho we're only supposed to
// load chunks in a sphere around the player, we'll allocate enough memory for the
// cube encapsulating this sphere, because it's easy to compute at compile time
// and gives us some (a lot actually) of leeway. But a better upper bound needs
// to be computed eventually.
constexpr usize CHUNK_POOL_SIZE = (LOAD_RADIUS * 2 + 1)
                                * (LOAD_RADIUS * 2 + 1)
                                * (LOAD_RADIUS * 2 + 1);

struct Chunk {
    v3i chunk_position;
    u8 data[CHUNK_W * CHUNK_W * CHUNK_W];
    usize vertices_count;
    ID3D12Resource* upload_buffer;
    ID3D12Resource* vertex_buffer;
    b32 vbo_ready;
};

struct ChunkMemoryPool {
    Chunk slots[CHUNK_POOL_SIZE];
    usize free_slots[CHUNK_POOL_SIZE];
    usize* free_slots_stack_ptr;
};

void initChunkMemoryPool(ChunkMemoryPool* pool, ID3D12Device* d3d_device);
Chunk* acquireChunkMemoryFromPool(ChunkMemoryPool* pool);
void releaseChunkMemoryToPool(ChunkMemoryPool* pool, Chunk* chunk);

// NOTE: The actual world will modeled using a hashmap that associates world
// coordinates to chunk handles. There are surely smarter and fancier ways to
// do this, but for now this allows easy chunk querying based on position.
// FIXME: After enough insertion/deletions, we start to run out of EMPTY
// buckets because they all have been replaced by REUSABLE buckets. But we
// use EMPTY buckets as a stop condition in several hash map routines, so
// the more the hash map gets used the slower they become, and when there
// are no more EMPTY buckets we just get caught in an infinite loop. How
// come none of the hashmap tutorials I've seen or even LLMs mention that ?

enum WorldEntryState {
    WORLD_ENTRY_EMPTY,
    WORLD_ENTRY_OCCUPIED,
    WORLD_ENTRY_REUSABLE,
};

struct WorldEntry {
    v3i key;
    Chunk* chunk;
    WorldEntryState state;
};

// Ideally, we would want the max occupancy of the hash map to be 70%. Using the
// number of chunks we have in the pool is actually a good approximation, since
// that number is already higher than the number of chunks we'll have loaded at
// a time.
constexpr usize nextPowerOfTwo(usize n) {
    usize value = 2;
    while (value < n) {
        value *= 2;
    }
    return value;
}
constexpr usize HASHMAP_SIZE = nextPowerOfTwo(CHUNK_POOL_SIZE) * 2;

struct WorldHashMap {
    WorldEntry entries[HASHMAP_SIZE];
    usize nb_empty;
    usize nb_occupied;
    usize nb_reusable;
};

// NOTE: This is just a helper to iterate over loaded chunks
// so it only returns something for active entries.
Chunk* getChunkByIndex(WorldHashMap* world, usize idx);
b32 isChunkInWorld(WorldHashMap* world, v3i chunk_position);
void worldInsert(WorldHashMap* world, v3i chunk_position, Chunk* chunk);
void worldDelete(WorldHashMap* world, v3i chunk_position);

// TODO: This can surely be optimized. We don't need a v3 (96 bits) to store the
// normal vector when we have only 6 different normal directions (3 bits) !
struct ChunkVertex {
    v3 position;
    v3 normal;
};

// NOTE: This is the worst case number of vertices that would be needed with our
// current meshing strategy for a chunk mesh (with a "checkerboard" chunk). This
// stops being true once we add transparent blocks. Too bad !
constexpr usize WORST_CASE_CHUNK_VERTICES = ((CHUNK_W * CHUNK_W * CHUNK_W) / 2) * 6 * 2 * 3;
