#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

/// @brief Pinning Home Assistant's TLS certificate by its SHA-256 fingerprint
/// (#17).
/// @details The CCU has no CA store and often no clock at connect time, and
/// HA usually serves a self-signed certificate, so an `https://` endpoint is
/// trusted only when the certificate it presents hashes to the fingerprint
/// saved in the settings. This is the text side: reading a fingerprint as
/// browsers and `openssl x509 -fingerprint -sha256` print it, and printing
/// one back. Pure, so host tests cover it.
namespace cert_pin
{

/// @brief A SHA-256 digest of a certificate's DER bytes.
using Fingerprint = std::array<uint8_t, 32>;

/// @brief Text length of a formatted fingerprint, "AB:CD:...", without the
/// terminator.
inline constexpr size_t kFormattedLength = 32U * 3U - 1U;

/// @brief Reads 64 hex digits; colons, spaces and case are ignored, and an
/// "SHA256 Fingerprint=" style prefix up to the last '=' is skipped.
/// @return The fingerprint, or nothing when the text is not exactly 32 bytes
/// of hex.
std::optional<Fingerprint> parse(std::string_view text);

/// @brief Writes `AB:CD:...` (kFormattedLength characters) into `out`.
void format(const Fingerprint& fingerprint, char* out, size_t out_size);

} // namespace cert_pin
