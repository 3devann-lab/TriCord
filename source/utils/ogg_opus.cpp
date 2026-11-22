#include "utils/ogg_opus.h"
#include "utils/base64_utils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Utils {
namespace OggOpus {

namespace {

const uint32_t SERIAL = 0x54524344; // "TRCD"
// Packets per page: about a second of 20 ms frames keeps pages small.
const size_t PACKETS_PER_PAGE = 50;

void put32(std::string &out, uint32_t v) {
	for (int i = 0; i < 4; i++) {
		out.push_back((char)((v >> (8 * i)) & 0xFF));
	}
}

void put64(std::string &out, uint64_t v) {
	for (int i = 0; i < 8; i++) {
		out.push_back((char)((v >> (8 * i)) & 0xFF));
	}
}

void put16(std::string &out, uint16_t v) {
	out.push_back((char)(v & 0xFF));
	out.push_back((char)(v >> 8));
}

void writePage(std::string &file, const std::vector<std::string> &packets, uint8_t flags, uint64_t granule,
               uint32_t sequence) {
	std::string segments;
	std::string body;
	for (const auto &packet : packets) {
		size_t left = packet.size();
		// Lacing: 255-byte chunks, ended by a value under 255 (0 when the packet is a multiple of 255).
		while (left >= 255) {
			segments.push_back((char)255);
			left -= 255;
		}
		segments.push_back((char)left);
		body += packet;
	}

	std::string page = "OggS";
	page.push_back(0);
	page.push_back((char)flags);
	put64(page, granule);
	put32(page, SERIAL);
	put32(page, sequence);
	put32(page, 0);
	page.push_back((char)segments.size());
	page += segments;
	page += body;

	uint32_t sum = crc((const uint8_t *)page.data(), page.size());
	for (int i = 0; i < 4; i++) {
		page[22 + i] = (char)((sum >> (8 * i)) & 0xFF);
	}
	file += page;
}

} // namespace

uint32_t crc(const uint8_t *data, size_t length) {
	static uint32_t table[256];
	static bool ready = false;
	if (!ready) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t r = i << 24;
			for (int k = 0; k < 8; k++) {
				r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
			}
			table[i] = r;
		}
		ready = true;
	}
	uint32_t sum = 0;
	for (size_t i = 0; i < length; i++) {
		sum = (sum << 8) ^ table[((sum >> 24) ^ data[i]) & 0xFF];
	}
	return sum;
}

std::string build(const std::vector<std::string> &packets, uint32_t inputRate, uint16_t preSkip48k,
                  uint32_t frameSamples48k) {
	std::string file;
	uint32_t sequence = 0;

	std::string head = "OpusHead";
	head.push_back(1);
	head.push_back(1);
	put16(head, preSkip48k);
	put32(head, inputRate);
	put16(head, 0);
	head.push_back(0);
	writePage(file, {head}, 0x02, 0, sequence++);

	std::string tags = "OpusTags";
	const std::string vendor = "TriCord";
	put32(tags, (uint32_t)vendor.size());
	tags += vendor;
	put32(tags, 0);
	writePage(file, {tags}, 0x00, 0, sequence++);

	uint64_t granule = preSkip48k;
	for (size_t start = 0; start < packets.size(); start += PACKETS_PER_PAGE) {
		size_t end = std::min(packets.size(), start + PACKETS_PER_PAGE);
		std::vector<std::string> page(packets.begin() + start, packets.begin() + end);
		granule += (uint64_t)(end - start) * frameSamples48k;
		uint8_t flags = end == packets.size() ? 0x04 : 0x00;
		writePage(file, page, flags, granule, sequence++);
	}
	if (packets.empty()) {
		writePage(file, {}, 0x04, granule, sequence++);
	}
	return file;
}

std::string waveform(const std::vector<int> &framePeaks) {
	if (framePeaks.empty()) {
		return "";
	}
	size_t count = std::min<size_t>(256, framePeaks.size());
	std::vector<unsigned char> samples(count);
	for (size_t i = 0; i < count; i++) {
		size_t from = i * framePeaks.size() / count;
		size_t to = std::max(from + 1, (i + 1) * framePeaks.size() / count);
		int peak = 0;
		for (size_t k = from; k < to; k++) {
			peak = std::max(peak, framePeaks[k]);
		}
		// Square root, so quiet speech still shows as more than a flat line.
		double level = std::sqrt(std::min(1.0, peak / 32768.0));
		samples[i] = (unsigned char)std::lround(level * 255.0);
	}
	return Base64::encode(samples.data(), samples.size());
}

} // namespace OggOpus
} // namespace Utils
