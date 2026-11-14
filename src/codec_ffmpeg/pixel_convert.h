#ifndef REPSP_CODEC_FFMPEG_PIXEL_CONVERT_H
#define REPSP_CODEC_FFMPEG_PIXEL_CONVERT_H

#include <cstddef>
#include <cstdint>

namespace repsp::codec_ffmpeg
{

// Converts a YUV420p (or YUVJ420p when full_range=true) frame to PSP GE pixel formats.
bool ConvertYuv420pToPsp(const uint8_t* y_plane,
                         int y_stride,
                         const uint8_t* u_plane,
                         int u_stride,
                         const uint8_t* v_plane,
                         int v_stride,
                         uint32_t src_width,
                         uint32_t src_height,
                         uint32_t dst_width,
                         uint32_t dst_height,
                         uint32_t pixel_format,
                         bool full_range,
                         uint8_t* out_bytes,
                         size_t out_size);

// Converts decoded audio samples to interleaved s16le with saturation.
// sample_format uses AVSampleFormat values (passed as int to avoid leaking FFmpeg headers).
bool ConvertAudioToS16Interleaved(const uint8_t* const* data,
                                  int sample_format,
                                  int channels,
                                  int nb_samples,
                                  int16_t* pcm_out,
                                  size_t out_samples);

} // namespace repsp::codec_ffmpeg

#endif // REPSP_CODEC_FFMPEG_PIXEL_CONVERT_H
