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

    return &pool->slots[slot_idx];
}

void releaseChunkMemoryToPool(ChunkMemoryPool* pool, Chunk* chunk) {
    usize slot_idx = (usize)(chunk - pool->slots);

    pool->free_slots_stack_ptr++;
    *pool->free_slots_stack_ptr = slot_idx;
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

    if (world->entries[idx].state == WORLD_ENTRY_OCCUPIED) {
        ASSERT(world->entries[idx].key == world->entries[idx].chunk->chunk_position);
        return world->entries[idx].chunk;
    } else {
        return NULL;
    }
}

b32 isChunkInWorld(WorldHashMap* world, v3i chunk_position) {
    usize hash = chunkPositionHash(chunk_position);
    usize idx = hash % HASHMAP_SIZE;

    while (world->entries[idx].state != WORLD_ENTRY_EMPTY) {
        if (world->entries[idx].state == WORLD_ENTRY_OCCUPIED
            && world->entries[idx].key == chunk_position) {
            return true;
        }

        idx = (idx + 1) % HASHMAP_SIZE;
    }
    return false;
}

// TODO: Track hashmap utilization using a count variable and a ++ on insertion
// and a -- on deletion.
void worldInsert(WorldHashMap* world, v3i chunk_position, Chunk* chunk) {
    ASSERT(!isChunkInWorld(world, chunk_position));

    usize hash = chunkPositionHash(chunk_position);
    usize idx = hash % HASHMAP_SIZE;

    // NOTE: If we found a previously deleted bucket during the probing,
    // it's better for performance to reuse it instead of taking the empty
    // bucket at the end. That way, we prevent the clusters from getting too
    // long too quickly.
    b32 found_reusable = false;
    usize reusable_idx = 0;

    while (world->entries[idx].state != WORLD_ENTRY_EMPTY) {

        // NOTE: I don't know what should happen if I try to insert a chunk
        // at a position where a chunk is already loaded. Surely that sounds
        // like an error ?
        if (world->entries[idx].state == WORLD_ENTRY_OCCUPIED
            && world->entries[idx].key == chunk_position) {
            ASSERT(false);
        }
        // NOTE: Found a possible bucket for the new chunk handle.
        else if (world->entries[idx].state == WORLD_ENTRY_REUSABLE
            && !found_reusable) {
            found_reusable = true;
            reusable_idx = idx;
        }

        idx = (idx + 1) % HASHMAP_SIZE;
    }

    if (found_reusable) idx = reusable_idx;

    if (!found_reusable) {
        world->nb_empty--;
    } else {
        world->nb_reusable--;
    }
    world->nb_occupied++;

    world->entries[idx].key = chunk_position;
    world->entries[idx].chunk = chunk;
    world->entries[idx].state = WORLD_ENTRY_OCCUPIED;
}

void worldDelete(WorldHashMap* world, v3i chunk_position) {
    
    usize hash = chunkPositionHash(chunk_position);
    usize idx = hash % HASHMAP_SIZE;

    while (world->entries[idx].state != WORLD_ENTRY_EMPTY) {

        if (world->entries[idx].state == WORLD_ENTRY_OCCUPIED
            && world->entries[idx].key == chunk_position) {
            world->nb_occupied--;
            world->nb_reusable++;
            world->entries[idx].state = WORLD_ENTRY_REUSABLE;
            return;
        }

        idx = (idx + 1) % HASHMAP_SIZE;
    }

    // NOTE: We didn't find a chunk in the hashmap with this position.
    // I think this should be an error if I try to unload a chunk that's
    // not loaded.
    ASSERT(false);
}
