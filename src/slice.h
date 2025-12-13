#pragma once 

#include "common.h"

template <typename T>
struct Slice {
    T* ptr;
    usize len;

    // NOTE: Classic constructor.
    Slice(T* _ptr, usize _len) : ptr(_ptr), len(_len) {};

    // NOTE: Generalized copy constructor.
    // Allows implicit conversions from Slice<U> to Slice<T> where
    // U can be cast to T, e.g Slice<u8> -> Slice<const u8>.
    template <typename U>
    Slice(const Slice<U>& other) : ptr(other.ptr), len(other.len) {}

    // NOTE: Constructor helper that facilitates the creation of
    // const u8 slices from string literals.
    template <usize N>
    constexpr Slice(const char (&literal)[N]) {
        ptr = (const u8*) literal;
        len = N - 1; // NOTE: N includes the null terminator.
    }

    // NOTE: Subscript operator overloading in order to access
    // elements of the slice. With bounds checking in debug mode !
    T& operator[](usize idx) {
        ASSERT(idx < len);
        return ptr[idx];
    }
    const T& operator[](usize idx) const {
        ASSERT(idx < len);
        return ptr[idx];
    }

    // NOTE: Comparison operator, useful for comparing two string slices.
    bool operator==(const Slice<T> other) const {
        if (len != other.len) return false;

        for (usize i = 0; i < len; i++) {
            if (ptr[i] != other.ptr[i]) return false;
        }

        return true;
    }
};

// NOTE: Mutable string slice.
using Str = Slice<u8>;
// NOTE: Read-only string slice.
using StrView = Slice<const u8>;

