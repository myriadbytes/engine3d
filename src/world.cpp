#include "world.h"

// TODO: Many duplicate vertices. Is it easy/possible to use indices here ?
// TODO: Currently the chunk doesn't look into neighboring chunks. This means there are generated
// triangles between solid blocks on two different chunks.
void generateNaiveChunkMesh(Chunk* chunk, ChunkVertex* out_vertices, usize* out_generated_vertex_count) {
    usize emitted = 0;
    for(usize i = 0; i < CHUNK_W * CHUNK_W * CHUNK_W; i++){

        usize x = (i % CHUNK_W);
        usize y = (i / CHUNK_W) % (CHUNK_W);
        usize z = (i / (CHUNK_W * CHUNK_W));

        if (!chunk->data[i]) continue;

        b32 empty_pos_x = true;
        b32 empty_neg_x = true;
        b32 empty_pos_y = true;
        b32 empty_neg_y = true;
        b32 empty_pos_z = true;
        b32 empty_neg_z = true;

        if (x < (CHUNK_W - 1)) {
            empty_pos_x = !chunk->data[i + 1];
        }
        if (x > 0) {
            empty_neg_x = !chunk->data[i - 1];
        }

        if (y < (CHUNK_W - 1)) {
            empty_pos_y = !chunk->data[i + CHUNK_W];
        }
        if (y > 0) {
            empty_neg_y = !chunk->data[i - CHUNK_W];
        }

        if (z < (CHUNK_W - 1)) {
            empty_pos_z = !chunk->data[i + CHUNK_W * CHUNK_W];
        }
        if (z > 0) {
            empty_neg_z = !chunk->data[i - CHUNK_W * CHUNK_W];
        }

        v3 position = {(f32)x, (f32)y, (f32)z};

        if (empty_pos_x) {
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {1, 0, 0}};
        }

        if (empty_neg_x) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {-1, 0, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {-1, 0, 0}};
        }

        if (empty_pos_y) {
            out_vertices[emitted++] = {position + v3 {0, 1, 0}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};

            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 1, 0}, v3 {0, 1, 0}};
        }

        if (empty_neg_y) {
            out_vertices[emitted++] = {position + v3 {0, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

            out_vertices[emitted++] = {position + v3 {1, 0, 0}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, -1, 0}};
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, -1, 0}};

        }

        if (empty_pos_z) {
            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};

            out_vertices[emitted++] = {position + v3 {0, 0, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {1, 1, 1}, v3 {0, 0, 1}};
            out_vertices[emitted++] = {position + v3 {0, 1, 1}, v3 {0, 0, 1}};
        }

        if (empty_neg_z) {
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
