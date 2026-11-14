#ifndef REPSP_CODEC_FFMPEG_AVC_FFMPEG_H
#define REPSP_CODEC_FFMPEG_AVC_FFMPEG_H

#include <cstddef>
#include <cstdint>

namespace repsp::codec_ffmpeg
{

void* AvcCreate();
void AvcDestroy(void* ctx);
void AvcFlush(void* ctx);
int AvcDecode(void* ctx,
              const uint8_t* au_bytes,
              size_t au_size,
              uint32_t width,
              uint32_t height,
              uint32_t pixel_format,
              uint8_t* out_bytes,
              size_t out_size);

} // namespace repsp::codec_ffmpeg

#endif // REPSP_CODEC_FFMPEG_AVC_FFMPEG_H
