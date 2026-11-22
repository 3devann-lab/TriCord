#include "utils/png_writer.h"
#include <cstring>
#include <zlib.h>

namespace Utils {
namespace Png {

namespace {

void put32(std::string &out, uint32_t v) {
	out.push_back((char)(v >> 24));
	out.push_back((char)(v >> 16));
	out.push_back((char)(v >> 8));
	out.push_back((char)v);
}

void chunk(std::string &out, const char *type, const std::string &data) {
	put32(out, (uint32_t)data.size());
	std::string body(type, 4);
	body += data;
	out += body;
	put32(out, (uint32_t)crc32(0L, (const Bytef *)body.data(), (uInt)body.size()));
}

} // namespace

std::string encodeRgb(const uint8_t *rgb, int width, int height) {
	// Each row gets the Sub filter, which suits flat drawings well.
	const size_t stride = (size_t)width * 3;
	std::vector<uint8_t> raw((stride + 1) * height);
	for (int y = 0; y < height; y++) {
		uint8_t *dst = &raw[y * (stride + 1)];
		const uint8_t *src = rgb + y * stride;
		dst[0] = 1;
		for (size_t i = 0; i < stride; i++) {
			uint8_t left = i >= 3 ? src[i - 3] : 0;
			dst[1 + i] = (uint8_t)(src[i] - left);
		}
	}

	uLongf packedSize = compressBound(raw.size());
	std::string packed(packedSize, '\0');
	if (compress2((Bytef *)&packed[0], &packedSize, raw.data(), raw.size(), 6) != Z_OK) {
		return "";
	}
	packed.resize(packedSize);

	std::string png("\x89PNG\r\n\x1a\n", 8);
	std::string ihdr;
	put32(ihdr, (uint32_t)width);
	put32(ihdr, (uint32_t)height);
	ihdr.push_back(8); // bit depth
	ihdr.push_back(2); // RGB
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	chunk(png, "IHDR", ihdr);
	chunk(png, "IDAT", packed);
	chunk(png, "IEND", "");
	return png;
}

} // namespace Png
} // namespace Utils
