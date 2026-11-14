#include "atrac_ffmpeg.h"

#include "pixel_convert.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

namespace repsp::codec_ffmpeg
{

namespace
{

struct AtracStreamContext
{
    AVCodecContext* codec_ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

    uint16_t format_tag = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t block_align = 0;
    std::vector<uint8_t> extradata;

    ~AtracStreamContext()
    {
        CloseDecoder();
        if (packet != nullptr)
        {
            av_packet_free(&packet);
        }
        if (frame != nullptr)
        {
            av_frame_free(&frame);
        }
    }

    void CloseDecoder()
    {
        if (codec_ctx != nullptr)
        {
            avcodec_free_context(&codec_ctx);
        }
        format_tag = 0;
        channels = 0;
        sample_rate = 0;
        block_align = 0;
        extradata.clear();
    }

    bool MatchesInfo(const RepspAtracFrameInfo& info) const
    {
        if (codec_ctx == nullptr)
        {
            return false;
        }
        if (format_tag != info.format_tag ||
            channels != info.channels ||
            sample_rate != info.sample_rate ||
            block_align != info.block_align)
        {
            return false;
        }
        if (extradata.size() != info.extradata_size)
        {
            return false;
        }
        if (!extradata.empty() &&
            std::memcmp(extradata.data(), info.extradata, extradata.size()) != 0)
        {
            return false;
        }
        return true;
    }

    bool EnsureOpen(const RepspAtracFrameInfo& info)
    {
        if (MatchesInfo(info))
        {
            return true;
        }
        CloseDecoder();

        if (info.channels == 0 || info.channels > 2 || info.sample_rate == 0)
        {
            return false;
        }

        AVCodecID codec_id = AV_CODEC_ID_NONE;
        if (info.format_tag == 0x0270u)
        {
            codec_id = AV_CODEC_ID_ATRAC3;
        }
        else if (info.format_tag == 0xFFFEu)
        {
            codec_id = AV_CODEC_ID_ATRAC3P;
        }
        else
        {
            return false;
        }

        const AVCodec* codec = avcodec_find_decoder(codec_id);
        if (codec == nullptr)
        {
            return false;
        }

        codec_ctx = avcodec_alloc_context3(codec);
        if (codec_ctx == nullptr)
        {
            return false;
        }

        codec_ctx->sample_rate = static_cast<int>(info.sample_rate);
        av_channel_layout_default(&codec_ctx->ch_layout, static_cast<int>(info.channels));
        codec_ctx->block_align = static_cast<int>(info.block_align);

        if (info.extradata != nullptr && info.extradata_size > 0)
        {
            codec_ctx->extradata = static_cast<uint8_t*>(
                av_mallocz(static_cast<size_t>(info.extradata_size) + AV_INPUT_BUFFER_PADDING_SIZE));
            if (codec_ctx->extradata == nullptr)
            {
                CloseDecoder();
                return false;
            }
            std::memcpy(codec_ctx->extradata, info.extradata, info.extradata_size);
            codec_ctx->extradata_size = static_cast<int>(info.extradata_size);
        }

        if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
        {
            CloseDecoder();
            return false;
        }

        if (packet == nullptr)
        {
            packet = av_packet_alloc();
        }
        if (frame == nullptr)
        {
            frame = av_frame_alloc();
        }
        if (packet == nullptr || frame == nullptr)
        {
            CloseDecoder();
            return false;
        }

        format_tag = info.format_tag;
        channels = info.channels;
        sample_rate = info.sample_rate;
        block_align = info.block_align;
        if (info.extradata != nullptr && info.extradata_size > 0)
        {
            extradata.assign(info.extradata, info.extradata + info.extradata_size);
        }
        return true;
    }
};

} // namespace

void* AtracCreate()
{
    return new AtracStreamContext();
}

void AtracDestroy(void* ctx)
{
    delete static_cast<AtracStreamContext*>(ctx);
}

void AtracReset(void* ctx)
{
    auto* stream = static_cast<AtracStreamContext*>(ctx);
    if (stream != nullptr && stream->codec_ctx != nullptr)
    {
        avcodec_flush_buffers(stream->codec_ctx);
    }
}

int AtracDecode(void* ctx,
                const RepspAtracFrameInfo* info,
                const uint8_t* frame_bytes,
                size_t frame_size,
                int16_t* pcm_out,
                size_t out_samples)
{
    if (info == nullptr || frame_bytes == nullptr || frame_size == 0 ||
        pcm_out == nullptr || out_samples == 0 || info->channels == 0)
    {
        return 0;
    }

    AtracStreamContext local_fallback;
    AtracStreamContext* stream = static_cast<AtracStreamContext*>(ctx);
    if (stream == nullptr)
    {
        stream = &local_fallback;
    }

    if (!stream->EnsureOpen(*info))
    {
        return 0;
    }

    av_packet_unref(stream->packet);
    if (av_new_packet(stream->packet, static_cast<int>(frame_size)) < 0)
    {
        return 0;
    }
    std::memcpy(stream->packet->data, frame_bytes, frame_size);

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
        std::fill(pcm_out, pcm_out + out_samples * info->channels, int16_t{ 0 });
        return 1;
    }
    if (recv_ret < 0)
    {
        return 0;
    }

    const bool ok = ConvertAudioToS16Interleaved(
        stream->frame->data,
        stream->frame->format,
        stream->frame->ch_layout.nb_channels > 0
            ? stream->frame->ch_layout.nb_channels
            : static_cast<int>(info->channels),
        stream->frame->nb_samples,
        pcm_out,
        out_samples);
    av_frame_unref(stream->frame);
    return ok ? 1 : 0;
}

} // namespace repsp::codec_ffmpeg
