#include "png_encoder.h"

#include <array>
#include <initializer_list>

namespace png_encoder
{
namespace
{

constexpr std::array<uint8_t, 8> kPngSignature = {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'};
constexpr uint8_t kBitDepth = 1U;
constexpr uint8_t kColourTypePalette = 3U;
constexpr uint8_t kFilterNone = 0U;
constexpr size_t kChunkFraming = 12U; // length, type, CRC

// zlib header: deflate, 32 KB window, no dictionary, "fastest" level; the
// pair is a multiple of 31 as RFC 1950 requires.
constexpr uint8_t kZlibCmf = 0x78U;
constexpr uint8_t kZlibFlg = 0x01U;
constexpr uint32_t kAdlerModulus = 65521U;

constexpr size_t kMinMatch = 3U;
constexpr size_t kMaxMatch = 258U;
constexpr size_t kMaxDistance = 32768U;
constexpr size_t kStoredBlockMax = 65535U;
constexpr unsigned kEndOfBlock = 256U;
constexpr unsigned kFirstLengthCode = 257U;

// RFC 1951 3.2.5: the length and distance each code starts at, and how many
// extra bits follow it.
constexpr std::array<uint16_t, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                                  15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                                  67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistanceBase = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistanceExtra = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                                    4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                                    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/// @brief CRC-32 lookup by nibble: 64 bytes of table instead of 1 KB.
constexpr std::array<uint32_t, 16> make_crc_table()
{
    std::array<uint32_t, 16> table = {};
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        uint32_t value = i;
        for (int bit = 0; bit < 4; ++bit)
        {
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
        }
        table[i] = value;
    }
    return table;
}
constexpr std::array<uint32_t, 16> kCrcTable = make_crc_table();

/// @brief The image's scanlines as PNG sees them: a filter byte, then the
/// row's bytes with the bits past the width cleared.
class Scanlines
{
  public:
    Scanlines(const uint8_t* bits, int width, int height, int stride)
        : bits_(bits), stride_(static_cast<size_t>(stride)),
          row_bytes_((static_cast<size_t>(width) + 7U) / 8U),
          last_mask_(static_cast<uint8_t>(0xFFU << ((8U - (static_cast<unsigned>(width) & 7U)) & 7U))),
          length_(static_cast<size_t>(height) * (row_bytes_ + 1U))
    {
    }

    size_t length() const { return length_; }
    size_t line_length() const { return row_bytes_ + 1U; }

    uint8_t at(size_t index) const
    {
        const size_t row = index / line_length();
        const size_t column = index % line_length();
        if (column == 0U)
        {
            return kFilterNone;
        }
        const uint8_t value = bits_[row * stride_ + column - 1U];
        return column == row_bytes_ ? static_cast<uint8_t>(value & last_mask_) : value;
    }

  private:
    const uint8_t* bits_;
    size_t stride_;
    size_t row_bytes_;
    uint8_t last_mask_;
    size_t length_;
};

/// @brief Writes bytes and deflate's least-significant-bit-first bit fields
/// into a bounded buffer. Past the end it stops writing and remembers.
class Writer
{
  public:
    Writer(uint8_t* out, size_t capacity) : out_(out), capacity_(capacity) {}

    void byte(uint8_t value)
    {
        if (length_ < capacity_)
        {
            out_[length_] = value;
        }
        ++length_;
    }

    void be32(uint32_t value)
    {
        byte(static_cast<uint8_t>(value >> 24U));
        byte(static_cast<uint8_t>(value >> 16U));
        byte(static_cast<uint8_t>(value >> 8U));
        byte(static_cast<uint8_t>(value));
    }

    void bytes(const uint8_t* data, size_t count)
    {
        for (size_t i = 0U; i < count; ++i)
        {
            byte(data[i]);
        }
    }

    void bits(uint32_t value, unsigned count)
    {
        bit_buffer_ |= value << bit_count_;
        bit_count_ += count;
        while (bit_count_ >= 8U)
        {
            byte(static_cast<uint8_t>(bit_buffer_));
            bit_buffer_ >>= 8U;
            bit_count_ -= 8U;
        }
    }

    /// @brief A Huffman code, which deflate sends most significant bit first.
    void code(uint32_t value, unsigned count)
    {
        uint32_t reversed = 0U;
        for (unsigned i = 0U; i < count; ++i)
        {
            reversed = (reversed << 1U) | ((value >> i) & 1U);
        }
        bits(reversed, count);
    }

    void flush_bits()
    {
        if (bit_count_ > 0U)
        {
            byte(static_cast<uint8_t>(bit_buffer_));
        }
        bit_buffer_ = 0U;
        bit_count_ = 0U;
    }

    bool overflowed() const { return length_ > capacity_; }
    size_t length() const { return length_; }
    uint8_t* data() const { return out_; }

    void rewind(size_t length)
    {
        length_ = length;
        bit_buffer_ = 0U;
        bit_count_ = 0U;
    }

    /// @brief Overwrites four bytes already written.
    void patch_be32(size_t at, uint32_t value)
    {
        if (at + 4U <= capacity_)
        {
            out_[at] = static_cast<uint8_t>(value >> 24U);
            out_[at + 1U] = static_cast<uint8_t>(value >> 16U);
            out_[at + 2U] = static_cast<uint8_t>(value >> 8U);
            out_[at + 3U] = static_cast<uint8_t>(value);
        }
    }

  private:
    uint8_t* out_;
    size_t capacity_;
    size_t length_ = 0U;
    uint32_t bit_buffer_ = 0U;
    unsigned bit_count_ = 0U;
};

/// @brief Writes one literal/length symbol with deflate's fixed codes.
void write_symbol(Writer& writer, unsigned symbol)
{
    constexpr unsigned kNineBitStart = 144U;
    constexpr unsigned kEightBitHighStart = 280U;
    if (symbol < kNineBitStart)
    {
        writer.code(0x30U + symbol, 8U);
    }
    else if (symbol < kEndOfBlock)
    {
        writer.code(0x190U + (symbol - kNineBitStart), 9U);
    }
    else if (symbol < kEightBitHighStart)
    {
        writer.code(symbol - kEndOfBlock, 7U);
    }
    else
    {
        writer.code(0xC0U + (symbol - kEightBitHighStart), 8U);
    }
}

/// @brief Writes a back-reference of `length` bytes from `distance` back.
void write_match(Writer& writer, size_t length, size_t distance)
{
    size_t code = kLengthBase.size() - 1U;
    while (kLengthBase[code] > length)
    {
        --code;
    }
    write_symbol(writer, kFirstLengthCode + static_cast<unsigned>(code));
    writer.bits(static_cast<uint32_t>(length - kLengthBase[code]), kLengthExtra[code]);

    code = kDistanceBase.size() - 1U;
    while (kDistanceBase[code] > distance)
    {
        --code;
    }
    writer.code(static_cast<uint32_t>(code), 5U);
    writer.bits(static_cast<uint32_t>(distance - kDistanceBase[code]), kDistanceExtra[code]);
}

/// @brief How many bytes from `index` repeat those `distance` back.
size_t match_length(const Scanlines& lines, size_t index, size_t distance)
{
    if (distance > index || distance > kMaxDistance)
    {
        return 0U;
    }
    size_t length = 0U;
    while (length < kMaxMatch && index + length < lines.length() &&
           lines.at(index + length) == lines.at(index + length - distance))
    {
        ++length;
    }
    return length;
}

/// @brief One fixed-Huffman deflate block holding every scanline.
void write_fixed_block(Writer& writer, const Scanlines& lines)
{
    writer.bits(1U, 1U); // last block
    writer.bits(1U, 2U); // fixed Huffman codes
    size_t index = 0U;
    while (index < lines.length() && !writer.overflowed())
    {
        const size_t run = match_length(lines, index, 1U);
        const size_t above = match_length(lines, index, lines.line_length());
        const size_t best = above > run ? above : run;
        if (best >= kMinMatch)
        {
            write_match(writer, best, above > run ? lines.line_length() : 1U);
            index += best;
        }
        else
        {
            write_symbol(writer, lines.at(index));
            ++index;
        }
    }
    write_symbol(writer, kEndOfBlock);
    writer.flush_bits();
}

/// @brief The scanlines as stored (uncompressed) deflate blocks.
void write_stored_blocks(Writer& writer, const Scanlines& lines)
{
    size_t index = 0U;
    do
    {
        const size_t count =
            lines.length() - index < kStoredBlockMax ? lines.length() - index : kStoredBlockMax;
        const bool last = index + count == lines.length();
        writer.byte(last ? 1U : 0U); // BFINAL, stored type, padding to the byte
        writer.byte(static_cast<uint8_t>(count));
        writer.byte(static_cast<uint8_t>(count >> 8U));
        writer.byte(static_cast<uint8_t>(~count));
        writer.byte(static_cast<uint8_t>(~count >> 8U));
        for (size_t i = 0U; i < count; ++i)
        {
            writer.byte(lines.at(index + i));
        }
        index += count;
    } while (index < lines.length());
}

uint32_t adler32(const Scanlines& lines)
{
    uint32_t a = 1U;
    uint32_t b = 0U;
    for (size_t i = 0U; i < lines.length(); ++i)
    {
        a = (a + lines.at(i)) % kAdlerModulus;
        b = (b + a) % kAdlerModulus;
    }
    return (b << 16U) | a;
}

/// @brief Starts a chunk; returns where its length goes, for end_chunk().
size_t begin_chunk(Writer& writer, const char* type)
{
    const size_t start = writer.length();
    writer.be32(0U);
    for (int i = 0; i < 4; ++i)
    {
        writer.byte(static_cast<uint8_t>(type[i]));
    }
    return start;
}

/// @brief Fills in a chunk's length and appends its CRC (over type and data).
void end_chunk(Writer& writer, size_t start)
{
    const size_t data_length = writer.length() - start - 8U;
    writer.patch_be32(start, static_cast<uint32_t>(data_length));
    if (!writer.overflowed())
    {
        writer.be32(crc32(writer.data() + start + 4U, data_length + 4U));
    }
}

} // namespace

uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc)
{
    crc = ~crc;
    for (size_t i = 0U; i < length; ++i)
    {
        crc = kCrcTable[(crc ^ data[i]) & 0x0FU] ^ (crc >> 4U);
        crc = kCrcTable[(crc ^ (static_cast<uint32_t>(data[i]) >> 4U)) & 0x0FU] ^ (crc >> 4U);
    }
    return ~crc;
}

size_t encode_1bpp(const uint8_t* bits, int width, int height, int stride, Rgb paper, Rgb ink,
                   uint8_t* out, size_t capacity)
{
    if (bits == nullptr || out == nullptr || width <= 0 || height <= 0 ||
        stride < (width + 7) / 8)
    {
        return 0U;
    }

    const Scanlines lines(bits, width, height, stride);
    Writer writer(out, capacity);
    writer.bytes(kPngSignature.data(), kPngSignature.size());

    size_t chunk = begin_chunk(writer, "IHDR");
    writer.be32(static_cast<uint32_t>(width));
    writer.be32(static_cast<uint32_t>(height));
    writer.byte(kBitDepth);
    writer.byte(kColourTypePalette);
    writer.byte(0U); // deflate
    writer.byte(0U); // adaptive filtering (every row uses None)
    writer.byte(0U); // not interlaced
    end_chunk(writer, chunk);

    chunk = begin_chunk(writer, "PLTE");
    for (const Rgb& colour : {paper, ink})
    {
        writer.byte(colour.r);
        writer.byte(colour.g);
        writer.byte(colour.b);
    }
    end_chunk(writer, chunk);

    chunk = begin_chunk(writer, "IDAT");
    writer.byte(kZlibCmf);
    writer.byte(kZlibFlg);
    const size_t deflate_start = writer.length();
    write_fixed_block(writer, lines);
    // A page that compresses badly is cheaper stored; that size is the
    // worst case max_encoded_size() promises.
    const size_t stored_length = lines.length() +
                                 5U * ((lines.length() + kStoredBlockMax - 1U) / kStoredBlockMax);
    if (writer.overflowed() || writer.length() - deflate_start > stored_length)
    {
        writer.rewind(deflate_start);
        write_stored_blocks(writer, lines);
    }
    writer.be32(adler32(lines));
    end_chunk(writer, chunk);

    chunk = begin_chunk(writer, "IEND");
    end_chunk(writer, chunk);

    return writer.overflowed() ? 0U : writer.length();
}

} // namespace png_encoder
