#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Utils {
namespace Png {

// Encodes 8-bit RGB pixels (row-major, top row first) as a PNG file.
std::string encodeRgb(const uint8_t *rgb, int width, int height);

} // namespace Png
} // namespace Utils
