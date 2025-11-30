#include "world.h"

// TODO: Many duplicate vertices. Is it easy/possible to use indices here ?
// TODO: Currently the chunk doesn't look into neighboring chunks. This means there are generated
// triangles between solid blocks on two different chunks.
void generateNaiveChunkMesh(Hashmap<Chunk*, v3i, WORLD_HASHMAP_SIZE>* world_hashmap, Chunk* chunk, ChunkVertex* out_vertices, usize* out_generated_vertex_count) {
    usize emitted = 0;
    for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){

        usize x = (i % CHUNK_W);
        usize y = (i / CHUNK_W) % (CHUNK_W);
        usize z = (i / (CHUNK_W * CHUNK_W));

        if (!chunk->data[i]) continue;

        b32 create_face_pos_x = false;
        b32 create_face_neg_x = false;
        b32 create_face_pos_y = false;
        b32 create_face_neg_y = false;
        b32 create_face_pos_z = false;
        b32 create_face_neg_z = false;

        if (x < (CHUNK_W - 1)) {
            create_face_pos_x = !chunk->data[i + 1];
        } else {
            Chunk* neighbor = hashmapGet(world_hashmap, chunk->chunk_position + v3i {1, 0, 0});
            if (neighbor) {
                create_face_pos_x = !neighbor->data[y * CHUNK_W + z * CHUNK_W * CHUNK_W];
            }
        }

        if (x > 0) {
            create_face_neg_x = !chunk->data[i - 1];
        } else {
            Chunk* neighbor = hashmapGet(world_hashmap, chunk->chunk_position - v3i {1, 0, 0});
            if (neighbor) {
                create_face_neg_x = !neighbor->data[(CHUNK_W - 1) + y * CHUNK_W + z * CHUNK_W * CHUNK_W];
            }
        }

        if (y < (CHUNK_W - 1)) {
            create_face_pos_y = !chunk->data[i + CHUNK_W];
        } else {
            Chunk* neighbor = hashmapGet(world_hashmap, chunk->chunk_position + v3i {0, 1, 0});
            if (neighbor) {
                create_face_pos_y = !neighbor->data[x + z * CHUNK_W * CHUNK_W];
            }
        }

        if (y > 0) {
            create_face_neg_y = !chunk->data[i - CHUNK_W];
        } else {
            Chunk* neighbor = hashmapGet(world_hashmap, chunk->chunk_position - v3i {0, 1, 0});
            if (neighbor) {
                create_face_neg_y = !neighbor->data[x + (CHUNK_W - 1) * CHUNK_W + z * CHUNK_W * CHUNK_W];
            }
        }

        if (z < (CHUNK_W - 1)) {
            create_face_pos_z = !chunk->data[i + CHUNK_W * CHUNK_W];
        } else {
            Chunk* neighbor = hashmapGet(world_hashmap, chunk->chunk_position + v3i {0, 0, 1});
            if (neighbor) {
                create_face_pos_z = !neighbor->data[x + y * CHUNK_W];
            }
        }

        if (z > 0) {
            create_face_neg_z = !chunk->data[i - CHUNK_W * CHUNK_W];
        } else {
            Chunk* neighbor = hashmapGet(world_hashmap, chunk->chunk_position - v3i {0, 0, 1});
            if (neighbor) {
                create_face_neg_z = !neighbor->data[x + y * CHUNK_W + (CHUNK_W - 1) * CHUNK_W * CHUNK_W];
            }
        }

        v3 position = {(f32)x, (f32)y, (f32)z};

        if (create_face_pos_x) {
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};
        }

        if (create_face_neg_x) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {-1, 0, 0}};
        }

        if (create_face_pos_y) {
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};
        }

        if (create_face_neg_y) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

        }

        if (create_face_pos_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 0, 1}};
        }

        if (create_face_neg_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, 0, -1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 0, -1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 0, -1}};
        }
    }

    *out_generated_vertex_count = emitted;
}
