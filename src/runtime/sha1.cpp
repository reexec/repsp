#include "repsp/runtime/sha1.h"

#include <cstring>

namespace repsp::runtime
{

namespace
{

inline uint32_t LeftRotate(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32 - bits));
}

} // namespace

void ComputeSha1(const uint8_t* data, size_t length, uint8_t digest[20])
{
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t total_bits = static_cast<uint64_t>(length) * 8ULL;

    size_t padded_len = length + 1;
    while ((padded_len % 64) != 56)
    {
        ++padded_len;
    }
    padded_len += 8;

    std::string buffer(padded_len, '\0');
    if (length > 0)
    {
        std::memcpy(buffer.data(), data, length);
    }
    buffer[length] = static_cast<char>(0x80);

    for (int i = 0; i < 8; ++i)
    {
        buffer[padded_len - 1 - i] = static_cast<char>((total_bits >> (i * 8)) & 0xFF);
    }

    for (size_t chunk = 0; chunk < padded_len; chunk += 64)
    {
        uint32_t w[80];
        const uint8_t* p = reinterpret_cast<const uint8_t*>(buffer.data() + chunk);

        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(p[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(p[i * 4 + 3]));
        }

        for (int i = 16; i < 80; ++i)
        {
            w[i] = LeftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f = 0;
            uint32_t k = 0;

            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = LeftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = LeftRotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    uint32_t h[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i)
    {
        digest[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
}

uint32_t ComputeNid(const std::string& function_name)
{
    uint8_t digest[20];
    ComputeSha1(reinterpret_cast<const uint8_t*>(function_name.data()), function_name.size(), digest);
    return static_cast<uint32_t>(digest[0]) |
           (static_cast<uint32_t>(digest[1]) << 8) |
           (static_cast<uint32_t>(digest[2]) << 16) |
           (static_cast<uint32_t>(digest[3]) << 24);
}

} // namespace repsp::runtime
