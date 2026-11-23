#include "utils/protobuf.h"

namespace Utils {
namespace Proto {

bool Reader::readVarint(uint64_t &value) {
	value = 0;
	for (int shift = 0; shift < 64; shift += 7) {
		if (pos >= end) {
			error = true;
			return false;
		}
		const uint8_t byte = *pos++;
		value |= (uint64_t)(byte & 0x7F) << shift;
		if (!(byte & 0x80)) {
			return true;
		}
	}
	error = true;
	return false;
}

bool Reader::next(uint32_t &field, uint32_t &wireType) {
	if (error || pos >= end) {
		return false;
	}
	uint64_t tag = 0;
	if (!readVarint(tag)) {
		return false;
	}
	field = (uint32_t)(tag >> 3);
	wireType = (uint32_t)(tag & 7);
	return field != 0;
}

bool Reader::readBytes(const uint8_t *&data, size_t &size) {
	uint64_t length = 0;
	if (!readVarint(length)) {
		return false;
	}
	if (length > (uint64_t)(end - pos)) {
		error = true;
		return false;
	}
	data = pos;
	size = (size_t)length;
	pos += length;
	return true;
}

bool Reader::readFixed64(uint64_t &value) {
	if (end - pos < 8) {
		error = true;
		return false;
	}
	value = 0;
	for (int i = 7; i >= 0; i--) {
		value = (value << 8) | pos[i];
	}
	pos += 8;
	return true;
}

bool Reader::readString(std::string &out) {
	const uint8_t *data = nullptr;
	size_t size = 0;
	if (!readBytes(data, size)) {
		return false;
	}
	out.assign((const char *)data, size);
	return true;
}

bool Reader::skip(uint32_t wireType) {
	uint64_t ignored = 0;
	const uint8_t *data = nullptr;
	size_t size = 0;
	switch (wireType) {
	case VARINT:
		return readVarint(ignored);
	case FIXED64:
		if (end - pos < 8) {
			error = true;
			return false;
		}
		pos += 8;
		return true;
	case LENGTH:
		return readBytes(data, size);
	case FIXED32:
		if (end - pos < 4) {
			error = true;
			return false;
		}
		pos += 4;
		return true;
	default:
		error = true;
		return false;
	}
}

void Writer::rawVarint(uint64_t value) {
	while (value >= 0x80) {
		out.push_back((char)((value & 0x7F) | 0x80));
		value >>= 7;
	}
	out.push_back((char)value);
}

void Writer::rawFixed64(uint64_t value) {
	for (int i = 0; i < 8; i++) {
		out.push_back((char)((value >> (8 * i)) & 0xFF));
	}
}

void Writer::varintField(uint32_t field, uint64_t value) {
	rawVarint(((uint64_t)field << 3) | Reader::VARINT);
	rawVarint(value);
}

void Writer::fixed64Field(uint32_t field, uint64_t value) {
	rawVarint(((uint64_t)field << 3) | Reader::FIXED64);
	rawFixed64(value);
}

void Writer::bytesField(uint32_t field, const std::string &bytes) {
	rawVarint(((uint64_t)field << 3) | Reader::LENGTH);
	rawVarint(bytes.size());
	out += bytes;
}

} // namespace Proto
} // namespace Utils
