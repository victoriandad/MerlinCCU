#pragma once

#include <cstddef>
#include <cstdint>

/// @brief Encodes the 1 bpp UI framebuffer as a PNG that browsers and Home
/// Assistant show directly (#159).
/// @details No zlib on the Pico, so the image data is compressed with
/// deflate's fixed Huffman codes and two kinds of match: a run of the byte
/// before, and the same bytes one row up. That suits UI pages (blank space,
/// rules and repeated rows) and needs no tables or working memory. A page that
/// does not shrink (dither, noise) is sent stored, uncompressed, instead.
namespace png_encoder
{

/// @brief One palette colour.
struct Rgb
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/// @brief The panel's colours as the web preview draws them: unlit background
/// and lit pixels.
inline constexpr Rgb kPanelPaper = {8U, 16U, 14U};
inline constexpr Rgb kPanelInk = {255U, 214U, 64U};

/// @brief The largest PNG encode_1bpp() writes for this image size: the one
/// with stored (uncompressed) image data.
constexpr size_t max_encoded_size(int width, int height)
{
    constexpr size_t kSignatureAndFixedChunks = 8U + 25U + 18U + 12U + 12U; // + IDAT framing
    constexpr size_t kZlibFraming = 2U + 4U;
    constexpr size_t kStoredBlockMax = 65535U;
    constexpr size_t kStoredBlockHeader = 5U;
    const size_t raw = static_cast<size_t>(height) * (1U + (static_cast<size_t>(width) + 7U) / 8U);
    const size_t blocks = (raw + kStoredBlockMax - 1U) / kStoredBlockMax;
    return kSignatureAndFixedChunks + kZlibFraming + blocks * kStoredBlockHeader + raw;
}

/// @brief Encodes a 1 bpp bitmap as a two-colour (palette) PNG.
/// @param bits Rows of `stride` bytes, leftmost pixel in the top bit; a set
///        bit is drawn in `ink`, a clear one in `paper`. Bits past `width`
///        are ignored.
/// @param out Where the PNG goes.
/// @return The PNG's length, or 0 if it does not fit in `capacity` or the
///         size is invalid. max_encoded_size() bytes always fit.
size_t encode_1bpp(const uint8_t* bits, int width, int height, int stride, Rgb paper, Rgb ink,
                   uint8_t* out, size_t capacity);

/// @brief The CRC-32 PNG and zlib-style tools use (ISO-HDLC), over `length` bytes.
uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0U);

} // namespace png_encoder
