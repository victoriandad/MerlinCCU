#pragma once

#include <cstddef>
#include <cstdint>

/// @brief Golden-image (pixel snapshot) test support for screens.cpp
/// rendering (issue #71).
/// @details Goldens are stored as PBM (`P4`, portable bitmap) files under
/// `tests/host/golden/<name>.pbm` -- the UI framebuffer's own row-major,
/// MSB-first 1bpp packing (see panel_config.h's kUiStride) is already a
/// valid PBM bitplane, so a golden file is just that raw buffer with a
/// `P4\n<width> <height>\n` text header prepended. Any PBM-aware image
/// viewer can open one directly for a human to look at.
namespace golden_test
{

/// @brief Compares `fb` (kUiWidth x kUiHeight, kUiFbSize bytes) against the
/// stored golden image named `name`.
/// @details Behaviour depends on the `MERLINCCU_REGENERATE_GOLDEN`
/// environment variable:
/// - Unset, golden exists: compares byte-for-byte. On mismatch, prints the
///   first differing pixel's row/column and writes the actual render next to
///   the golden as `<name>.actual.pbm` for inspection, then returns false.
/// - Unset, golden missing: fails with a message explaining how to generate
///   it (this is deliberate -- a silently-created golden on first run would
///   defeat the point of a regression test).
/// - Set (to any non-empty value): writes/overwrites the golden with the
///   current render and returns true. This is the "regenerate" step after a
///   deliberate layout change -- see docs/architecture.md.
bool check_golden(const char* name, const uint8_t* fb);

} // namespace golden_test
