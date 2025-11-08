#include "img.h"
#include <Windows.h>
#include <strsafe.h>

#define packed __attribute__((packed))

// TODO: Use compiler built-ins or instrinsics.
u32 endian_swap(u32 x) {
    u8 b0 = (x >> 0) & 0xFF;
    u8 b1 = (x >> 8) & 0xFF;
    u8 b2 = (x >> 16) & 0xFF;
    u8 b3 = (x >> 24) & 0xFF;

    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

// NOTE: There are faster ways to do this, but it doesn't matter since
// this is constexpr.
constexpr u32 four_cc(const char* str) {
    return (str[3] << 24) | (str[2] << 16) | (str[1] << 8) | str[0];
}

constexpr u32 PNG_HEADER_SIZE = 8;
constexpr u8 PNG_HEADER[PNG_HEADER_SIZE] = {137, 80, 78, 71, 13, 10, 26, 10};

struct packed PngChunkHeader {
    u32 length;
    // NOTE: The PNG type is 4 bytes long, and can be
    // interpreted as an ASCII string (e.g. "IHDR"),
    // but for comparison it's easier to just have it
    // as a number.
    union {
        u32 type_nb;
        char type_chars[4];
    };
};

struct packed PngChunkFooter {
    u32 crc;
};

struct packed IhdrChunk {
    u32 width;
    u32 height;
    u8 bit_depth;
    u8 color_type;
    u8 compression;
    u8 filter;
    u8 interlace;
};

struct DeflateBuffer {
    u8* contents;
    u32 contents_size;

    u32 bit_buffer;
    u32 bit_count;
};

void pull_next_byte_into_bitbuffer(DeflateBuffer* buffer) {
    ASSERT(buffer->contents_size > 0);
    // NOTE: This is especially important : we need to check that
    // we have more than 8 "free bits" in our bitbuffer or else we
    // are gonna drop bits and corrupt the rest of the stream.
    // Logically this cannot happen when the bitbuffer is 32 bits and
    // the max bits read at a time is 16, but this was a nightmare to
    // debug when I tried to reduce the bitbuffer to a u16 so now there
    // is an assert in case I ever try to be too smart again.
    ASSERT((sizeof(buffer->bit_buffer) * 8) - buffer->bit_count >= 8);

    u8 next_byte = *buffer->contents;
    buffer->contents++;
    buffer->contents_size--;

    buffer->bit_buffer |= ((u32)next_byte << buffer->bit_count);
    buffer->bit_count += 8;
}

u16 consume_bits(DeflateBuffer* buffer, u16 nb_bits_to_read) {
    ASSERT(nb_bits_to_read < 16);
    ASSERT(nb_bits_to_read > 0);

    u16 result = {};

    while (buffer->bit_count < nb_bits_to_read) {
        pull_next_byte_into_bitbuffer(buffer);
    }

    if (buffer->bit_count >= nb_bits_to_read) {
        // NOTE: If the user wants to read n bits, shifting 1 n places to the left and
        // subtracting 1 will give us the mask we want. Example with trying to read 5 bits :
        // 1 << 5       = 0010 0000
        // (1 << 5) - 1 = 0001 1111
        result = buffer->bit_buffer & ((1 << nb_bits_to_read) - 1);

        buffer->bit_count -= nb_bits_to_read;
        buffer->bit_buffer >>= nb_bits_to_read;
    }

    return result;
}

u16 peek_bits(DeflateBuffer* buffer, u16 nb_bits_to_peek) {
    ASSERT(nb_bits_to_peek < 16);
    ASSERT(nb_bits_to_peek > 0);

    u16 result = {};

    while (buffer->bit_count < nb_bits_to_peek) {
        pull_next_byte_into_bitbuffer(buffer);
    }

    if (buffer->bit_count >= nb_bits_to_peek) {
        // NOTE: If the user wants to read n bits, shifting 1 n places to the left and
        // subtracting 1 will give us the mask we want. Example with trying to read 5 bits :
        // 1 << 5       = 0010 0000
        // (1 << 5) - 1 = 0001 1111
        result = buffer->bit_buffer & ((1 << nb_bits_to_peek) - 1);
    }

    return result;
}

void discard_bits(DeflateBuffer* buffer, u16 nb_bits_to_discard) {
    ASSERT(nb_bits_to_discard < 16);
    ASSERT(nb_bits_to_discard > 0);

    while (buffer->bit_count < nb_bits_to_discard) {
        pull_next_byte_into_bitbuffer(buffer);
    }

    if (buffer->bit_count >= nb_bits_to_discard) {
        buffer->bit_count -= nb_bits_to_discard;
        buffer->bit_buffer >>= nb_bits_to_discard;
    }
}

void flush_byte(DeflateBuffer* buffer) {
    buffer->bit_buffer = {};
    buffer->bit_count = 0;
}

constexpr u32 HUFFMAN_LUT_BITS = 9;
constexpr u32 HUFFMAN_TABLE_SIZE = (1 << HUFFMAN_LUT_BITS);

struct HuffmanEntry {
    u16 symbol;
    u8 length;

    b8 is_subtable;
    HuffmanEntry* subtable;
};

HuffmanEntry* compute_huffman_table(u16* code_lengths, u32 code_lengths_count, Arena* table_arena) {
    constexpr u32 MAX_BITS = 16;

    HuffmanEntry* table = (HuffmanEntry*)pushZeros(table_arena, HUFFMAN_TABLE_SIZE * sizeof(HuffmanEntry));

    // NOTE: This algorithm for converting symbol code lengths
    // to the actual codes is well documented in the DEFLATE spec.
    u32 lengths_histogram[MAX_BITS] = {};
    for (int i = 0; i < code_lengths_count; i++) {
        lengths_histogram[code_lengths[i]]++;
    }
    lengths_histogram[0] = 0;

    // NOTE: Basically we construct a table that has the first code
    // for each code lengths. Then when we assign code to each symbol
    // we just read the code based on the symbol code length and then
    // increment it.
    u16 next_code[MAX_BITS] = {};
    {
        u16 code = 0;
        for (int bits = 1; bits < MAX_BITS; bits++) {
            code = (code + lengths_histogram[bits-1]) << 1;
            next_code[bits] = code;
        }
    }

    // NOTE: This is a little messy because it is much easier to fill in the
    // table at the same time as we are finding out which code is associated
    // to each symbol. But basically, for each symbol we wanna represent,
    // we find out which code is used for that symbol and then fill all the
    // entries in the look-up table that begin with this code.
    for (u16 symbol = 0; symbol < code_lengths_count; symbol++) {
        u16 symbol_code_len = code_lengths[symbol];

        // NOTE: Check that the code for our symbol is less
        // than 9 bits long. If not, we need to implement
        // subtable creation :(
        ASSERT(symbol_code_len <= HUFFMAN_LUT_BITS);

        if (symbol_code_len != 0) {
            u16 symbol_code = next_code[symbol_code_len];
            next_code[symbol_code_len]++;

            // TRICKY: The bit order for Huffman codes read from the stream is different
            // from everything else... So we need to bit-reverse the codes.
            // If symbol A has code 1100, you would need to read 1 -> 1 -> 0 -> 0 to
            // "match it", but an A is encoded in the stream as 0011. I guess this is because
            // the spec assumes that we are going to read from the stream of bytes by
            // right shifting ?
            u16 reversed_code = 0;
            for (u8 bit_idx = 0; bit_idx < symbol_code_len; bit_idx++) {
                reversed_code = (reversed_code << 1) | (symbol_code & 1);
                symbol_code >>= 1;
            }

            // NOTE: Once we have our bit-reversed code, we construct a look-up table indexed
            // by 9-bit patterns (that's 512 entries, a nice number). During decoding, we simply
            // peek the next 9 bits of the stream and index into the table with them. For our A
            // symbol example, all patterns that look like xxxxx0011 will point to an entry that
            // tells our decoder to output an A symbol, and discard 4 bits (the length of the code).
            // Codes longer than 9 bits would just point to another table, but they're not supported
            // for now.
            u16 max = 1 << (HUFFMAN_LUT_BITS - symbol_code_len);
            for (u16 fill = 0; fill < max; fill++) {
                u16 key = reversed_code | (fill << symbol_code_len);

                ASSERT(key <= ((1 << HUFFMAN_LUT_BITS) - 1));

                table[key].is_subtable = false;
                table[key].length = symbol_code_len;
                table[key].symbol = symbol;
            }
        }
    }

    return table;
}

u16 decode_next_symbol(DeflateBuffer* buffer, HuffmanEntry* table) {
    u16 key = peek_bits(buffer, 9);

    HuffmanEntry* entry = table + key;
    ASSERT(!entry->is_subtable);
    ASSERT(entry->length > 0);
    u16 result = entry->symbol;
    discard_bits(buffer, entry->length);

    return result;
}

// NOTE: Fills the lengths array with nb_lengths_to_decode elements decoded
// from the stream, by using the special encoding the the spec uses for the
// literal and distance code lengths.
void decode_lengths(DeflateBuffer* buffer, HuffmanEntry* table, u16* lengths, u32 nb_lengths_to_decode) {
    // SPEC: The alphabet for code lengths is as follows:

    // 0 - 15: Represent code lengths of 0 - 15
    //    16: Copy the previous code length 3 - 6 times.
    //        The next 2 bits indicate repeat length
    //              (0 = 3, ... , 3 = 6)
    //           Example:  Codes 8, 16 (+2 bits 11),
    //                     16 (+2 bits 10) will expand to
    //                     12 code lengths of 8 (1 + 6 + 5)
    //    17: Repeat a code length of 0 for 3 - 10 times.
    //        (3 bits of length)
    //    18: Repeat a code length of 0 for 11 - 138 times
    //        (7 bits of length)

    for (u32 idx = 0; idx < nb_lengths_to_decode;) {
        u16 len = decode_next_symbol(buffer, table);

        if (len < 16) {
            lengths[idx] = len;
            idx++;
        }
        else if (len == 16) {
            u16 repeats = 3 + consume_bits(buffer, 2);
            u16 to_repeat = lengths[idx - 1];

            for (u8 i = 0; i < repeats; i++) {
                lengths[idx] = to_repeat;
                idx++;
            }
        }
        else if (len == 17) {
            u16 repeats = 3 + consume_bits(buffer, 3);

            for (u8 i = 0; i < repeats; i++) {
                lengths[idx] = 0;
                idx++;
            }
        }
        else if (len == 18) {
            u16 repeats = 11 + consume_bits(buffer, 7);

            for (u8 i = 0; i < repeats; i++) {
                lengths[idx] = 0;
                idx++;
            }
        }
    }
}

// SPEC:
//           Extra               Extra               Extra
//      Code Bits Length(s) Code Bits Lengths   Code Bits Length(s)
//      ---- ---- ------     ---- ---- -------   ---- ---- -------
//       257   0     3       267   1   15,16     277   4   67-82
//       258   0     4       268   1   17,18     278   4   83-98
//       259   0     5       269   2   19-22     279   4   99-114
//       260   0     6       270   2   23-26     280   4  115-130
//       261   0     7       271   2   27-30     281   5  131-162
//       262   0     8       272   2   31-34     282   5  163-194
//       263   0     9       273   3   35-42     283   5  195-226
//       264   0    10       274   3   43-50     284   5  227-257
//       265   1  11,12      275   3   51-58     285   0    258
//       266   1  13,14      276   3   59-66
constexpr u16 SPEC_LEN_TABLE[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19,
    23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
constexpr u16 SPEC_LEN_EXTRA_BITS_TABLE[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0
};
static_assert(ARRAY_COUNT(SPEC_LEN_TABLE) == ARRAY_COUNT(SPEC_LEN_EXTRA_BITS_TABLE));

// SPEC:
//            Extra           Extra               Extra
//       Code Bits Dist  Code Bits   Dist     Code Bits Distance
//       ---- ---- ----  ---- ----  ------    ---- ---- --------
//         0   0    1     10   4     33-48    20    9   1025-1536
//         1   0    2     11   4     49-64    21    9   1537-2048
//         2   0    3     12   5     65-96    22   10   2049-3072
//         3   0    4     13   5     97-128   23   10   3073-4096
//         4   1   5,6    14   6    129-192   24   11   4097-6144
//         5   1   7,8    15   6    193-256   25   11   6145-8192
//         6   2   9-12   16   7    257-384   26   12  8193-12288
//         7   2  13-16   17   7    385-512   27   12 12289-16384
//         8   3  17-24   18   8    513-768   28   13 16385-24576
//         9   3  25-32   19   8   769-1024   29   13 24577-32768
constexpr u16 SPEC_DIST_TABLE[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257,
    385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
    16385, 24577
};
constexpr u16 SPEC_DIST_EXTRA_BITS_TABLE[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
    10, 10, 11, 11, 12, 12, 13, 13
};
static_assert(ARRAY_COUNT(SPEC_DIST_TABLE) == ARRAY_COUNT(SPEC_DIST_EXTRA_BITS_TABLE));

// NOTE: https://www.w3.org/TR/2003/REC-PNG-20031110/
u8* read_image(const char* path, u32* w, u32* h, Arena* return_arena, Arena* scratch) {
    HANDLE file_handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file_handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    // TODO: We are currently ignoring the high bytes of
    // the size for VERY large files (> 4GB).
    DWORD file_size_high = 0;
    DWORD file_size = GetFileSize(file_handle, &file_size_high);
    ASSERT(file_size_high == 0);

    u8* png_bytes = (u8*)pushBytes(scratch, file_size);
    DWORD bytes_read;
    ReadFile(file_handle, (void*)png_bytes, file_size, &bytes_read, NULL);
    ASSERT(bytes_read == file_size);
    CloseHandle(file_handle);

    // NOTE: A png file always starts with a fixed header, so we
    // check for it.
    for (u32 i = 0; i < PNG_HEADER_SIZE; i++) {
        if (png_bytes[i] != PNG_HEADER[i])
            return NULL;
    }

    // NOTE: After the header, the rest of the file is made up of chunks of
    // different types. The ones we are interested in are IHDR (for general
    // image info) and IDAT (containing the compressed image data).
    // Large PNGs can contain multiple IDAT chunks that then have to be
    // concatenated, but since the file I am interested in reader for now
    // only have one, we are just going to error out if we encounter more than 1.

    char printf_buffer[512];

    b32 ihdr_found = false;
    b32 idat_found = false;

    u8* idat_contents;
    u32 idat_contents_size;

    u32 image_width;
    u32 image_height;

    OutputDebugStringA("PNG Chunks:\n");

    // NOTE: Loop through all chunks starting after the header.
    u32 at = PNG_HEADER_SIZE;
    while (at < file_size) {

        // NOTE: A chunk begins with a header containing length and type.
        // WARNING: This is technically UB. Language lawyers are gonna put me in jail.
        PngChunkHeader* chunk_header = (PngChunkHeader*)(&png_bytes[at]);
        u32 chunk_contents_size = endian_swap(chunk_header->length);
        at += sizeof(PngChunkHeader);

        StringCbPrintfA(printf_buffer, 256, "    %.4s (%u bytes)\n", chunk_header->type_chars, chunk_contents_size);
        OutputDebugStringA(printf_buffer);

        switch(chunk_header->type_nb) {
            case four_cc("IHDR"): {
                ihdr_found = true;

                IhdrChunk* idhr = (IhdrChunk*)(&png_bytes[at]);
                image_width = endian_swap(idhr->width);
                image_height = endian_swap(idhr->height);

                if (idhr->bit_depth != 8) {
                    // NOTE: We only support 8bbp !
                    return NULL;
                }
                if (idhr->color_type != 6) {
                    // NOTE: We only support RGBA !
                    return NULL;
                }
                if (idhr->compression != 0) {
                    // NOTE: This is a non-standard PNG.
                    return NULL;
                }
                if (idhr->filter != 0) {
                    // NOTE: This is a non-standard PNG.
                    return NULL;
                }
                if (idhr->interlace != 0) {
                    // NOTE: We only support non-interlaced PNGs !
                    return NULL;
                }
            } break;
            case four_cc("IDAT"): {
                // NOTE: We only support a single data chunk for now.
                if (idat_found) return NULL;

                idat_found = true;
                idat_contents = &png_bytes[at];
                idat_contents_size = chunk_contents_size;
            } break;
        }

        // NOTE: Skip over the contents, read the footer, and set the cursor
        // ready to read the next chunk.
        at += chunk_contents_size;
        PngChunkFooter* footer = (PngChunkFooter*)&png_bytes[at];
        (void) footer;
        at += sizeof(PngChunkFooter);
    }

    // NOTE: A PNG with no contents would be weird but it's better to check.
    if (!ihdr_found || !idat_found) return NULL;

    // NOTE: The IDAT chunk contains the compressed image data, in the form
    // of a "ZLib Stream" (https://datatracker.ietf.org/doc/html/rfc1950).
    // It has two bytes of info at the start, then the actual data in DEFLATE
    // format, then a 4 bytes checksum at the end.

    u8 CMF = idat_contents[0];
    u8 FLG = idat_contents[1];

    u8 compression_method = CMF & 0xF;
    u8 compression_info = CMF >> 4;
    USED(compression_info);

    u8 fcheck = FLG & 0x1F;
    u8 fdict = (FLG >> 5) & 1;
    u8 flevel = FLG >> 6;
    USED(fcheck);
    USED(flevel);

    // NOTE: Compression method has to be DEFLATE for PNG.
    if (compression_method != 8) {
        return NULL;
    }

    OutputDebugStringA("Zlib Stream:\n");
    StringCbPrintfA(printf_buffer, 512, "    Compression Method:%d\n", (int)compression_method);
    OutputDebugStringA(printf_buffer);
    StringCbPrintfA(printf_buffer, 512, "    FDict:%d\n", (int)fdict);
    OutputDebugStringA(printf_buffer);

    // NOTE: If fdict is set, then there are more bytes to read after those two,
    // but that's a problem for later.
    if (fdict) return NULL;

    // NOTE: After the two bytes of ZLib info, and the eventual fdict stuff, the rest
    // of the contents (except the 4 bytes checksum at the end) is DEFLATE-encoded.
    // See the spec: https://www.ietf.org/rfc/rfc1951.txt
    DeflateBuffer deflate_buffer = {};
    deflate_buffer.contents = &idat_contents[2];
    deflate_buffer.contents_size = idat_contents_size - 2 - 4;

    // NOTE: The decoded IDAT chunk is not the final image, since the image is filtered
    // before compression and that filtering needs to be reversed. The output stream is
    // thus slightly larger than the final image, because of the filter info bytes.
    u32 output_stream_size = image_height * (1 + image_width * 4);
    u8* output_stream = (u8*)pushBytes(scratch, output_stream_size);

    // NOTE: Each DEFLATE "block" starts with a 3-bit header that indicates
    // what to do.
    u32 bfinal = 0;
    while (bfinal == 0) {
        bfinal = consume_bits(&deflate_buffer, 1);
        // NOTE: We only support one block currently.
        // Supporting more is not much harder but the bitmap
        // I am interested in loading only has one so... eh.
        ASSERT(bfinal == 1);

        u32 btype = consume_bits(&deflate_buffer, 2);
        switch (btype) {
            case 0b00: {
                // TODO: No compression.
                return NULL;
            } break;
            case 0b01: {
                // TODO: Compressed with fixed Huffman codes.
                return NULL;
            } break;
            case 0b10: {
                // NOTE: Compressed with dynamic Huffman codes.

                // TRICKY: DEFLATE is weird in that the Huffman codes is specified
                // with code lengths that are themselves compressed using Huffman
                // codes... So HLIT is the number of code lengths for the data and
                // HCLEN is the number of code lengths to decode these data code lengths.
                // Yeah... This is not confusing at all...
                u32 HLIT = consume_bits(&deflate_buffer, 5);  // # of Literal/Length codes - 257
                u32 HDIST = consume_bits(&deflate_buffer, 5); // # of Distance codes - 1
                u32 HCLEN = consume_bits(&deflate_buffer, 4); // # of Code Length codes - 4

                constexpr u32 hclen_swizzle[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                u16 meta_huffman_lengths[ARRAY_COUNT(hclen_swizzle)] = {};

                // SPEC: (HCLEN + 4) x 3 bits: code lengths for the code length
                // alphabet given just above, in the order: 16, 17, 18, [...]
                for (u32 idx = 0; idx < HCLEN + 4; idx++) {
                    meta_huffman_lengths[hclen_swizzle[idx]] = consume_bits(&deflate_buffer, 3);
                }

                HuffmanEntry* meta_huffman_table = compute_huffman_table(
                    meta_huffman_lengths,
                    ARRAY_COUNT(meta_huffman_lengths),
                    scratch
                );

                // SPEC:  HLIT + 257 code lengths for the literal/length alphabet,
                // encoded using the code length Huffman code
                u16* literal_lengths = (u16*)pushBytes(scratch, (HLIT + 257) * sizeof(u16));
                decode_lengths(&deflate_buffer, meta_huffman_table, literal_lengths, HLIT + 257);

                // SPEC: HDIST + 1 code lengths for the distance alphabet,
                // encoded using the code length Huffman code
                u16* distance_lengths = (u16*)pushBytes(scratch, (HDIST + 1) * sizeof(u16));
                decode_lengths(&deflate_buffer, meta_huffman_table, distance_lengths, HDIST+1);

                HuffmanEntry* literal_table = compute_huffman_table(literal_lengths, HLIT+257, scratch);
                HuffmanEntry* distance_table = compute_huffman_table(distance_lengths, HDIST+1, scratch);

                // SPEC: The actual compressed data of the block, encoded
                // using the literal/length and distance Huffman codes

                // SPEC:
                // loop (until end of block code recognized)
                //    decode literal/length value from input stream
                //    if value < 256
                //       copy value (literal byte) to output stream
                //    otherwise
                //       if value = end of block (256)
                //          break from loop
                //       otherwise (value = 257..285)
                //          decode distance from input stream

                //          move backwards distance bytes in the output
                //          stream, and copy length bytes from this
                //          position to the output stream.
                // end loop

                u32 cursor = 0;
                while (true) {
                    u16 value = decode_next_symbol(&deflate_buffer, literal_table);
                    if (value < 256) {
                        output_stream[cursor++] = (u8)value;
                    }
                    else if (value == 256) {
                        ASSERT(cursor == output_stream_size);
                        break;
                    }
                    else {
                        ASSERT(value <= 285);
                        u16 length = SPEC_LEN_TABLE[value - 257];
                        u16 extra_len_bits = SPEC_LEN_EXTRA_BITS_TABLE[value - 257];
                        if (extra_len_bits > 0) {
                            length += consume_bits(&deflate_buffer, extra_len_bits);
                        }

                        u16 distance_code = decode_next_symbol(&deflate_buffer, distance_table);
                        ASSERT(distance_code <= 29);
                        u16 distance = SPEC_DIST_TABLE[distance_code];
                        u16 extra_dist_bits = SPEC_DIST_EXTRA_BITS_TABLE[distance_code];
                        if (extra_dist_bits) {
                            distance += consume_bits(&deflate_buffer, extra_dist_bits);
                        }
                        ASSERT(distance != 0);
                        ASSERT(distance <= cursor);

                        u32 backwards_cursor = cursor - distance;
                        for (u16 i = 0; i < length; i++) {
                            output_stream[cursor++] = output_stream[backwards_cursor++];
                        }
                    }
                }
            } break;
            case 0b11: {
                // NOTE: This is an invalid value as per the spec.
                return NULL;
            } break;
            default: {
                StringCbPrintfA(printf_buffer, 512, "Btype: %d\n", (int)btype);
                OutputDebugStringA(printf_buffer);
            } break;
        }
    }

    // NOTE: Now we need to reverse the filtering on the image. Each scanline (row of pixels)
    // begins with a byte that indicates the filter type used on this image.
    u32 final_image_size = image_width * image_height * 4;
    u8* final_image = (u8*) pushBytes(return_arena, final_image_size);

    u32 stream_scanline_size = 1 + image_width * 4;
    u32 image_scanline_size = image_width * 4;
    for (u32 scanline = 0; scanline < image_height; scanline ++) {
        u8 filter_type = output_stream[scanline * stream_scanline_size];
        ASSERT(filter_type <= 4);

        u8* output_stream_byte = output_stream + scanline * stream_scanline_size + 1;
        u8* final_image_byte = final_image + scanline * image_scanline_size;

        // NOTE: The variable names in this switch come directly from the PNG spec (section 9.2).
        switch (filter_type) {
            case 0: {
                // NOTE: None
                for (u32 x = 0; x < image_scanline_size; x++) {
                    *(final_image_byte++) = *(output_stream_byte++);
                }
            } break;
            case 1: {
                // NOTE: Sub
                for (u32 x = 0; x < image_scanline_size; x++) {
                    u8 a = x >= 4 ? *(final_image_byte - 4) : 0;
                    *(final_image_byte++) = *(output_stream_byte++) + a;
                }
            } break;
            case 2: {
                // NOTE: Up
                for (u32 x = 0; x < image_scanline_size; x++) {
                    u8 b = scanline > 0 ? *(final_image_byte - image_scanline_size) : 0;
                    *(final_image_byte++) = *(output_stream_byte++) + b;
                }
            } break;
            case 3: {
                // TODO: Average
                ASSERT(false);
            } break;
            case 4: {
                // NOTE: Paeth
                for (u32 x = 0; x < image_scanline_size; x++) {
                    u8 a = x >= 4 ? *(final_image_byte - 4) : 0;
                    u8 b = scanline > 0 ? *(final_image_byte - image_scanline_size) : 0;
                    u8 c = (x >= 4) && (scanline > 0) ? *(final_image_byte - image_scanline_size - 4) : 0;
                    i16 p = a + b - c;
                    i16 pa = (p - a) >= 0 ? (p - a) : -(p - a);
                    i16 pb = (p - b) >= 0 ? (p - b) : -(p - b);
                    i16 pc = (p - c) >= 0 ? (p - c) : -(p - c);

                    i16 pr;
                    if (pa <= pb && pa <= pc) {
                        pr = a;
                    }
                    else if (pb <= pc) {
                        pr = b;
                    } else {
                        pr = c;
                    }
                    ASSERT(pr >= 0 && pr <= 255);
                    *(final_image_byte++) = *(output_stream_byte++) + (u8)pr;
                }

            } break;
        }
    }

    *w = image_width;
    *h = image_height;

    return final_image;
}
