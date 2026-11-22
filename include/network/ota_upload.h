#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

/// @brief Checks for a firmware upload (#154), kept free of Pico SDK calls so
/// host tests can cover them.
/// @details The web server streams `POST /api/ota` bodies straight into the
/// spare flash slot. Before it erases anything it needs to know the request is
/// allowed and sized sensibly; before it writes the first sector it needs to
/// know the bytes look like an RP2350 image. Both checks live here.
namespace ota_upload
{

/// @brief Flash erase unit; the upload is written one sector at a time.
inline constexpr size_t kSectorBytes = 4096U;

/// @brief Flash program unit; a short final sector is padded to this.
inline constexpr size_t kPageBytes = 256U;

/// @brief First word of an RP2350 metadata block (PICOBIN_BLOCK_MARKER_START).
/// The boot ROM only looks for an image's block in its first 4 KB.
inline constexpr uint32_t kBlockMarkerStart = 0xFFFFDED3U;

/// @brief Request header carrying the shared upload token.
inline constexpr std::string_view kTokenHeader = "X-OTA-Token";

/// @brief Why an upload request was refused before anything was erased.
enum class RequestError : uint8_t
{
    None = 0,
    NotConfigured, ///< No token is built in, so uploads are switched off.
    BadToken,      ///< The token header is missing or wrong.
    MissingLength, ///< No usable Content-Length.
    Empty,         ///< Content-Length is zero.
    TooLarge,      ///< The image is bigger than the spare slot.
};

/// @brief The parts of an upload request the server acts on.
struct UploadRequest
{
    RequestError error = RequestError::None;
    uint32_t content_length = 0U;
};

/// @brief Checks an upload request's headers.
/// @param headers request line and headers, up to but not including the
/// blank line.
/// @param expected_token the built-in token; empty means uploads are off.
/// @param slot_bytes size of the spare firmware slot.
/// @details Header names match case-insensitively, as HTTP allows. The token
/// is compared in full and in constant time for its length, so a wrong guess
/// learns nothing from timing.
UploadRequest parse_request(std::string_view headers, std::string_view expected_token,
                            uint32_t slot_bytes);

/// @brief True when a metadata block marker sits on a word boundary within
/// the first sector, as the boot ROM requires of a bootable image.
/// @details This catches the common mistakes (uploading the `.uf2` or `.elf`,
/// or the wrong file) before the spare slot is touched. The boot ROM still
/// does the real check, including the image hash, when it boots the slot.
bool has_image_block(const uint8_t* data, size_t length);

/// @brief Rounds a final sector's length up to whole flash pages.
size_t padded_to_page(size_t length);

/// @brief Short reason for a refused request, for the HTTP response.
std::string_view error_text(RequestError error);

} // namespace ota_upload
