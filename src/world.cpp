#include "world.h"

// TODO: It is pretty wasteful to have one upload buffer per chunk. There should
// just be a pool of n upload buffers that all chunk share, because you're never
// going to have to update the meshes from all the loaded chunks at once and even
// then you can just queue them.
// TODO: I am using CreateCommittedResource for the VRAM buffers but I think it's
// better to allocate a big heap and subdivide it yourself if I ever wanna optimize.
void allocateChunkGPUBuffers(ID3D12Device* d3d_device, Chunk* chunk) {

    // NOTE: Creates a CPU-side heap + buffer that can be used to transfer data to the GPU
    D3D12_HEAP_PROPERTIES upload_heap_props = {};
    upload_heap_props.Type = D3D12_HEAP_TYPE_UPLOAD; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
    upload_heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    upload_heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    upload_heap_props.CreationNodeMask = 1;
    upload_heap_props.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC upload_heap_resource_desc = {};
    upload_heap_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_heap_resource_desc.Alignment = 0;
    upload_heap_resource_desc.Width = WORST_CASE_CHUNK_VERTICES * sizeof(ChunkVertex); // This is the only field that really matters.
    upload_heap_resource_desc.Height = 1;
    upload_heap_resource_desc.DepthOrArraySize = 1;
    upload_heap_resource_desc.MipLevels = 1;
    upload_heap_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    upload_heap_resource_desc.SampleDesc.Count = 1;
    upload_heap_resource_desc.SampleDesc.Quality = 0;
    upload_heap_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_heap_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT upload_buffer_creation_res = d3d_device->CreateCommittedResource(
        &upload_heap_props,
        D3D12_HEAP_FLAG_NONE,
        &upload_heap_resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, // this is from the GPU's perspective
        NULL,
        IID_PPV_ARGS(&chunk->upload_buffer)
    );
    ASSERT(SUCCEEDED(upload_buffer_creation_res));

    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT; // DEFAULT = VRAM | UPLOAD / READBACK = RAM
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask = 1;
    heap_props.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC buffer_resource_desc = {};
    buffer_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_resource_desc.Alignment = 0;
    buffer_resource_desc.Width = WORST_CASE_CHUNK_VERTICES * sizeof(ChunkVertex);
    buffer_resource_desc.Height = 1;
    buffer_resource_desc.DepthOrArraySize = 1;
    buffer_resource_desc.MipLevels = 1;
    buffer_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    buffer_resource_desc.SampleDesc.Count = 1;
    buffer_resource_desc.SampleDesc.Quality = 0;
    buffer_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT buffer_creation_res = d3d_device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &buffer_resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        IID_PPV_ARGS(&chunk->vertex_buffer)
    );
    ASSERT(SUCCEEDED(buffer_creation_res));

    chunk->vbo_ready = false;
}

void initChunkMemoryPool(ChunkMemoryPool* pool, ID3D12Device* d3d_device) {
    // TODO: Zero out the memory ?
    pool->nb_used = 0;

    // NOTE: Fill the free slots stack with all the indices.
    for (int i = 0; i < CHUNK_POOL_SIZE; i++) {
        pool->free_slots[i] = CHUNK_POOL_SIZE - i - 1;
    }

    // NOTE: The first time we want to get a chunk, we'll read off from
    // the end of the free slots stack and decrement the pointer.
    pool->free_slots_stack_ptr = pool->free_slots + (CHUNK_POOL_SIZE - 1);

    // NOTE: Allocate VRAM and an upload buffer for each chunk.
    for (int i = 0; i < CHUNK_POOL_SIZE; i++) {
        allocateChunkGPUBuffers(d3d_device, &pool->slots[i]);
    }
}

Chunk* acquireChunkMemoryFromPool(ChunkMemoryPool* pool) {
    ASSERT(pool->free_slots_stack_ptr >= pool->free_slots);

    usize slot_idx = *pool->free_slots_stack_ptr;
    pool->free_slots_stack_ptr--;

    pool->nb_used++;
    return &pool->slots[slot_idx];
}

void releaseChunkMemoryToPool(ChunkMemoryPool* pool, Chunk* chunk) {
    usize slot_idx = (usize)(chunk - pool->slots);

    pool->free_slots_stack_ptr++;
    *pool->free_slots_stack_ptr = slot_idx;

    pool->nb_used--;
}

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
