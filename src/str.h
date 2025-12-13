#pragma once

#include "common.h"
#include "slice.h"

// NOTE: Here are the possible format string placeholders :
// - {(u|i)(32|64)} -> for the corresponding integers
// - {f(32|64)} -> for floating point types
// - {size} -> prints a size_t as an actual memory size, i.e. "64 KB" or "4 MB"
StrView formatString(Slice<u8> buffer, StrView fmt, ...);
