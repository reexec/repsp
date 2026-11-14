#include "repsp/codec/atrac_decoder.h"
#include "repsp/codec/avc_decoder.h"
#include "repsp/codec/loader.h"
#include "repsp/platform/path.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct BitWriter
{
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    int bit_pos = 0;

    void WriteBit(int b)
    {
        cur = static_cast<uint8_t>((cur << 1) | (b & 1));
        ++bit_pos;
        if (bit_pos == 8)
        {
            bytes.push_back(cur);
            cur = 0;
            bit_pos = 0;
        }
    }

    void WriteBits(uint32_t v, int n)
    {
        for (int i = n - 1; i >= 0; --i)
        {
            WriteBit(static_cast<int>((v >> i) & 1u));
        }
    }

    void WriteUe(uint32_t v)
    {
        const uint32_t code = v + 1u;
        int bits = 0;
        for (uint32_t tmp = code; tmp > 0; tmp >>= 1)
        {
            ++bits;
        }
        for (int i = 0; i < bits - 1; ++i)
        {
            WriteBit(0);
        }
        WriteBits(code, bits);
    }

    void WriteSe(int32_t v)
    {
        const uint32_t mapped = (v <= 0)
            ? static_cast<uint32_t>(-2 * v)
            : static_cast<uint32_t>(2 * v - 1);
        WriteUe(mapped);
    }

    void WriteTrailingBits()
    {
        WriteBit(1);
        while (bit_pos != 0)
        {
            WriteBit(0);
        }
    }

    void AlignZero()
    {
        while (bit_pos != 0)
        {
            WriteBit(0);
        }
    }
};

void AppendNal(std::vector<uint8_t>& out, uint8_t nal_header, const std::vector<uint8_t>& rbsp)
{
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x01);
    out.push_back(nal_header);
    int zero_count = 0;
    for (uint8_t b : rbsp)
    {
        if (zero_count == 2 && b <= 0x03)
        {
            out.push_back(0x03);
            zero_count = 0;
        }
        out.push_back(b);
        zero_count = (b == 0x00) ? (zero_count + 1) : 0;
    }
}

std::vector<uint8_t> BuildSyntheticH264Idr16x16(uint8_t y_val, uint8_t u_val, uint8_t v_val)
{
    std::vector<uint8_t> au;

    // SPS
    BitWriter sps;
    sps.WriteBits(66, 8);   // profile_idc = Baseline
    sps.WriteBits(0x80, 8); // constraint_set0_flag = 1
    sps.WriteBits(10, 8);   // level_idc = 1.0
    sps.WriteUe(0);         // seq_parameter_set_id
    sps.WriteUe(0);         // log2_max_frame_num_minus4
    sps.WriteUe(0);         // pic_order_cnt_type
    sps.WriteUe(0);         // log2_max_pic_order_cnt_lsb_minus4
    sps.WriteUe(0);         // max_num_ref_frames
    sps.WriteBit(0);        // gaps_in_frame_num_value_allowed_flag
    sps.WriteUe(0);         // pic_width_in_mbs_minus1 (16px)
    sps.WriteUe(0);         // pic_height_in_map_units_minus1 (16px)
    sps.WriteBit(1);        // frame_mbs_only_flag
    sps.WriteBit(0);        // direct_8x8_inference_flag
    sps.WriteBit(0);        // frame_cropping_flag
    sps.WriteBit(0);        // vui_parameters_present_flag
    sps.WriteTrailingBits();
    AppendNal(au, 0x67, sps.bytes);

    // PPS
    BitWriter pps;
    pps.WriteUe(0);        // pic_parameter_set_id
    pps.WriteUe(0);        // seq_parameter_set_id
    pps.WriteBit(0);       // entropy_coding_mode_flag (CAVLC)
    pps.WriteBit(0);       // bottom_field_pic_order_in_frame_present_flag
    pps.WriteUe(0);        // num_slice_groups_minus1
    pps.WriteUe(0);        // num_ref_idx_l0_default_active_minus1
    pps.WriteUe(0);        // num_ref_idx_l1_default_active_minus1
    pps.WriteBit(0);       // weighted_pred_flag
    pps.WriteBits(0, 2);   // weighted_bipred_idc
    pps.WriteSe(0);        // pic_init_qp_minus26
    pps.WriteSe(0);        // pic_init_qs_minus26
    pps.WriteSe(0);        // chroma_qp_index_offset
    pps.WriteBit(0);       // deblocking_filter_control_present_flag
    pps.WriteBit(0);       // constrained_intra_pred_flag
    pps.WriteBit(0);       // redundant_pic_cnt_present_flag
    pps.WriteTrailingBits();
    AppendNal(au, 0x68, pps.bytes);

    // IDR slice with single I_PCM macroblock
    BitWriter slice;
    slice.WriteUe(0);      // first_mb_in_slice
    slice.WriteUe(7);      // slice_type = I
    slice.WriteUe(0);      // pic_parameter_set_id
    slice.WriteBits(0, 4); // frame_num
    slice.WriteUe(0);      // idr_pic_id
    slice.WriteBits(0, 4); // pic_order_cnt_lsb
    slice.WriteBit(0);     // no_output_of_prior_pics_flag
    slice.WriteBit(0);     // long_term_reference_flag
    slice.WriteSe(0);      // slice_qp_delta
    slice.WriteUe(25);     // mb_type = I_PCM
    slice.AlignZero();
    for (int i = 0; i < 256; ++i)
    {
        slice.bytes.push_back(y_val);
    }
    for (int i = 0; i < 64; ++i)
    {
        slice.bytes.push_back(u_val);
    }
    for (int i = 0; i < 64; ++i)
    {
        slice.bytes.push_back(v_val);
    }
    slice.WriteTrailingBits();
    AppendNal(au, 0x65, slice.bytes);

    return au;
}

} // namespace

int main()
{
    using namespace repsp::codec;
    using namespace repsp::platform;

    const std::filesystem::path plugin_path = GetExecutableDirectory() / DefaultCodecPluginFilename();
    std::string err;
    auto plugin = LoadCodecPlugin(plugin_path, &err);
    if (plugin == nullptr || !plugin->IsLoaded())
    {
        std::fprintf(stderr, "Failed to load FFmpeg codec plugin from %s: %s\n",
                     plugin_path.string().c_str(), err.c_str());
        return 1;
    }

    if (std::string(plugin->Name()) != "ffmpeg" || std::string(plugin->Description()).empty())
    {
        std::fprintf(stderr, "Unexpected plugin metadata: name='%s'\n", plugin->Name());
        return 1;
    }

    AvcDecoder* avc = plugin->GetAvcDecoder();
    AtracDecoder* atrac = plugin->GetAtracDecoder();
    if (avc == nullptr || atrac == nullptr)
    {
        std::fprintf(stderr, "Plugin did not expose both AVC and ATRAC decoders\n");
        return 1;
    }

    // Test AVC synthetic 16x16 I_PCM frame (Y=200, U=64, V=220 -> warm orange: R > G > B).
    void* avc_ctx = avc->CreateStreamContext();
    const std::vector<uint8_t> au = BuildSyntheticH264Idr16x16(200, 64, 220);

    std::vector<uint8_t> rgba(16u * 16u * 4u, 0);
    if (!avc->DecodeAu(avc_ctx, au, 16, 16, kAvcPixelFormat8888, rgba))
    {
        std::fprintf(stderr, "AVC DecodeAu failed for 8888\n");
        avc->DestroyStreamContext(avc_ctx);
        return 1;
    }
    const uint8_t r = rgba[0];
    const uint8_t g = rgba[1];
    const uint8_t b = rgba[2];
    const uint8_t a = rgba[3];
    if (a != 0xFFu || !(r > g && g > b))
    {
        std::fprintf(stderr, "Unexpected 8888 pixel: R=%u G=%u B=%u A=%u\n", r, g, b, a);
        avc->DestroyStreamContext(avc_ctx);
        return 1;
    }

    avc->Flush(avc_ctx);
    std::vector<uint8_t> rgb565(16u * 16u * 2u, 0);
    if (!avc->DecodeAu(avc_ctx, au, 16, 16, kAvcPixelFormat5650, rgb565))
    {
        std::fprintf(stderr, "AVC DecodeAu failed for 5650 after flush\n");
        avc->DestroyStreamContext(avc_ctx);
        return 1;
    }
    avc->DestroyStreamContext(avc_ctx);

    // Test ATRAC context lifecycle and error handling on invalid inputs.
    void* atrac_ctx = atrac->CreateStreamContext();
    atrac->ResetStreamContext(atrac_ctx);
    AtracFrameInfo bad_info{};
    bad_info.format_tag = 0x1234u;
    bad_info.channels = 2;
    bad_info.sample_rate = 44100;
    bad_info.samples_per_frame = 2048;
    std::vector<int16_t> pcm(2048u * 2u, 0);
    const uint8_t dummy_frame[16]{};
    if (atrac->DecodeFrame(atrac_ctx, bad_info, dummy_frame, sizeof(dummy_frame), pcm.data(), 2048))
    {
        std::fprintf(stderr, "Expected unsupported format_tag to return false\n");
        atrac->DestroyStreamContext(atrac_ctx);
        return 1;
    }
    atrac->DestroyStreamContext(atrac_ctx);

    std::printf("codec_ffmpeg_probe: PASS (%s)\n", plugin->Description());
    return 0;
}
