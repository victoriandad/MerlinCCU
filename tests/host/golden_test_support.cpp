#include "golden_test_support.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "panel_config.h"

#ifndef MERLINCCU_GOLDEN_DIR
#error "MERLINCCU_GOLDEN_DIR must be defined by the build (see tests/host/CMakeLists.txt)"
#endif

namespace golden_test
{

namespace
{

std::string golden_path(const char* name)
{
    return std::string(MERLINCCU_GOLDEN_DIR) + "/" + name + ".pbm";
}

std::string actual_path(const char* name)
{
    return std::string(MERLINCCU_GOLDEN_DIR) + "/" + name + ".actual.pbm";
}

bool regenerate_requested()
{
    const char* value = std::getenv("MERLINCCU_REGENERATE_GOLDEN");
    return value != nullptr && value[0] != '\0';
}

/// @brief Writes `fb` as a `P4` PBM file at `path`. Returns false on I/O failure.
bool write_pbm(const std::string& path, const uint8_t* fb)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
    {
        std::printf("    golden: could not open '%s' for writing\n", path.c_str());
        return false;
    }

    std::fprintf(file, "P4\n%d %d\n", kUiWidth, kUiHeight);
    const size_t written = std::fwrite(fb, 1, static_cast<size_t>(kUiFbSize), file);
    std::fclose(file);
    return written == static_cast<size_t>(kUiFbSize);
}

/// @brief Reads a `P4` PBM file's raw bitplane into `out` (kUiFbSize bytes).
/// @details Only accepts files whose header matches this build's
/// kUiWidth/kUiHeight exactly -- a size mismatch almost certainly means the
/// golden predates a real UI resolution change, not something to silently
/// reinterpret.
bool read_pbm(const std::string& path, std::vector<uint8_t>& out)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        return false;
    }

    char magic[3] = {};
    int width = 0;
    int height = 0;
    const bool header_ok = std::fscanf(file, "%2s %d %d", magic, &width, &height) == 3 &&
                           std::strcmp(magic, "P4") == 0;
    // PBM requires exactly one whitespace byte between the header and the
    // raw bitplane data.
    if (header_ok)
    {
        std::fgetc(file);
    }
    if (!header_ok || width != kUiWidth || height != kUiHeight)
    {
        std::fclose(file);
        std::printf("    golden: '%s' header mismatch (expected P4 %d %d)\n", path.c_str(),
                    kUiWidth, kUiHeight);
        return false;
    }

    out.resize(static_cast<size_t>(kUiFbSize));
    const size_t read = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    return read == out.size();
}

} // namespace

bool check_golden(const char* name, const uint8_t* fb)
{
    const std::string path = golden_path(name);

    if (regenerate_requested())
    {
        const bool ok = write_pbm(path, fb);
        std::printf("    golden: %s '%s'\n", ok ? "regenerated" : "FAILED to regenerate",
                    path.c_str());
        return ok;
    }

    std::vector<uint8_t> golden;
    if (!read_pbm(path, golden))
    {
        std::printf(
            "    golden: '%s' missing or unreadable. Run with "
            "MERLINCCU_REGENERATE_GOLDEN=1 to create it, then inspect the result before "
            "committing it.\n",
            path.c_str());
        return false;
    }

    if (std::memcmp(golden.data(), fb, golden.size()) == 0)
    {
        return true;
    }

    // Report the first differing pixel (not just byte) since a single flipped
    // bit is otherwise hard to place on a 252x320 page by eye.
    for (int y = 0; y < kUiHeight; ++y)
    {
        for (int x = 0; x < kUiWidth; ++x)
        {
            const size_t byte_index = static_cast<size_t>(y) * static_cast<size_t>(kUiStride) +
                                      static_cast<size_t>(x >> 3);
            const uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
            if ((golden[byte_index] & mask) != (fb[byte_index] & mask))
            {
                std::printf("    golden: '%s' first differing pixel at (x=%d, y=%d)\n",
                            path.c_str(), x, y);
                write_pbm(actual_path(name), fb);
                std::printf("    golden: actual render written to '%s' for inspection\n",
                            actual_path(name).c_str());
                return false;
            }
        }
    }

    // Sizes matched and memcmp found a difference, but the pixel scan above
    // didn't (shouldn't happen -- only reachable if kUiFbSize has padding
    // bytes beyond the last row that the scan doesn't cover).
    std::printf("    golden: '%s' differs outside the scanned pixel grid\n", path.c_str());
    write_pbm(actual_path(name), fb);
    return false;
}

} // namespace golden_test
