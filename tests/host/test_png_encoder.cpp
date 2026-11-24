#include "png_encoder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "panel_config.h"
#include "test_framework.h"

namespace
{

constexpr png_encoder::Rgb kPaper = png_encoder::kPanelPaper;
constexpr png_encoder::Rgb kInk = png_encoder::kPanelInk;

uint32_t read_be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24U) | (static_cast<uint32_t>(p[1]) << 16U) |
           (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
}

/// @brief Reads deflate's bit fields; enough of an inflater to check the
/// encoder, which only writes stored and fixed-Huffman blocks.
class BitReader
{
  public:
    BitReader(const uint8_t* data, size_t length) : data_(data), length_(length) {}

    uint32_t bits(unsigned count)
    {
        uint32_t value = 0U;
        for (unsigned i = 0U; i < count; ++i)
        {
            value |= static_cast<uint32_t>(bit()) << i;
        }
        return value;
    }

    unsigned bit()
    {
        if (byte_ >= length_)
        {
            failed = true;
            return 0U;
        }
        const unsigned value = (data_[byte_] >> bit_) & 1U;
        if (++bit_ == 8U)
        {
            bit_ = 0U;
            ++byte_;
        }
        return value;
    }

    void align()
    {
        if (bit_ != 0U)
        {
            bit_ = 0U;
            ++byte_;
        }
    }

    bool failed = false;

  private:
    const uint8_t* data_;
    size_t length_;
    size_t byte_ = 0U;
    unsigned bit_ = 0U;
};

unsigned fixed_symbol(BitReader& reader)
{
    unsigned code = 0U;
    for (int i = 0; i < 7; ++i)
    {
        code = (code << 1U) | reader.bit();
    }
    if (code <= 0x17U)
    {
        return 256U + code;
    }
    code = (code << 1U) | reader.bit();
    if (code >= 0x30U && code <= 0xBFU)
    {
        return code - 0x30U;
    }
    if (code >= 0xC0U && code <= 0xC7U)
    {
        return 280U + (code - 0xC0U);
    }
    code = (code << 1U) | reader.bit();
    return 144U + (code - 0x190U);
}

/// @brief Inflates a stored/fixed-only deflate stream; false on a bad one.
bool inflate(const uint8_t* data, size_t length, std::vector<uint8_t>& out)
{
    static const uint16_t kLengthBase[] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                           15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                           67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t kLengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t kDistanceBase[] = {
        1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t kDistanceExtra[] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    BitReader reader(data, length);
    bool last = false;
    while (!last && !reader.failed)
    {
        last = reader.bits(1U) != 0U;
        const uint32_t type = reader.bits(2U);
        if (type == 0U)
        {
            reader.align();
            const uint32_t count = reader.bits(16U);
            if ((reader.bits(16U) ^ 0xFFFFU) != count)
            {
                return false;
            }
            for (uint32_t i = 0U; i < count; ++i)
            {
                out.push_back(static_cast<uint8_t>(reader.bits(8U)));
            }
            continue;
        }
        if (type != 1U)
        {
            return false;
        }
        for (;;)
        {
            const unsigned symbol = fixed_symbol(reader);
            if (reader.failed || symbol > 285U)
            {
                return false;
            }
            if (symbol < 256U)
            {
                out.push_back(static_cast<uint8_t>(symbol));
                continue;
            }
            if (symbol == 256U)
            {
                break;
            }
            const unsigned length_code = symbol - 257U;
            const size_t match = kLengthBase[length_code] + reader.bits(kLengthExtra[length_code]);
            unsigned distance_code = 0U;
            for (int i = 0; i < 5; ++i)
            {
                distance_code = (distance_code << 1U) | reader.bit();
            }
            if (distance_code >= 30U)
            {
                return false;
            }
            const size_t distance =
                kDistanceBase[distance_code] + reader.bits(kDistanceExtra[distance_code]);
            if (distance > out.size())
            {
                return false;
            }
            for (size_t i = 0U; i < match; ++i)
            {
                out.push_back(out[out.size() - distance]);
            }
        }
    }
    return last && !reader.failed;
}

/// @brief What a decoded PNG held.
struct Decoded
{
    bool ok = false;
    uint32_t width = 0U;
    uint32_t height = 0U;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> scanlines;
};

/// @brief Walks the chunks, checking every CRC and the zlib framing.
Decoded decode(const uint8_t* png, size_t length)
{
    Decoded result;
    static const uint8_t kSignature[] = {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'};
    if (length < 8U || std::memcmp(png, kSignature, 8U) != 0)
    {
        return result;
    }
    std::vector<uint8_t> idat;
    size_t at = 8U;
    bool ended = false;
    while (at + 12U <= length && !ended)
    {
        const uint32_t data_length = read_be32(png + at);
        if (at + 12U + data_length > length)
        {
            return result;
        }
        const std::string type(reinterpret_cast<const char*>(png + at + 4U), 4U);
        const uint8_t* data = png + at + 8U;
        if (png_encoder::crc32(png + at + 4U, data_length + 4U) != read_be32(data + data_length))
        {
            return result;
        }
        if (type == "IHDR")
        {
            result.width = read_be32(data);
            result.height = read_be32(data + 4U);
            if (data[8] != 1U || data[9] != 3U)
            {
                return result;
            }
        }
        else if (type == "PLTE")
        {
            result.palette.assign(data, data + data_length);
        }
        else if (type == "IDAT")
        {
            idat.insert(idat.end(), data, data + data_length);
        }
        else if (type == "IEND")
        {
            ended = true;
        }
        at += 12U + data_length;
    }
    if (!ended || at != length || idat.size() < 6U || ((idat[0] << 8U) | idat[1]) % 31U != 0U)
    {
        return result;
    }
    if (!inflate(idat.data() + 2U, idat.size() - 6U, result.scanlines))
    {
        return result;
    }
    uint32_t a = 1U;
    uint32_t b = 0U;
    for (const uint8_t value : result.scanlines)
    {
        a = (a + value) % 65521U;
        b = (b + a) % 65521U;
    }
    result.ok = ((b << 16U) | a) == read_be32(idat.data() + idat.size() - 4U);
    return result;
}

/// @brief The scanlines a framebuffer should decode to.
std::vector<uint8_t> expected_scanlines(const uint8_t* fb)
{
    std::vector<uint8_t> lines;
    for (int y = 0; y < kUiHeight; ++y)
    {
        lines.push_back(0U);
        for (int x = 0; x < kUiStride; ++x)
        {
            uint8_t value = fb[y * kUiStride + x];
            if (x == kUiStride - 1 && (kUiWidth & 7) != 0)
            {
                value = static_cast<uint8_t>(value & (0xFFU << (8 - (kUiWidth & 7))));
            }
            lines.push_back(value);
        }
    }
    return lines;
}

/// @brief Encodes, decodes and compares; returns the PNG size or 0.
size_t round_trip(const uint8_t* fb)
{
    std::vector<uint8_t> png(png_encoder::max_encoded_size(kUiWidth, kUiHeight));
    const size_t length =
        png_encoder::encode_1bpp(fb, kUiWidth, kUiHeight, kUiStride, kPaper, kInk, png.data(),
                                 png.size());
    if (length == 0U)
    {
        return 0U;
    }
    const Decoded decoded = decode(png.data(), length);
    const std::vector<uint8_t> palette = {kPaper.r, kPaper.g, kPaper.b, kInk.r, kInk.g, kInk.b};
    if (!decoded.ok || decoded.width != static_cast<uint32_t>(kUiWidth) ||
        decoded.height != static_cast<uint32_t>(kUiHeight) || decoded.palette != palette ||
        decoded.scanlines != expected_scanlines(fb))
    {
        return 0U;
    }
    return length;
}

/// @brief With MERLINCCU_PNG_DUMP_DIR set, writes the page's PNG there so a
/// person, or another decoder, can check it.
void dump_png(const char* name, const uint8_t* fb)
{
    const char* dir = std::getenv("MERLINCCU_PNG_DUMP_DIR");
    if (dir == nullptr)
    {
        return;
    }
    std::vector<uint8_t> png(png_encoder::max_encoded_size(kUiWidth, kUiHeight));
    const size_t length = png_encoder::encode_1bpp(fb, kUiWidth, kUiHeight, kUiStride, kPaper,
                                                   kInk, png.data(), png.size());
    const std::string path = std::string(dir) + "/" + name + ".png";
    if (std::FILE* file = std::fopen(path.c_str(), "wb"))
    {
        std::fwrite(png.data(), 1U, length, file);
        std::fclose(file);
    }
}

} // namespace

HOST_TEST(png_encoder_crc32_matches_the_standard_check_value)
{
    const char* text = "123456789";
    EXPECT_EQ(png_encoder::crc32(reinterpret_cast<const uint8_t*>(text), 9U), 0xCBF43926U);
}

HOST_TEST(png_encoder_blank_page_is_tiny_and_decodes)
{
    std::vector<uint8_t> fb(kUiFbSize, 0U);
    const size_t length = round_trip(fb.data());
    EXPECT_TRUE(length > 0U);
    EXPECT_TRUE(length < 200U);

    // Bits past the width are left out of the image.
    std::fill(fb.begin(), fb.end(), uint8_t{0xFFU});
    EXPECT_TRUE(round_trip(fb.data()) > 0U);
}

HOST_TEST(png_encoder_noise_falls_back_to_stored_within_the_worst_case)
{
    std::vector<uint8_t> fb(kUiFbSize);
    uint32_t seed = 12345U;
    for (uint8_t& value : fb)
    {
        seed = seed * 1103515245U + 12345U;
        value = static_cast<uint8_t>(seed >> 16U);
    }
    const size_t length = round_trip(fb.data());
    EXPECT_TRUE(length > 0U);
    EXPECT_EQ(length, png_encoder::max_encoded_size(kUiWidth, kUiHeight));
}

HOST_TEST(png_encoder_refuses_a_buffer_too_small)
{
    std::vector<uint8_t> fb(kUiFbSize, 0U);
    std::vector<uint8_t> png(40U);
    EXPECT_EQ(png_encoder::encode_1bpp(fb.data(), kUiWidth, kUiHeight, kUiStride, kPaper, kInk,
                                       png.data(), png.size()),
              size_t{0U});
    EXPECT_EQ(png_encoder::encode_1bpp(fb.data(), 0, kUiHeight, kUiStride, kPaper, kInk,
                                       png.data(), png.size()),
              size_t{0U});
}

HOST_TEST(png_encoder_every_golden_page_decodes)
{
    // The goldens are real pages, so their sizes are what HA will be sent.
    size_t largest = 0U;
    std::string largest_name;
    int pages = 0;
    for (const char* name :
         {"page_home", "page_status", "page_status_overview", "page_status_resources",
          "page_settings", "page_shares", "page_share_detail", "page_weather", "page_calendar",
          "page_alert_list", "page_alert_detail", "page_local_conditions",
          "page_local_condition_graph", "page_pinter", "page_keypad_debug",
          "page_greyscale_test"})
    {
        const std::string path = std::string(MERLINCCU_GOLDEN_DIR) + "/" + name + ".pbm";
        std::FILE* file = std::fopen(path.c_str(), "rb");
        EXPECT_TRUE(file != nullptr);
        if (file == nullptr)
        {
            continue;
        }
        std::vector<uint8_t> contents(64U * 1024U);
        const size_t read = std::fread(contents.data(), 1U, contents.size(), file);
        std::fclose(file);
        EXPECT_TRUE(read >= static_cast<size_t>(kUiFbSize));
        const uint8_t* fb = contents.data() + read - kUiFbSize;
        const size_t length = round_trip(fb);
        EXPECT_TRUE(length > 0U);
        dump_png(name, fb);
        if (length > largest)
        {
            largest = length;
            largest_name = name;
        }
        ++pages;
    }
    std::printf("  largest golden PNG: %s, %zu bytes over %d pages\n", largest_name.c_str(),
                largest, pages);
}
