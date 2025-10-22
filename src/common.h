#pragma once

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usize;

typedef float f32;
typedef double f64;

typedef int8_t b8;
typedef int32_t b32;

#define global static

#define ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))

#if ENGINE_SLOW
    #define ASSERT(expr)\
        if (!(expr)) {\
            volatile u8* ptr = (u8*)(0);\
            *ptr = 0;\
        }
#else
    #define ASSERT(expr) {}
#endif

#define KILOBYTES(value) (value * 1024LL)
#define MEGABYTES(value) (KILOBYTES(value) * 1024LL)
#define GIGABYTES(value) (MEGABYTES(value) * 1024LL)
#define TERABYTES(value) (GIGABYTES(value) * 1024LL)
