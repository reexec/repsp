#ifndef REPSP_CODEC_FFMPEG_ATRAC_FFMPEG_H
#define REPSP_CODEC_FFMPEG_ATRAC_FFMPEG_H

#include "repsp/codec/plugin.h"

#include <cstddef>
#include <cstdint>

namespace repsp::codec_ffmpeg
{

void* AtracCreate();
void AtracDestroy(void* ctx);
void AtracReset(void* ctx);
int AtracDecode(void* ctx,
                const RepspAtracFrameInfo* info,
                const uint8_t* frame_bytes,
                size_t frame_size,
                int16_t* pcm_out,
                size_t out_samples);

} // namespace repsp::codec_ffmpeg

#endif // REPSP_CODEC_FFMPEG_ATRAC_FFMPEG_H
