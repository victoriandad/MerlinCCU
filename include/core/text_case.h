#pragma once

#include <cctype>
#include <cstddef>

/// @brief Case conversion shared by the controllers and the page renderers,
/// which show headings and labels in uppercase over mixed-case model text
/// (TXT-4, TXT-5).
namespace text_case
{

/// @brief Copies `input` into `output` in uppercase, truncating to fit.
/// @details A null or empty `input` gives an empty string. Does nothing when
/// `output` is null or `output_size` is zero. `input` may be `output`, to
/// convert a buffer in place.
inline void copy_uppercase(const char* input, char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0U)
    {
        return;
    }

    size_t write_index = 0U;
    if (input != nullptr)
    {
        while (input[write_index] != '\0' && write_index + 1U < output_size)
        {
            output[write_index] =
                static_cast<char>(std::toupper(static_cast<unsigned char>(input[write_index])));
            ++write_index;
        }
    }
    output[write_index] = '\0';
}

} // namespace text_case
