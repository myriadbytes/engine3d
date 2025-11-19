#pragma once

#include "common.h"
#include "allocators.h"

// TODO: This needs some form of error reporting.
u8* read_image(const char* path, u32* w, u32* h, Arena* return_arena, Arena* scratch);
