#include "http_response.h"

#include <cstring>

#include "test_framework.h"

HOST_TEST(headers_end_returns_null_for_incomplete_headers)
{
    const char* buffer = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n";
    EXPECT_TRUE(http_response::headers_end(buffer) == nullptr);
}

HOST_TEST(headers_end_finds_the_separator_once_headers_are_complete)
{
    const char* buffer = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    const char* end = http_response::headers_end(buffer);
    EXPECT_TRUE(end != nullptr);
    EXPECT_TRUE(std::strcmp(end, "\r\n\r\nhello") == 0);
}

HOST_TEST(body_returns_the_first_byte_past_the_header_separator)
{
    const char* buffer = "HTTP/1.1 200 OK\r\n\r\n{\"a\":1}";
    const char* body = http_response::body(buffer);
    EXPECT_TRUE(body != nullptr);
    EXPECT_TRUE(std::strcmp(body, "{\"a\":1}") == 0);
}

HOST_TEST(body_returns_null_when_headers_are_incomplete)
{
    const char* buffer = "HTTP/1.1 200 OK\r\n";
    EXPECT_TRUE(http_response::body(buffer) == nullptr);
}

HOST_TEST(partial_status_parses_the_status_line_even_before_headers_finish)
{
    EXPECT_EQ(http_response::partial_status("HTTP/1.1 404 Not Found\r\n"), 404);
    EXPECT_EQ(http_response::partial_status("HTTP/1.0 200 OK\r\nX"), 200);
}

HOST_TEST(partial_status_returns_zero_for_an_incomplete_status_line)
{
    EXPECT_EQ(http_response::partial_status("HTTP/1.1 2"), 0);
    EXPECT_EQ(http_response::partial_status(""), 0);
}

HOST_TEST(find_header_value_matches_case_insensitively)
{
    const char* buffer = "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n\r\nbody";
    const char* end = http_response::headers_end(buffer);
    const char* value = http_response::find_header_value(buffer, end, "Content-Type:");
    EXPECT_TRUE(value != nullptr);
    EXPECT_TRUE(std::strncmp(value, "application/json", 16) == 0);
}

HOST_TEST(find_header_value_returns_null_when_the_header_is_absent)
{
    const char* buffer = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nbody";
    const char* end = http_response::headers_end(buffer);
    EXPECT_TRUE(http_response::find_header_value(buffer, end, "Content-Length:") == nullptr);
}

HOST_TEST(header_has_token_finds_chunked_transfer_encoding_case_insensitively)
{
    const char* buffer = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    const char* end = http_response::headers_end(buffer);
    EXPECT_TRUE(http_response::header_has_token(buffer, end, "Transfer-Encoding:", "chunked"));
}

HOST_TEST(header_has_token_does_not_false_positive_on_body_content)
{
    // A JSON body that happens to contain the literal token text must not be
    // mistaken for a real header -- the search is bounded to the header's
    // own value, not the whole buffer.
    const char* buffer =
        "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n{\"note\":\"chunked\"}";
    const char* end = http_response::headers_end(buffer);
    EXPECT_FALSE(http_response::header_has_token(buffer, end, "Transfer-Encoding:", "chunked"));
}

HOST_TEST(parse_content_length_reads_a_well_formed_header)
{
    const char* buffer = "HTTP/1.1 200 OK\r\nContent-Length: 42\r\n\r\nx";
    const char* end = http_response::headers_end(buffer);
    size_t length = 0;
    EXPECT_TRUE(http_response::parse_content_length(buffer, end, &length));
    EXPECT_EQ(length, 42U);
}

HOST_TEST(parse_content_length_rejects_a_non_numeric_value)
{
    const char* buffer = "HTTP/1.1 200 OK\r\nContent-Length: banana\r\n\r\nx";
    const char* end = http_response::headers_end(buffer);
    size_t length = 0;
    EXPECT_FALSE(http_response::parse_content_length(buffer, end, &length));
}

HOST_TEST(decode_chunked_body_reassembles_multiple_chunks_in_place)
{
    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
    size_t length = std::strlen(buffer);

    EXPECT_TRUE(http_response::decode_chunked_body(buffer, &length));
    EXPECT_TRUE(std::strcmp(http_response::body(buffer), "hello world") == 0);
}

HOST_TEST(decode_chunked_body_fails_on_malformed_chunk_size)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\n\r\nZZ\r\nhello\r\n0\r\n\r\n");
    size_t length = std::strlen(buffer);

    EXPECT_FALSE(http_response::decode_chunked_body(buffer, &length));
}

HOST_TEST(decode_chunked_body_returns_false_when_the_terminating_chunk_has_not_arrived)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\n\r\n5\r\nhello\r\n");
    size_t length = std::strlen(buffer);

    EXPECT_FALSE(http_response::decode_chunked_body(buffer, &length));
}

HOST_TEST(is_complete_uses_content_length_when_present)
{
    EXPECT_TRUE(http_response::is_complete("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello",
                                           std::strlen("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"
                                                       "hello")));

    const char* partial = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello";
    EXPECT_FALSE(http_response::is_complete(partial, std::strlen(partial)));
}

HOST_TEST(is_complete_recognizes_a_terminated_chunked_body)
{
    const char* complete =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    EXPECT_TRUE(http_response::is_complete(complete, std::strlen(complete)));

    const char* partial = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n";
    EXPECT_FALSE(http_response::is_complete(partial, std::strlen(partial)));
}

HOST_TEST(is_complete_is_false_without_content_length_or_chunked_encoding)
{
    // No Content-Length and no chunked encoding -- completeness can only be
    // known by the connection closing, which this pure function can't see.
    const char* buffer = "HTTP/1.1 200 OK\r\n\r\nhello";
    EXPECT_FALSE(http_response::is_complete(buffer, std::strlen(buffer)));
}

HOST_TEST(is_complete_is_false_when_headers_have_not_arrived_yet)
{
    const char* buffer = "HTTP/1.1 200";
    EXPECT_FALSE(http_response::is_complete(buffer, std::strlen(buffer)));
}
