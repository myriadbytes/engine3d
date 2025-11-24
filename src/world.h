#pragma once

#include <vulkan/vulkan.h>

#include "common.h"
#include "maths.h"
#include "gpu.h"

constexpr i32 CHUNK_W = 16;

constexpr v3i worldPosToChunk(v3 world_pos) {
    v3 chunk_pos = world_pos / CHUNK_W;

    return v3i {
        mfloor(chunk_pos.x()),
        mfloor(chunk_pos.y()),
        mfloor(chunk_pos.z()),
    };
}

constexpr v3 chunkToWorldPos(v3i chunk_pos) {
    return v3 {
        (f32)(chunk_pos.x() * CHUNK_W),
        (f32)(chunk_pos.y() * CHUNK_W),
        (f32)(chunk_pos.z() * CHUNK_W)
    };
}

// NOTE: How many chunks to load around the player, in ALL 3 directions.
// A radius of 1 would mean 7 chunks in a diamond pattern, the center one
// being the chunk the player is inside.
constexpr i32 LOAD_RADIUS = 8;

// NOTE: We'll allocate a pool of chunks at startup, so that there is no memory
// allocation for the chunk backing data at runtime. We can affort to do this
// because the amount of data low and constant for every chunk. On the other hand,
// vertex buffers (VRAM) are a very different story : the size is highly variable
// depending on the chunk's geometry and the worst case size (2MB) is much higher
// than the average (a few KB). Trying to startup-alloc enough VRAM for the loaded
// world with worst case buffer size would lead to ~10GB of VRAM usage. So I am using
// an actual memory allocator with a backing VRAM buffer of <1 GB. When we want to
// load a new chunk, we'll just get an unused one from the pool. VRAM for the
// vertex buffers will be allocated/deallocated/reallocated during the rendering
// loop I think, and we'll just tag the new / modified chunks as "needing remeshing".
// Even tho we're only supposed to load chunks in a sphere around the player, we'll
// allocate enough memory for the cube encapsulating this sphere, because it's easy
// to compute at compile time and gives us some (a lot actually) of leeway, for
// "always-loaded" special chunks for example. But a better upper bound should be
// found eventually.
constexpr usize CHUNK_POOL_SIZE = (LOAD_RADIUS * 2 + 1)
                                * (LOAD_RADIUS * 2 + 1)
                                * (LOAD_RADIUS * 2 + 1);

struct Chunk {
    b32 is_loaded;

    v3i chunk_position;
    u8 data[CHUNK_W * CHUNK_W * CHUNK_W];

    b32 needs_remeshing;
    usize vertices_count;

    AllocatedBuffer vertex_buffer;
};

// NOTE: The actual world will modeled using a hashmap that associates world
// coordinates to chunk handles. This is JUST FOR ACCESS/QUERY. No game world
// related memory is managed or owned by the hashmap. There are surely smarter
// and fancier ways to do this, but for now this allows easy chunk querying
// based on position.

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
constexpr usize WORLD_HASHMAP_SIZE = nextPowerOfTwo(CHUNK_POOL_SIZE);

// TODO: This can surely be optimized. We don't need a v3 (96 bits) to store the
// normal vector when we have only 6 different normal directions (3 bits) !
struct ChunkVertex {
    v3 position;
    v3 normal;
};

// NOTE: ChatGPT wrote that. I hope it's a good hash.
// It uses prime numbers so you know it must be.
constexpr usize chunkPositionHash(v3i chunk_position) {
    usize hash = 0;
    hash ^= (usize)(chunk_position.x() * 73856093);
    hash ^= (usize)(chunk_position.y() * 19349663);
    hash ^= (usize)(chunk_position.z() * 83492791);
    return hash;
}

// TODO: Look into switching to greedy meshing.
void generateNaiveChunkMesh(Chunk* chunk, ChunkVertex* out_vertices, usize* out_generated_vertex_count);
