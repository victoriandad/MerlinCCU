#include "ota_upload.h"

#include <array>
#include <cstring>
#include <string>

#include "test_framework.h"

namespace
{

using ota_upload::RequestError;

constexpr uint32_t kSlotBytes = 1536U * 1024U;
constexpr std::string_view kToken = "s3cret-token";

/// @brief Builds an upload request's headers.
std::string headers(std::string_view token_line, std::string_view length_line)
{
    std::string text = "POST /api/ota HTTP/1.1\r\nHost: merlin-dining\r\n";
    text += token_line;
    text += length_line;
    text += "Content-Type: application/octet-stream";
    return text;
}

} // namespace

HOST_TEST(ota_request_with_token_and_length_is_accepted)
{
    const std::string text = headers("X-OTA-Token: s3cret-token\r\n", "Content-Length: 758000\r\n");
    const ota_upload::UploadRequest request = ota_upload::parse_request(text, kToken, kSlotBytes);
    EXPECT_TRUE(request.error == RequestError::None);
    EXPECT_EQ(request.content_length, 758000U);
}

HOST_TEST(ota_request_header_names_ignore_case)
{
    const std::string text = headers("x-ota-token:s3cret-token\r\n", "content-length:  42  \r\n");
    const ota_upload::UploadRequest request = ota_upload::parse_request(text, kToken, kSlotBytes);
    EXPECT_TRUE(request.error == RequestError::None);
    EXPECT_EQ(request.content_length, 42U);
}

HOST_TEST(ota_request_is_refused_without_a_built_in_token)
{
    const std::string text = headers("X-OTA-Token: \r\n", "Content-Length: 42\r\n");
    EXPECT_TRUE(ota_upload::parse_request(text, "", kSlotBytes).error ==
                RequestError::NotConfigured);
}

HOST_TEST(ota_request_is_refused_with_a_missing_or_wrong_token)
{
    EXPECT_TRUE(ota_upload::parse_request(headers("", "Content-Length: 42\r\n"), kToken, kSlotBytes)
                    .error == RequestError::BadToken);
    EXPECT_TRUE(
        ota_upload::parse_request(
            headers("X-OTA-Token: s3cret-tokem\r\n", "Content-Length: 42\r\n"), kToken, kSlotBytes)
            .error == RequestError::BadToken);
    // A prefix of the right token is still wrong.
    EXPECT_TRUE(
        ota_upload::parse_request(headers("X-OTA-Token: s3cret\r\n", "Content-Length: 42\r\n"),
                                  kToken, kSlotBytes)
            .error == RequestError::BadToken);
}

HOST_TEST(ota_request_needs_a_sensible_length)
{
    const std::string_view token_line = "X-OTA-Token: s3cret-token\r\n";
    EXPECT_TRUE(ota_upload::parse_request(headers(token_line, ""), kToken, kSlotBytes).error ==
                RequestError::MissingLength);
    EXPECT_TRUE(ota_upload::parse_request(headers(token_line, "Content-Length: 12x\r\n"), kToken,
                                          kSlotBytes)
                    .error == RequestError::MissingLength);
    EXPECT_TRUE(ota_upload::parse_request(headers(token_line, "Content-Length: 99999999999\r\n"),
                                          kToken, kSlotBytes)
                    .error == RequestError::MissingLength);
    EXPECT_TRUE(
        ota_upload::parse_request(headers(token_line, "Content-Length: 0\r\n"), kToken, kSlotBytes)
            .error == RequestError::Empty);
    EXPECT_TRUE(ota_upload::parse_request(headers(token_line, "Content-Length: 1572865\r\n"),
                                          kToken, kSlotBytes)
                    .error == RequestError::TooLarge);
    EXPECT_TRUE(ota_upload::parse_request(headers(token_line, "Content-Length: 1572864\r\n"),
                                          kToken, kSlotBytes)
                    .error == RequestError::None);
}

HOST_TEST(ota_request_line_is_not_mistaken_for_a_header)
{
    // "Content-Length" in the request line must not count as the header.
    const std::string text = "POST /api/ota?Content-Length:5 HTTP/1.1\r\nX-OTA-Token: s3cret-token";
    EXPECT_TRUE(ota_upload::parse_request(text, kToken, kSlotBytes).error ==
                RequestError::MissingLength);
}

HOST_TEST(ota_image_block_is_found_on_a_word_boundary_in_the_first_sector)
{
    std::array<uint8_t, ota_upload::kSectorBytes> sector = {};
    EXPECT_FALSE(ota_upload::has_image_block(sector.data(), sector.size()));

    const uint32_t marker = ota_upload::kBlockMarkerStart;
    std::memcpy(sector.data() + 0x110U, &marker, sizeof(marker));
    EXPECT_TRUE(ota_upload::has_image_block(sector.data(), sector.size()));

    // Off a word boundary it does not count.
    sector = {};
    std::memcpy(sector.data() + 0x111U, &marker, sizeof(marker));
    EXPECT_FALSE(ota_upload::has_image_block(sector.data(), sector.size()));

    // A UF2 file starts with its own magic, not the block marker.
    sector = {};
    const char uf2[] = "UF2\n";
    std::memcpy(sector.data(), uf2, 4U);
    EXPECT_FALSE(ota_upload::has_image_block(sector.data(), sector.size()));
    EXPECT_FALSE(ota_upload::has_image_block(nullptr, 0U));
}

HOST_TEST(ota_final_sector_pads_to_whole_pages)
{
    EXPECT_EQ(ota_upload::padded_to_page(1U), 256U);
    EXPECT_EQ(ota_upload::padded_to_page(256U), 256U);
    EXPECT_EQ(ota_upload::padded_to_page(257U), 512U);
    EXPECT_EQ(ota_upload::padded_to_page(4096U), 4096U);
}
