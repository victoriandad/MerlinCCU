#include "ota_upload.h"

#include <cstring>

namespace ota_upload
{

namespace
{

constexpr std::string_view kContentLengthHeader = "Content-Length";
constexpr std::string_view kLineEnd = "\r\n";

/// @brief ASCII lower-case, enough for HTTP header names.
char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// @brief Case-insensitive comparison of two header names.
bool same_name(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0U; i < a.size(); ++i)
    {
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
    }
    return true;
}

/// @brief Drops spaces and tabs from both ends.
std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1U);
    }
    return text;
}

/// @brief Finds a header's value; the request line is skipped.
bool find_header(std::string_view headers, std::string_view name, std::string_view& value)
{
    size_t line_start = headers.find(kLineEnd);
    while (line_start != std::string_view::npos)
    {
        line_start += kLineEnd.size();
        const size_t line_end = headers.find(kLineEnd, line_start);
        const std::string_view line =
            headers.substr(line_start, line_end == std::string_view::npos ? std::string_view::npos
                                                                          : line_end - line_start);
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos && same_name(trim(line.substr(0U, colon)), name))
        {
            value = trim(line.substr(colon + 1U));
            return true;
        }
        line_start = line_end;
    }
    return false;
}

/// @brief Parses a whole decimal number that fits in 32 bits.
bool parse_length(std::string_view text, uint32_t& out)
{
    if (text.empty())
    {
        return false;
    }
    uint64_t value = 0U;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        value = (value * 10U) + static_cast<uint64_t>(c - '0');
        if (value > UINT32_MAX)
        {
            return false;
        }
    }
    out = static_cast<uint32_t>(value);
    return true;
}

/// @brief Compares tokens without stopping at the first different byte.
bool tokens_match(std::string_view given, std::string_view expected)
{
    if (given.size() != expected.size())
    {
        return false;
    }
    unsigned difference = 0U;
    for (size_t i = 0U; i < expected.size(); ++i)
    {
        difference |= static_cast<unsigned>(static_cast<unsigned char>(given[i]) ^
                                            static_cast<unsigned char>(expected[i]));
    }
    return difference == 0U;
}

} // namespace

UploadRequest parse_request(std::string_view headers, std::string_view expected_token,
                            uint32_t slot_bytes)
{
    UploadRequest request;
    if (expected_token.empty())
    {
        request.error = RequestError::NotConfigured;
        return request;
    }

    std::string_view token;
    if (!find_header(headers, kTokenHeader, token) || !tokens_match(token, expected_token))
    {
        request.error = RequestError::BadToken;
        return request;
    }

    std::string_view length_text;
    if (!find_header(headers, kContentLengthHeader, length_text) ||
        !parse_length(length_text, request.content_length))
    {
        request.error = RequestError::MissingLength;
        return request;
    }
    if (request.content_length == 0U)
    {
        request.error = RequestError::Empty;
        return request;
    }
    if (request.content_length > slot_bytes)
    {
        request.error = RequestError::TooLarge;
    }
    return request;
}

bool has_image_block(const uint8_t* data, size_t length)
{
    if (data == nullptr)
    {
        return false;
    }
    const size_t limit = length < kSectorBytes ? length : kSectorBytes;
    for (size_t offset = 0U; offset + sizeof(uint32_t) <= limit; offset += sizeof(uint32_t))
    {
        uint32_t word = 0U;
        std::memcpy(&word, data + offset, sizeof(word));
        if (word == kBlockMarkerStart)
        {
            return true;
        }
    }
    return false;
}

size_t padded_to_page(size_t length)
{
    return ((length + kPageBytes - 1U) / kPageBytes) * kPageBytes;
}

std::string_view error_text(RequestError error)
{
    switch (error)
    {
    case RequestError::None:
        return "OK";
    case RequestError::NotConfigured:
        return "Firmware upload is off: no token is built in (config/ota_credentials.h).";
    case RequestError::BadToken:
        return "Firmware upload refused: missing or wrong X-OTA-Token.";
    case RequestError::MissingLength:
        return "Firmware upload refused: no Content-Length.";
    case RequestError::Empty:
        return "Firmware upload refused: the file is empty.";
    case RequestError::TooLarge:
        return "Firmware upload refused: the image is bigger than the firmware slot.";
    }
    return "Firmware upload refused.";
}

} // namespace ota_upload
