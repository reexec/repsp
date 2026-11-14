#pragma once

#include "repsp/gpu/render_backend.h"

#include <cstdint>

namespace repsp::gpu
{

inline constexpr int kTexBpp[11] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8 };

int WrapCoord(int c, int size, int clamp);

uint32_t TexelFetchWithCounter(const DrawState& st, int x, int y, uint64_t* bad_texels);

void DecodeTextureRgba8(const DrawState& st, uint8_t* out_rgba8, uint64_t* bad_texels = nullptr);

uint32_t ComputeTextureDataSizeBytes(const DrawState& st);

} // namespace repsp::gpu
