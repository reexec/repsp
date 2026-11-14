#ifndef REPSP_CODEC_PLUGIN_H
#define REPSP_CODEC_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REPSP_CODEC_PLUGIN_ABI_VERSION 1u
#define REPSP_CODEC_PLUGIN_GET_V1_SYMBOL "repsp_codec_plugin_get_v1"

#if defined(_WIN32)
#define REPSP_CODEC_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define REPSP_CODEC_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define REPSP_CODEC_PLUGIN_EXPORT
#endif

typedef struct RepspAtracFrameInfo
{
    uint16_t format_tag;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t samples_per_frame;
    uint32_t block_align;
    const uint8_t* extradata;
    uint32_t extradata_size;
    uint32_t flags;
} RepspAtracFrameInfo;

typedef struct RepspCodecPluginV1
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* name;
    const char* description;

    void* (*atrac_create)(void);
    void (*atrac_destroy)(void* ctx);
    void (*atrac_reset)(void* ctx);
    int (*atrac_decode)(void* ctx,
                        const RepspAtracFrameInfo* info,
                        const uint8_t* frame_bytes,
                        size_t frame_size,
                        int16_t* pcm_out,
                        size_t out_samples);

    void* (*avc_create)(void);
    void (*avc_destroy)(void* ctx);
    void (*avc_flush)(void* ctx);
    int (*avc_decode)(void* ctx,
                      const uint8_t* au_bytes,
                      size_t au_size,
                      uint32_t width,
                      uint32_t height,
                      uint32_t pixel_format,
                      uint8_t* out_bytes,
                      size_t out_size);
} RepspCodecPluginV1;

typedef const RepspCodecPluginV1* (*RepspCodecPluginGetV1Fn)(void);

#ifdef __cplusplus
}
#endif

#endif // REPSP_CODEC_PLUGIN_H
