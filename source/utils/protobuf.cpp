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

} // namespace Proto
} // namespace Utils
