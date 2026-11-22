#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Utils {
namespace OggOpus {

// Wraps mono Opus packets, each frameSamples48k long at 48 kHz, into an Ogg Opus file
// (RFC 7845): an OpusHead page, an OpusTags page, then the audio.
std::string build(const std::vector<std::string> &packets, uint32_t inputRate, uint16_t preSkip48k,
                  uint32_t frameSamples48k);

// The CRC Ogg pages carry (polynomial 0x04C11DB7, no reflection).
uint32_t crc(const uint8_t *data, size_t length);

// Discord's voice message waveform: up to 256 amplitude bytes, base64 encoded.
std::string waveform(const std::vector<int> &framePeaks);

} // namespace OggOpus
} // namespace Utils
