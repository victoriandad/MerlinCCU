#pragma once

/// @brief Shared token for over-the-air firmware uploads (#154).
/// @details Copy this file to `config/ota_credentials.h`, set a long random
/// token, and keep that local file out of version control. Every unit built
/// with the same file accepts the same token.
///
/// Uploads send it in the `X-OTA-Token` header, from the web page's Firmware
/// card or from a script. While the token is empty (or the file is missing),
/// the CCU refuses every upload.
///
/// Anyone on the network who knows the token can replace the firmware, so
/// treat it like a password. Web config access control (#158) will replace it.
inline constexpr char kOtaUploadToken[] = "";
