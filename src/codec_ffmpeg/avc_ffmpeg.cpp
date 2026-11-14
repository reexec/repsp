#include "avc_ffmpeg.h"

#include "pixel_convert.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <cstring>

namespace repsp::codec_ffmpeg
{

namespace
{

struct AvcStreamContext
{
    AVCodecContext* codec_ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

    ~AvcStreamContext()
    {
        Close();
    }

    void Close()
    {
        if (codec_ctx != nullptr)
        {
            avcodec_free_context(&codec_ctx);
        }
        if (packet != nullptr)
        {
            av_packet_free(&packet);
        }
        if (frame != nullptr)
        {
            av_frame_free(&frame);
        }
    }

    bool EnsureOpen()
    {
        if (codec_ctx != nullptr)
        {
            return true;
        }

        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (codec == nullptr)
        {
            return false;
        }

        codec_ctx = avcodec_alloc_context3(codec);
        if (codec_ctx == nullptr)
        {
            return false;
        }

        if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
        {
            Close();
            return false;
        }

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (packet == nullptr || frame == nullptr)
        {
            Close();
            return false;
        }
        return true;
    }
};

} // namespace

void* AvcCreate()
{
    return new AvcStreamContext();
}

void AvcDestroy(void* ctx)
{
    delete static_cast<AvcStreamContext*>(ctx);
}

void AvcFlush(void* ctx)
{
    auto* stream = static_cast<AvcStreamContext*>(ctx);
    if (stream != nullptr && stream->codec_ctx != nullptr)
    {
        avcodec_flush_buffers(stream->codec_ctx);
    }
}

int AvcDecode(void* ctx,
              const uint8_t* au_bytes,
              size_t au_size,
              uint32_t width,
              uint32_t height,
              uint32_t pixel_format,
              uint8_t* out_bytes,
              size_t out_size)
{
    if (au_bytes == nullptr || au_size == 0 ||
        out_bytes == nullptr || width == 0 || height == 0)
    {
        return 0;
    }

    AvcStreamContext local_fallback;
    AvcStreamContext* stream = static_cast<AvcStreamContext*>(ctx);
    if (stream == nullptr)
    {
        stream = &local_fallback;
    }

    if (!stream->EnsureOpen())
    {
        return 0;
    }

    av_packet_unref(stream->packet);
    if (av_new_packet(stream->packet, static_cast<int>(au_size)) < 0)
    {
        return 0;
    }
    std::memcpy(stream->packet->data, au_bytes, au_size);

    const int send_ret = avcodec_send_packet(stream->codec_ctx, stream->packet);
    av_packet_unref(stream->packet);
    if (send_ret < 0)
    {
        return 0;
    }

    av_frame_unref(stream->frame);
    const int recv_ret = avcodec_receive_frame(stream->codec_ctx, stream->frame);
    if (recv_ret == AVERROR(EAGAIN))
    {
        return 1;
    }
    if (recv_ret < 0)
    {
        return 0;
    }

    const bool full_range = (stream->frame->format == AV_PIX_FMT_YUVJ420P);
    if (stream->frame->format != AV_PIX_FMT_YUV420P && !full_range)
    {
        av_frame_unref(stream->frame);
        return 0;
    }

    const bool ok = ConvertYuv420pToPsp(
        stream->frame->data[0],
        stream->frame->linesize[0],
        stream->frame->data[1],
        stream->frame->linesize[1],
        stream->frame->data[2],
        stream->frame->linesize[2],
        static_cast<uint32_t>(stream->frame->width),
        static_cast<uint32_t>(stream->frame->height),
        width,
        height,
        pixel_format,
        full_range,
        out_bytes,
        out_size);
    av_frame_unref(stream->frame);
    return ok ? 1 : 0;
}

} // namespace repsp::codec_ffmpeg
