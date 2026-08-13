#include "http_response.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace http_response
{

namespace
{

/// @brief Performs an ASCII-only case-insensitive character comparison.
bool ascii_iequals(char left, char right)
{
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

/// @brief Returns whether a string starts with the given ASCII prefix ignoring case.
bool ascii_starts_with_case_insensitive(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr)
    {
        return false;
    }

    while (*prefix != '\0')
    {
        if (*text == '\0' || !ascii_iequals(*text, *prefix))
        {
            return false;
        }
        ++text;
        ++prefix;
    }

    return true;
}

/// @brief Returns the start of the next HTTP header line within the buffer.
const char* next_header_line(const char* line, const char* headers_end_ptr)
{
    if (line == nullptr || headers_end_ptr == nullptr || line >= headers_end_ptr)
    {
        return nullptr;
    }

    const char* line_end = std::strstr(line, "\r\n");
    if (line_end == nullptr || line_end >= headers_end_ptr)
    {
        return nullptr;
    }

    const char* next = line_end + 2;
    return (next < headers_end_ptr) ? next : nullptr;
}

int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

} // namespace

const char* headers_end(const char* buffer)
{
    return buffer == nullptr ? nullptr : std::strstr(buffer, "\r\n\r\n");
}

const char* body(const char* buffer)
{
    const char* end = headers_end(buffer);
    return end == nullptr ? nullptr : end + 4;
}

int partial_status(const char* buffer)
{
    if (buffer == nullptr)
    {
        return 0;
    }

    const char* line_end = std::strstr(buffer, "\r\n");
    if (line_end == nullptr)
    {
        return 0;
    }

    int status = 0;
    if (std::sscanf(buffer, "HTTP/%*d.%*d %d", &status) != 1)
    {
        return 0;
    }

    return status;
}

const char* find_header_value(const char* buffer, const char* headers_end_ptr,
                              const char* header_name)
{
    if (buffer == nullptr || headers_end_ptr == nullptr || header_name == nullptr ||
        header_name[0] == '\0')
    {
        return nullptr;
    }

    const char* line = std::strstr(buffer, "\r\n");
    if (line == nullptr)
    {
        return nullptr;
    }

    for (line += 2; line != nullptr && line < headers_end_ptr;
         line = next_header_line(line, headers_end_ptr))
    {
        while (*line == ' ' || *line == '\t')
        {
            ++line;
        }

        if (ascii_starts_with_case_insensitive(line, header_name))
        {
            const char* value = line + std::strlen(header_name);
            while (*value == ' ' || *value == '\t')
            {
                ++value;
            }
            return value;
        }
    }

    return nullptr;
}

bool header_has_token(const char* buffer, const char* headers_end_ptr, const char* header_name,
                      const char* token)
{
    if (token == nullptr || token[0] == '\0')
    {
        return false;
    }

    const char* value = find_header_value(buffer, headers_end_ptr, header_name);
    if (value == nullptr)
    {
        return false;
    }

    const size_t token_len = std::strlen(token);
    const char* line_end = std::strstr(value, "\r\n");
    if (line_end == nullptr || line_end > headers_end_ptr)
    {
        line_end = headers_end_ptr;
    }
    if (line_end == nullptr)
    {
        return false;
    }

    for (const char* cursor = value; cursor + token_len <= line_end; ++cursor)
    {
        size_t i = 0;
        while (i < token_len && ascii_iequals(cursor[i], token[i]))
        {
            ++i;
        }
        if (i == token_len)
        {
            return true;
        }
    }

    return false;
}

bool parse_content_length(const char* buffer, const char* headers_end_ptr, size_t* out_length)
{
    if (out_length == nullptr)
    {
        return false;
    }

    const char* value = find_header_value(buffer, headers_end_ptr, "Content-Length:");
    if (value == nullptr)
    {
        return false;
    }

    char* parse_end = nullptr;
    const unsigned long parsed = std::strtoul(value, &parse_end, 10);
    if (parse_end == value)
    {
        return false;
    }

    while (parse_end != nullptr && (*parse_end == ' ' || *parse_end == '\t'))
    {
        ++parse_end;
    }

    if (parse_end == nullptr || (*parse_end != '\0' && std::strncmp(parse_end, "\r\n", 2) != 0))
    {
        return false;
    }

    *out_length = static_cast<size_t>(parsed);
    return true;
}

bool decode_chunked_body(char* buffer, size_t* io_length)
{
    if (buffer == nullptr || io_length == nullptr)
    {
        return false;
    }

    char* headers_end_ptr = std::strstr(buffer, "\r\n\r\n");
    if (headers_end_ptr == nullptr)
    {
        return false;
    }

    char* read = headers_end_ptr + 4;
    char* write = read;
    const char* response_end = buffer + *io_length;

    while (read < response_end)
    {
        size_t chunk_size = 0;
        bool saw_digit = false;
        while (read < response_end && *read != '\r' && *read != ';')
        {
            const int value = hex_digit_value(*read);
            if (value < 0)
            {
                return false;
            }

            saw_digit = true;
            chunk_size = (chunk_size * 16U) + static_cast<size_t>(value);
            ++read;
        }

        if (!saw_digit)
        {
            return false;
        }

        while (read < response_end && *read != '\r')
        {
            ++read;
        }

        if ((read + 1) >= response_end || read[0] != '\r' || read[1] != '\n')
        {
            return false;
        }
        read += 2;

        if (chunk_size == 0)
        {
            *write = '\0';
            *io_length = static_cast<size_t>(write - buffer);
            return true;
        }

        if (read + chunk_size + 2 > response_end)
        {
            return false;
        }

        std::memmove(write, read, chunk_size);
        write += chunk_size;
        read += chunk_size;

        if (read[0] != '\r' || read[1] != '\n')
        {
            return false;
        }
        read += 2;
    }

    return false;
}

bool is_complete(const char* buffer, size_t buffer_len)
{
    const char* end = headers_end(buffer);
    if (end == nullptr)
    {
        return false;
    }

    const char* response_body = end + 4;
    const size_t body_length = buffer_len - static_cast<size_t>(response_body - buffer);
    size_t content_length = 0;
    if (parse_content_length(buffer, end, &content_length))
    {
        return body_length >= content_length;
    }

    if (header_has_token(buffer, end, "Transfer-Encoding:", "chunked"))
    {
        return std::strncmp(response_body, "0\r\n", 3) == 0 ||
               std::strstr(response_body, "\r\n0\r\n") != nullptr;
    }

    return false;
}

} // namespace http_response
