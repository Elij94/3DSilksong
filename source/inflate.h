#pragma once
#include <stddef.h>
#include <3ds/types.h>

// Decompresses a zlib stream (or raw deflate) into dst. Returns the number of bytes written, -1 on error.
long zinflate(const void *src, size_t srclen, void *dst, size_t dstlen);
