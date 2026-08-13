#pragma once

#include <cstddef>

/// @brief Pure, hardware-independent HTTP response parsing shared by every
/// network manager (issue #46).
/// @details Extracted from home_assistant_manager.cpp -- the most mature and
/// battle-tested of the three implementations that had converged on nearly
/// the same logic (status-line parsing, chunked decoding, completion
/// detection) independently. Knows nothing about lwIP/altcp, sockets, or any
/// particular manager's retry/completion state machine; every function takes
/// an explicit buffer and length rather than reading a manager's own global
/// response buffer, so this is host-testable (see tests/host/) and reusable
/// regardless of how a caller structures its own connect/send/recv flow.
///
/// Deliberately NOT here (see docs/architecture.md's "Network helper
/// ownership boundary"): TLS trust policy (issue #17), JSON body parsing
/// (provider-specific, stays in each manager), and the actual socket/DNS/
/// retry state machine (each manager's completion-handling shape differs for
/// real reasons -- see the architecture doc).
namespace http_response
{

/// @brief Returns a pointer to the `\r\n\r\n` header/body separator itself, or
/// nullptr if the header block isn't complete yet in `buffer`.
const char* headers_end(const char* buffer);

/// @brief Returns a pointer to the first byte of the response body, or
/// nullptr if the header block isn't complete yet.
const char* body(const char* buffer);

/// @brief Parses the HTTP status code from the response's start line.
/// @details Works even when only part of the response has arrived, as long
/// as the start line itself is complete. Returns 0 if not yet parseable.
int partial_status(const char* buffer);

/// @brief Finds a named response header's value, given a pointer to the
/// `\r\n\r\n` separator (from headers_end()). Case-insensitive header name
/// match; the returned pointer aliases into `buffer`, and is valid only up
/// to the next `\r\n` or `headers_end_ptr`, whichever is first.
const char* find_header_value(const char* buffer, const char* headers_end_ptr,
                              const char* header_name);

/// @brief Returns whether a named header's value contains `token`
/// (case-insensitive, substring match within that one header's value).
bool header_has_token(const char* buffer, const char* headers_end_ptr, const char* header_name,
                      const char* token);

/// @brief Parses the `Content-Length` header's value, if present and
/// well-formed (a bare integer, optionally followed by trailing whitespace).
bool parse_content_length(const char* buffer, const char* headers_end_ptr, size_t* out_length);

/// @brief Decodes an HTTP chunked body in-place within `buffer`.
/// @details `io_length` must be the buffer's current used length on entry;
/// updated to the new (shorter) length on success. Returns false if the
/// chunk framing is malformed or the terminating zero-length chunk hasn't
/// arrived yet -- callers should treat that as "not complete yet", not
/// necessarily a hard failure, unless the buffer is also known to be full.
bool decode_chunked_body(char* buffer, size_t* io_length);

/// @brief Returns whether the buffered response already contains a complete
/// body per `Content-Length` or a terminated chunked encoding, without
/// waiting for the connection to close.
/// @details Some servers (particularly over TLS/keep-alive) don't close
/// promptly after a complete response; checking this lets a caller act as
/// soon as the advertised body is present instead of idling until the I/O
/// deadline or an explicit close.
bool is_complete(const char* buffer, size_t buffer_len);

} // namespace http_response
